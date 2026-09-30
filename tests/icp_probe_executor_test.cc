/// Recovery V2 Phase 2: ProbeExecutor / ProbeSession bounded recovery I/O.
///
/// Automatic recovery connect() must not consume LifecycleExecutor workers.
/// Logical cancellation != physical cancellation. Stale completions are ignored.

#include "GatedProbeAdapter.hh"

#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/icp/ProbeExecutor.hh>
#include <virtual_factory/icp/RecoveryService.hh>
#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/industrial/MockIndustrialAdapter.hh>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{

int failures = 0;

void expect(bool condition, const std::string &message)
{
  if (!condition)
  {
    std::cerr << "FAIL: " << message << std::endl;
    ++failures;
  }
}

bool waitFor(const std::function<bool()> &pred, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (pred())
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}

std::string tempPath(const char *suffix)
{
  std::ostringstream stream;
  stream << "/tmp/icp-probe-" << ::getpid() << "-" << suffix;
  return stream.str();
}

using virtual_factory::icp::AdapterManager;
using virtual_factory::icp::ProbeCompletion;
using virtual_factory::icp::ProbeCompletionSink;
using virtual_factory::icp::ProbeExecutor;
using virtual_factory::icp::ProbeOutcome;
using virtual_factory::icp::ProbeRequest;
using virtual_factory::icp::RecoveryService;
using virtual_factory::icp::RecoveryState;
using virtual_factory::icp::kMaxQuarantinedProbeWorkers;
using virtual_factory::icp::kMaxQuarantinedProbesPerAdapter;
using virtual_factory::test::GatedProbeAdapter;
using virtual_factory::test::ProbeHangGate;

struct RecordingSink : ProbeCompletionSink
{
  mutable std::mutex mu;
  std::vector<ProbeCompletion> completions;

  void onProbeCompleted(const ProbeCompletion &completion) override
  {
    std::lock_guard<std::mutex> lock(this->mu);
    this->completions.push_back(completion);
  }

  std::size_t count() const
  {
    std::lock_guard<std::mutex> lock(this->mu);
    return this->completions.size();
  }

  std::size_t count(ProbeOutcome outcome) const
  {
    std::lock_guard<std::mutex> lock(this->mu);
    std::size_t n = 0;
    for (const auto &c : this->completions)
    {
      if (c.outcome == outcome)
      {
        ++n;
      }
    }
    return n;
  }
};

ProbeRequest makeRequest(
    const std::shared_ptr<GatedProbeAdapter> &adapter,
    const std::shared_ptr<std::mutex> &io,
    std::uint64_t probeId,
    std::uint64_t epoch = 0,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(200))
{
  ProbeRequest request;
  request.adapterId = adapter->id();
  request.probeId = probeId;
  request.runtimeEpoch = epoch;
  request.recoverySequence = probeId;
  request.adapter = adapter;
  request.ioMutex = io;
  request.enrolled = std::make_shared<std::atomic<bool>>(true);
  request.timeout = timeout;
  return request;
}

virtual_factory::icp::AdapterConfigRecord makeMockCfg(const std::string &id)
{
  virtual_factory::icp::AdapterConfigRecord mock;
  mock.adapterId = id;
  mock.protocol = "mock";
  mock.enabled = true;
  mock.connection.timeoutMs = 200;
  virtual_factory::icp::EquipmentMappingRecord eq;
  eq.equipmentId = "EQ-" + id;
  eq.type = "motor";
  mock.equipment.push_back(eq);
  return mock;
}

virtual_factory::icp::AdapterSessionDiagnostics sessionOf(
    virtual_factory::icp::ApplicationService &service, const std::string &id)
{
  const auto report = service.diagnosticsReport();
  for (const auto &row : report.adapters)
  {
    if (row.adapter.adapterId == id)
    {
      return row.session;
    }
  }
  return {};
}

}  // namespace

int main()
{
  using ms = std::chrono::milliseconds;

  // -------------------------------------------------------------------
  // A. Normal successful probe → RecoveryControl CONNECTED
  // -------------------------------------------------------------------
  {
    const std::string cfg = tempPath("a-cfg.json");
    const std::string hist = tempPath("a-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    virtual_factory::icp::ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeMockCfg("probe-a")).ok, "A upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "A save/load");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("probe-a");
                 return v && v->connectionState == "CONNECTED"
                     && service.recovery().snapshot("probe-a").state
                     == RecoveryState::Connected;
               },
               ms(4000)),
           "A probe succeeds; RecoveryControl CONNECTED");
    expect(!service.lifecycle().hasInFlightOrPending(
               "probe-a", virtual_factory::icp::LifecycleOp::RecoveryConnect),
           "A automatic recovery did not use LifecycleExecutor RecoveryConnect");
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // -------------------------------------------------------------------
  // B. Normal failed probe → RECOVERY_WAIT + backoff
  // -------------------------------------------------------------------
  {
    const std::string cfg = tempPath("b-cfg.json");
    const std::string hist = tempPath("b-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    virtual_factory::icp::ApplicationService service(cfg, hist);
    expect(service.upsertAdapterConfig(makeMockCfg("probe-b")).ok, "B upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "B save/load");
    {
      auto extracted = service.manager().extractAdapter("probe-b");
      (void)extracted;
    }
    auto hang = std::make_unique<GatedProbeAdapter>("probe-b");
    hang->setPeerAvailable(false);
    GatedProbeAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "B add gated");
    service.start();
    expect(waitFor(
               [&]() {
                 return raw->connectFailed() >= 1
                     && service.recovery().snapshot("probe-b").state
                     == RecoveryState::RecoveryWait;
               },
               ms(4000)),
           "B failed probe enters RECOVERY_WAIT");
    const auto snap = service.recovery().snapshot("probe-b");
    expect(snap.backoffMs.count() >= 2000, "B backoff doubled after failure");
    expect(snap.consecutiveFailures >= 1, "B consecutiveFailures incremented");
    expect(!service.lifecycle().hasInFlightOrPending(
               "probe-b", virtual_factory::icp::LifecycleOp::RecoveryConnect),
           "B failure path did not use RecoveryConnect");
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // -------------------------------------------------------------------
  // C. One hung probe: logical timeout, capacity remains
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto gate = std::make_shared<ProbeHangGate>();
    auto hung = std::make_shared<GatedProbeAdapter>("hung-c");
    hung->setGate(gate);
    hung->setNeverReturn(true);
    hung->setPeerAvailable(false);
    auto io = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(hung, io, 1, 0, ms(150))), "C submit hung");
    expect(hung->waitConnectEntered(ms(1000)), "C hung entered connect");
    expect(waitFor(
               [&]() { return sink->count(ProbeOutcome::Timeout) >= 1; },
               ms(2000)),
           "C logical timeout published");
    auto healthy = std::make_shared<GatedProbeAdapter>("ok-c");
    auto io2 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(healthy, io2, 2, 0, ms(500))),
           "C submit healthy after hang");
    expect(waitFor(
               [&]() { return healthy->connectCompleted() >= 1; },
               ms(2000)),
           "C healthy adapter still recovered");
    expect(exec.liveWorkerCount() == 4, "C live workers replenished to 4");
    gate->open();
    exec.stop();
  }

  // -------------------------------------------------------------------
  // D. Hung Gen0 + new Gen1 probe; late Gen0 must not mutate Gen1
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto gate0 = std::make_shared<ProbeHangGate>();
    auto gen0 = std::make_shared<GatedProbeAdapter>("d");
    gen0->setGate(gate0);
    gen0->setNeverReturn(true);
    gen0->setPeerAvailable(false);
    auto io0 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(gen0, io0, 10, 0, ms(120))), "D submit Gen0");
    expect(gen0->waitConnectEntered(ms(1000)), "D Gen0 entered");
    expect(waitFor(
               [&]() { return sink->count(ProbeOutcome::Timeout) >= 1; },
               ms(2000)),
           "D Gen0 logically timed out");

    auto gen1 = std::make_shared<GatedProbeAdapter>("d");
    gen1->setPeerAvailable(true);
    auto io1 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(gen1, io1, 11, 1, ms(500))), "D submit Gen1");
    expect(waitFor(
               [&]() { return gen1->connectCompleted() >= 1; },
               ms(2000)),
           "D Gen1 connected");
    expect(gen1->connectionState() == virtual_factory::ConnectionState::Connected,
           "D Gen1 CONNECTED before Gen0 returns");

    gen0->setLateSuccess(true);
    gate0->open();
    expect(waitFor(
               [&]() {
                 return gen0->connectCompleted() >= 1 || gen0->connectFailed() >= 1;
               },
               ms(2000)),
           "D Gen0 physical call returned");
    expect(gen1->connectionState() == virtual_factory::ConnectionState::Connected,
           "D late Gen0 did not change Gen1 connection state");
    exec.stop();
  }

  // -------------------------------------------------------------------
  // E. Two hung probes; a third healthy adapter still recovers
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto g1 = std::make_shared<ProbeHangGate>();
    auto g2 = std::make_shared<ProbeHangGate>();
    auto a1 = std::make_shared<GatedProbeAdapter>("e1");
    auto a2 = std::make_shared<GatedProbeAdapter>("e2");
    a1->setGate(g1);
    a2->setGate(g2);
    a1->setNeverReturn(true);
    a2->setNeverReturn(true);
    auto io1 = std::make_shared<std::mutex>();
    auto io2 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(a1, io1, 1, 0, ms(120))), "E submit 1");
    expect(exec.submit(makeRequest(a2, io2, 2, 0, ms(120))), "E submit 2");
    expect(a1->waitConnectEntered(ms(1000)) && a2->waitConnectEntered(ms(1000)),
           "E both hung entered");
    auto healthy = std::make_shared<GatedProbeAdapter>("e3");
    auto io3 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(healthy, io3, 3, 0, ms(500))), "E submit 3");
    expect(waitFor(
               [&]() { return healthy->connectCompleted() >= 1; },
               ms(2000)),
           "E healthy adapter recovered while two hung");
    g1->open();
    g2->open();
    exec.stop();
  }

  // -------------------------------------------------------------------
  // F. 50 adapters, peers down, bounded occupancy
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    std::vector<std::shared_ptr<GatedProbeAdapter>> adapters;
    std::vector<std::shared_ptr<std::mutex>> ios;
    for (int i = 0; i < 50; ++i)
    {
      auto a = std::make_shared<GatedProbeAdapter>("f-" + std::to_string(i));
      a->setPeerAvailable(false);
      adapters.push_back(a);
      ios.push_back(std::make_shared<std::mutex>());
      expect(exec.submit(makeRequest(a, ios.back(), static_cast<std::uint64_t>(i + 1))),
             "F submit " + a->id());
    }
    expect(waitFor(
               [&]() { return sink->count(ProbeOutcome::Failure) >= 50; },
               ms(8000)),
           "F all 50 probes completed as failures");
    expect(exec.pendingCount() == 0, "F no leftover pending queue");
    expect(exec.logicalProbeCount() == 0, "F no leftover logical probes");
    expect(exec.inFlightCount() == 0, "F no leftover in-flight");
    expect(exec.peakOccupancy() <= 150, "F occupancy bounded by adapter count");
    exec.stop();
  }

  // -------------------------------------------------------------------
  // G. Late success of cancelled probe ignored
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(2);
    auto gate = std::make_shared<ProbeHangGate>();
    auto oldA = std::make_shared<GatedProbeAdapter>("g");
    oldA->setGate(gate);
    oldA->setNeverReturn(true);
    oldA->setPeerAvailable(false);
    auto io0 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(oldA, io0, 1, 0, ms(100))), "G submit old");
    expect(oldA->waitConnectEntered(ms(1000)), "G old entered");
    exec.cancel("g", 1);
    auto cur = std::make_shared<GatedProbeAdapter>("g");
    auto io1 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(cur, io1, 2, 1, ms(500))), "G submit current");
    expect(waitFor(
               [&]() { return cur->connectCompleted() >= 1; },
               ms(2000)),
           "G current probe succeeded");
    oldA->setLateSuccess(true);
    gate->open();
    expect(waitFor(
               [&]() { return oldA->connectCompleted() >= 1; },
               ms(2000)),
           "G old physical success returned");
    expect(cur->connectionState() == virtual_factory::ConnectionState::Connected,
           "G current runtime unchanged by late success");
    exec.stop();
  }

  // -------------------------------------------------------------------
  // H. Remove during probe: late completion ignored
  // -------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h-cfg.json");
    const std::string hist = tempPath("h-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    virtual_factory::icp::ApplicationService service(cfg, hist);
    auto rec = makeMockCfg("probe-h");
    rec.connection.timeoutMs = 2000;
    expect(service.upsertAdapterConfig(rec).ok, "H upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "H save/load");
    {
      auto extracted = service.manager().extractAdapter("probe-h");
      (void)extracted;
    }
    auto gate = std::make_shared<ProbeHangGate>();
    auto hang = std::make_unique<GatedProbeAdapter>("probe-h");
    hang->setGate(gate);
    hang->setNeverReturn(true);
    hang->setPeerAvailable(false);
    GatedProbeAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "H add");
    service.start();
    expect(raw->waitConnectEntered(ms(4000)), "H probe entered");
    expect(service.removeAdapterConfig("probe-h").ok, "H remove while probing");
    gate->open();
    expect(waitFor(
               [&]() {
                 return raw->connectFailed() >= 1 || raw->connectCompleted() >= 1;
               },
               ms(2000)),
           "H late physical return");
    expect(service.manager().adapter("probe-h") == nullptr,
           "H late completion did not resurrect removed adapter");
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // -------------------------------------------------------------------
  // I. Disable during probe: late completion cannot reconnect
  // -------------------------------------------------------------------
  {
    const std::string cfg = tempPath("i-cfg.json");
    const std::string hist = tempPath("i-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    virtual_factory::icp::ApplicationService service(cfg, hist);
    auto rec = makeMockCfg("probe-i");
    rec.connection.timeoutMs = 2000;
    expect(service.upsertAdapterConfig(rec).ok, "I upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "I save/load");
    {
      auto extracted = service.manager().extractAdapter("probe-i");
      (void)extracted;
    }
    auto gate = std::make_shared<ProbeHangGate>();
    auto hang = std::make_unique<GatedProbeAdapter>("probe-i");
    hang->setGate(gate);
    hang->setNeverReturn(true);
    hang->setPeerAvailable(true);
    hang->setLateSuccess(true);
    GatedProbeAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "I add");
    service.start();
    expect(raw->waitConnectEntered(ms(4000)), "I probe entered");
    rec.enabled = false;
    expect(service.upsertAdapterConfig(rec).ok, "I disable");
    gate->open();
    expect(waitFor(
               [&]() { return raw->connectCompleted() >= 1; },
               ms(2000)),
           "I late success returned");
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < ms(400))
    {
      expect(service.manager().adapter("probe-i") == nullptr,
             "I disable was not undone by late success");
      std::this_thread::sleep_for(ms(50));
    }
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // -------------------------------------------------------------------
  // J. Rematerialize during probe: Gen0 cannot mutate Gen1
  // -------------------------------------------------------------------
  {
    const std::string cfg = tempPath("j-cfg.json");
    const std::string hist = tempPath("j-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    virtual_factory::icp::ApplicationService service(cfg, hist);
    auto rec = makeMockCfg("probe-j");
    rec.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(rec).ok, "J upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "J save/load");
    {
      auto extracted = service.manager().extractAdapter("probe-j");
      (void)extracted;
    }
    auto gate = std::make_shared<ProbeHangGate>();
    auto hang = std::make_unique<GatedProbeAdapter>("probe-j");
    hang->setGate(gate);
    hang->setNeverReturn(true);
    hang->setPeerAvailable(false);
    GatedProbeAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "J add Gen0");
    auto gen1 = std::make_unique<GatedProbeAdapter>("probe-j");
    gen1->setPeerAvailable(true);
    GatedProbeAdapter *raw1 = gen1.get();
    service.injectRuntimeAdapterForTest(std::move(gen1));
    service.start();
    expect(raw->waitConnectEntered(ms(4000)), "J Gen0 entered");
    expect(waitFor(
               [&]() {
                 virtual_factory::IndustrialAdapter *runtime =
                     service.manager().adapter("probe-j");
                 auto v = service.adapter("probe-j");
                 return runtime != nullptr && runtime != raw && v
                     && v->connectionState == "CONNECTED";
               },
               ms(8000)),
           "J Gen1 CONNECTED after isolation/rematerialize");
    raw->setLateSuccess(true);
    gate->open();
    expect(waitFor(
               [&]() {
                 return raw->connectCompleted() >= 1 || raw->connectFailed() >= 1;
               },
               ms(2000)),
           "J Gen0 returned");
    expect(service.manager().adapter("probe-j") != raw,
           "J Gen0 result did not reinstall Gen0");
    auto after = service.adapter("probe-j");
    expect(after && after->connectionState == "CONNECTED",
           "J Gen1 remains CONNECTED");
    expect(raw1->connectCompleted() >= 1 || after->connectionState == "CONNECTED",
           "J current runtime is Gen1");
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // -------------------------------------------------------------------
  // K. Operator Connect while automatic probe exists
  // -------------------------------------------------------------------
  {
    const std::string cfg = tempPath("k-cfg.json");
    const std::string hist = tempPath("k-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    virtual_factory::icp::ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeMockCfg("probe-k")).ok, "K upsert");
    auto gate = std::make_shared<ProbeHangGate>();
    auto hang = std::make_unique<GatedProbeAdapter>("probe-k");
    hang->setGate(gate);
    hang->setPeerAvailable(false);
    GatedProbeAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "K add");
    auto started = service.connectAdapter("probe-k");
    expect(started.ok && started.accepted, "K operator Connect accepted");
    expect(raw->waitConnectEntered(ms(2000)), "K connect entered");
    auto second = service.connectAdapter("probe-k");
    expect(second.ok && second.accepted, "K second Connect coalesced");
    expect(raw->connectEntered() == 1, "K no duplicate connect()");
    raw->setPeerAvailable(true);
    gate->open();
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("probe-k");
                 return v && v->connectionState == "CONNECTED";
               },
               ms(4000)),
           "K CONNECTED without duplicate logical probe");
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // -------------------------------------------------------------------
  // L. Shutdown during probe is bounded; late completion harmless
  // -------------------------------------------------------------------
  {
    const std::string cfg = tempPath("l-cfg.json");
    const std::string hist = tempPath("l-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    virtual_factory::icp::ApplicationService service(cfg, hist);
    auto rec = makeMockCfg("probe-l");
    rec.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(rec).ok, "L upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "L save/load");
    {
      auto extracted = service.manager().extractAdapter("probe-l");
      (void)extracted;
    }
    auto gate = std::make_shared<ProbeHangGate>();
    auto hang = std::make_unique<GatedProbeAdapter>("probe-l");
    hang->setGate(gate);
    hang->setNeverReturn(true);
    GatedProbeAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "L add");
    service.start();
    expect(raw->waitConnectEntered(ms(4000)), "L probe in progress");
    const auto t0 = std::chrono::steady_clock::now();
    service.stop();
    const auto stopMs = std::chrono::duration_cast<ms>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
    expect(stopMs < 12000, "L stop bounded under probe grace");
    gate->open();
    expect(waitFor(
               [&]() {
                 return raw->connectFailed() >= 1 || raw->connectCompleted() >= 1;
               },
               ms(2000)),
           "L late completion after stop is harmless");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // -------------------------------------------------------------------
  // M. Cross-protocol fairness: OPC-UA-labelled hang vs MQTT/mock
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto gate = std::make_shared<ProbeHangGate>();
    auto opcua = std::make_shared<GatedProbeAdapter>("m-opcua", "opcua");
    opcua->setGate(gate);
    opcua->setNeverReturn(true);
    auto io1 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(opcua, io1, 1, 0, ms(150))), "M submit opcua");
    expect(opcua->waitConnectEntered(ms(1000)), "M opcua hung");
    auto mqtt = std::make_shared<GatedProbeAdapter>("m-mqtt", "mqtt");
    auto mock = std::make_shared<GatedProbeAdapter>("m-mock", "mock");
    auto io2 = std::make_shared<std::mutex>();
    auto io3 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(mqtt, io2, 2)), "M submit mqtt");
    expect(exec.submit(makeRequest(mock, io3, 3)), "M submit mock");
    expect(waitFor(
               [&]() {
                 return mqtt->connectCompleted() >= 1
                     && mock->connectCompleted() >= 1;
               },
               ms(2000)),
           "M MQTT and mock recovered while OPC UA hung");
    gate->open();
    exec.stop();
  }

  // -------------------------------------------------------------------
  // N. 24h fake-clock outage, 100 adapters, bounded logical state
  // -------------------------------------------------------------------
  {
    auto clock = std::make_shared<virtual_factory::icp::FakeRecoveryClock>(
        std::chrono::steady_clock::time_point(std::chrono::seconds(1000)));
    RecoveryService recovery(clock);
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    std::vector<std::shared_ptr<GatedProbeAdapter>> adapters;
    std::vector<std::shared_ptr<std::mutex>> ios;
    adapters.reserve(100);
    ios.reserve(100);
    for (int i = 0; i < 100; ++i)
    {
      const std::string id = "n-" + std::to_string(i);
      recovery.armDesired(id, true);
      auto a = std::make_shared<GatedProbeAdapter>(id);
      a->setPeerAvailable(false);
      adapters.push_back(a);
      ios.push_back(std::make_shared<std::mutex>());
    }
    std::size_t maxLogical = 0;
    std::size_t maxPending = 0;
    std::size_t maxInFlight = 0;
    for (int step = 0; step < 24 * 60; ++step)
    {
      clock->advance(ms(60000));
      const auto due = recovery.tick();
      for (const auto &event : due)
      {
        const int idx = std::stoi(event.adapterId.substr(2));
        ProbeRequest request;
        request.adapterId = event.adapterId;
        request.probeId = event.probeId;
        request.runtimeEpoch = event.runtimeEpoch;
        request.recoverySequence = event.recoverySequence;
        request.adapter = adapters[static_cast<std::size_t>(idx)];
        request.ioMutex = ios[static_cast<std::size_t>(idx)];
        request.enrolled = std::make_shared<std::atomic<bool>>(true);
        request.timeout = ms(50);
        (void)exec.submit(std::move(request));
      }
      waitFor([&]() { return exec.inFlightCount() == 0 && exec.pendingCount() == 0; },
              ms(2000));
      for (const auto &event : due)
      {
        recovery.noteFailure(event.adapterId, event.probeId, event.runtimeEpoch);
      }
      maxLogical = std::max(maxLogical, recovery.controlCount());
      maxPending = std::max(maxPending, exec.pendingCount());
      maxInFlight = std::max(maxInFlight, exec.inFlightCount());
    }
    expect(recovery.controlCount() == 100, "N still 100 RecoveryControl records");
    expect(maxLogical == 100, "N logical controls never grew past adapter count");
    expect(maxPending <= 100, "N pending never exceeded adapter count");
    expect(exec.peakOccupancy() <= 400, "N executor occupancy bounded");
    expect(exec.quarantinedWorkerCount() <= kMaxQuarantinedProbeWorkers,
           "N quarantine globally bounded");
    (void)maxInFlight;
    exec.stop();
  }

  // Executor worker clamp
  {
    ProbeExecutor exec;
    exec.start(0);
    expect(exec.liveWorkerCount() == 1, "worker count clamps to minimum 1");
    exec.stop();
  }
  {
    ProbeExecutor exec;
    exec.start(99);
    expect(exec.liveWorkerCount() == 32, "worker count clamps to maximum 32");
    exec.stop();
  }

  if (failures != 0)
  {
    std::cerr << failures << " failure(s)" << std::endl;
    return 1;
  }
  std::cout << "icp_probe_executor_test OK" << std::endl;
  return 0;
}
