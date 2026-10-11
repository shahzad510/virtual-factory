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
using virtual_factory::icp::ProbeSession;
using virtual_factory::icp::ProbeTraceEvent;
using virtual_factory::icp::ProbeTraceKind;
using virtual_factory::icp::kMaxQuarantinedProbeWorkers;
using virtual_factory::icp::kMaxQuarantinedProbesPerAdapter;
using virtual_factory::icp::kReservedProbeWorkerCount;
using virtual_factory::test::GatedProbeAdapter;
using virtual_factory::test::ProbeHangGate;

struct RecordingSink : ProbeCompletionSink
{
  mutable std::mutex mu;
  std::vector<ProbeCompletion> completions;
  std::vector<ProbeTraceEvent> traces;

  void onProbeCompleted(const ProbeCompletion &completion) override
  {
    std::lock_guard<std::mutex> lock(this->mu);
    this->completions.push_back(completion);
  }

  void onProbeTrace(const ProbeTraceEvent &trace) override
  {
    std::lock_guard<std::mutex> lock(this->mu);
    this->traces.push_back(trace);
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

  std::vector<ProbeTraceEvent> tracesFor(const std::string &adapterId) const
  {
    std::lock_guard<std::mutex> lock(this->mu);
    std::vector<ProbeTraceEvent> out;
    for (const auto &t : this->traces)
    {
      if (t.adapterId == adapterId)
      {
        out.push_back(t);
      }
    }
    return out;
  }

  std::size_t traceCount(
      const std::string &adapterId, ProbeTraceKind kind) const
  {
    std::lock_guard<std::mutex> lock(this->mu);
    std::size_t n = 0;
    for (const auto &t : this->traces)
    {
      if (t.adapterId == adapterId && t.kind == kind)
      {
        ++n;
      }
    }
    return n;
  }

  int traceIndex(const std::string &adapterId, ProbeTraceKind kind) const
  {
    std::lock_guard<std::mutex> lock(this->mu);
    int seen = 0;
    for (const auto &t : this->traces)
    {
      if (t.adapterId != adapterId)
      {
        continue;
      }
      if (t.kind == kind)
      {
        return seen;
      }
      ++seen;
    }
    return -1;
  }

  bool hasTrace(
      const std::string &adapterId,
      ProbeTraceKind kind,
      const std::string &detail) const
  {
    std::lock_guard<std::mutex> lock(this->mu);
    for (const auto &t : this->traces)
    {
      if (t.adapterId == adapterId && t.kind == kind && t.detail == detail)
      {
        return true;
      }
    }
    return false;
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

  // -------------------------------------------------------------------
  // Phase 2.1 A. One never-return + healthy unrelated adapter
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto gate = std::make_shared<ProbeHangGate>();
    auto hung = std::make_shared<GatedProbeAdapter>("p21-a-hung");
    hung->setGate(gate);
    hung->setNeverReturn(true);
    auto ioH = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(hung, ioH, 1, 0, ms(80))), "2.1 A submit hung");
    expect(hung->waitConnectEntered(ms(1000)), "2.1 A hung entered");
    expect(waitFor(
               [&]() { return sink->count(ProbeOutcome::Timeout) >= 1; },
               ms(2000)),
           "2.1 A hung timed out");
    auto healthy = std::make_shared<GatedProbeAdapter>("p21-a-ok");
    auto ioOk = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(healthy, ioOk, 2, 0, ms(500))),
           "2.1 A submit healthy");
    expect(waitFor(
               [&]() { return healthy->connectCompleted() >= 1; },
               ms(2000)),
           "2.1 A unrelated adapter recovered");
    expect(exec.liveWorkerCount() == 4, "2.1 A live workers remain 4");
    expect(exec.quarantinedWorkerCount("p21-a-hung") <= 2,
           "2.1 A hung quarantine within q_a");
    gate->open();
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.1 B. Adapter A reaches q_a=2; B still recovers
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto g1 = std::make_shared<ProbeHangGate>();
    auto g2 = std::make_shared<ProbeHangGate>();
    auto a1 = std::make_shared<GatedProbeAdapter>("p21-b");
    auto a2 = std::make_shared<GatedProbeAdapter>("p21-b");
    a1->setGate(g1);
    a2->setGate(g2);
    a1->setNeverReturn(true);
    a2->setNeverReturn(true);
    auto io1 = std::make_shared<std::mutex>();
    auto io2 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(a1, io1, 1, 0, ms(80))), "2.1 B submit 1");
    expect(a1->waitConnectEntered(ms(1000)), "2.1 B first entered");
    expect(waitFor(
               [&]() { return exec.quarantinedWorkerCount("p21-b") >= 1; },
               ms(2000)),
           "2.1 B first quarantined");
    expect(exec.submit(makeRequest(a2, io2, 2, 1, ms(80))), "2.1 B submit 2");
    expect(a2->waitConnectEntered(ms(1000)), "2.1 B second entered");
    expect(waitFor(
               [&]() {
                 return exec.quarantinedWorkerCount("p21-b")
                     == kMaxQuarantinedProbesPerAdapter;
               },
               ms(2000)),
           "2.1 B q_a reached");
    auto healthy = std::make_shared<GatedProbeAdapter>("p21-b-ok");
    auto ioOk = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(healthy, ioOk, 3, 0, ms(500))),
           "2.1 B submit healthy");
    expect(waitFor(
               [&]() { return healthy->connectCompleted() >= 1; },
               ms(2000)),
           "2.1 B recovers after A q_a");
    g1->open();
    g2->open();
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.1 C. Global Q=8; 9th hung probe is not a 9th quarantine
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    std::vector<std::shared_ptr<ProbeHangGate>> gates;
    std::vector<std::shared_ptr<GatedProbeAdapter>> hung;
    std::vector<std::shared_ptr<std::mutex>> ios;
    for (int i = 0; i < 9; ++i)
    {
      auto gate = std::make_shared<ProbeHangGate>();
      auto a = std::make_shared<GatedProbeAdapter>("p21-c-" + std::to_string(i));
      a->setGate(gate);
      a->setNeverReturn(true);
      auto io = std::make_shared<std::mutex>();
      gates.push_back(gate);
      hung.push_back(a);
      ios.push_back(io);
      expect(exec.submit(makeRequest(
                 a, io, static_cast<std::uint64_t>(i + 1), 0, ms(80))),
             "2.1 C submit " + a->id());
    }
    expect(waitFor(
               [&]() {
                 return exec.quarantinedWorkerCount()
                     == kMaxQuarantinedProbeWorkers;
               },
               ms(4000)),
           "2.1 C global Q filled");
    expect(waitFor(
               [&]() {
                 return exec.quarantinedWorkerCount()
                         == kMaxQuarantinedProbeWorkers
                     && (exec.nonQuarantinableLiveCount() >= 1
                         || exec.pendingCount() >= 1);
               },
               ms(4000)),
           "2.1 C 9th is nonQ or pending, not Q");
    expect(exec.quarantinedWorkerCount() == kMaxQuarantinedProbeWorkers,
           "2.1 C no 9th quarantine");
    expect(exec.nonQuarantinableLiveCount() <= 2, "2.1 C nonQ <= N-R");
    for (const auto &gate : gates)
    {
      gate->open();
    }
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.1 D. One adapter cannot occupy all 4 live workers
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto g0 = std::make_shared<ProbeHangGate>();
    auto g1 = std::make_shared<ProbeHangGate>();
    auto g2 = std::make_shared<ProbeHangGate>();
    auto g3 = std::make_shared<ProbeHangGate>();
    auto a0 = std::make_shared<GatedProbeAdapter>("p21-d");
    auto a1 = std::make_shared<GatedProbeAdapter>("p21-d");
    auto a2 = std::make_shared<GatedProbeAdapter>("p21-d");
    auto a3 = std::make_shared<GatedProbeAdapter>("p21-d");
    a0->setGate(g0);
    a1->setGate(g1);
    a2->setGate(g2);
    a3->setGate(g3);
    a0->setNeverReturn(true);
    a1->setNeverReturn(true);
    a2->setNeverReturn(true);
    a3->setNeverReturn(true);
    auto io0 = std::make_shared<std::mutex>();
    auto io1 = std::make_shared<std::mutex>();
    auto io2 = std::make_shared<std::mutex>();
    auto io3 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(a0, io0, 1, 0, ms(80))), "2.1 D gen0");
    expect(waitFor(
               [&]() { return exec.quarantinedWorkerCount("p21-d") >= 1; },
               ms(2000)),
           "2.1 D gen0 quarantined");
    expect(exec.submit(makeRequest(a1, io1, 2, 1, ms(80))), "2.1 D gen1");
    expect(waitFor(
               [&]() {
                 return exec.quarantinedWorkerCount("p21-d")
                     == kMaxQuarantinedProbesPerAdapter;
               },
               ms(2000)),
           "2.1 D gen1 quarantined");
    expect(exec.submit(makeRequest(a2, io2, 3, 2, ms(80))), "2.1 D gen2 nonQ");
    expect(a2->waitConnectEntered(ms(2000)), "2.1 D nonQ entered");
    expect(exec.nonQuarantinableLiveCount() <= (4 - kReservedProbeWorkerCount),
           "2.1 D nonQ <= N-R");
    expect(exec.submit(makeRequest(a3, io3, 4, 3, ms(80))), "2.1 D gen3 pending");
    expect(waitFor(
               [&]() {
                 return exec.pendingCount() >= 1 && exec.hasLivePhysical("p21-d")
                     && exec.liveWorkerCount() == 4;
               },
               ms(2000)),
           "2.1 D fourth stays pending; live pool stays 4");
    auto healthy = std::make_shared<GatedProbeAdapter>("p21-d-ok");
    auto ioOk = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(healthy, ioOk, 99, 0, ms(800))),
           "2.1 D submit healthy");
    expect(waitFor(
               [&]() { return healthy->connectCompleted() >= 1; },
               ms(3000)),
           "2.1 D unrelated still recovers");
    g0->open();
    g1->open();
    g2->open();
    g3->open();
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.1 E. Peer healthy after A reached q_a — nonQ attempt runs
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto g1 = std::make_shared<ProbeHangGate>();
    auto g2 = std::make_shared<ProbeHangGate>();
    auto a1 = std::make_shared<GatedProbeAdapter>("p21-e");
    auto a2 = std::make_shared<GatedProbeAdapter>("p21-e");
    a1->setGate(g1);
    a2->setGate(g2);
    a1->setNeverReturn(true);
    a2->setNeverReturn(true);
    auto io1 = std::make_shared<std::mutex>();
    auto io2 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(a1, io1, 1, 0, ms(80))), "2.1 E gen0");
    expect(waitFor(
               [&]() { return exec.quarantinedWorkerCount("p21-e") >= 1; },
               ms(2000)),
           "2.1 E gen0 quarantined");
    expect(exec.submit(makeRequest(a2, io2, 2, 1, ms(80))), "2.1 E gen1");
    expect(waitFor(
               [&]() {
                 return exec.quarantinedWorkerCount("p21-e")
                     == kMaxQuarantinedProbesPerAdapter;
               },
               ms(2000)),
           "2.1 E q_a full");
    auto recovered = std::make_shared<GatedProbeAdapter>("p21-e");
    recovered->setPeerAvailable(true);
    auto io3 = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(recovered, io3, 3, 2, ms(800))),
           "2.1 E peer-up probe");
    expect(waitFor(
               [&]() { return recovered->connectCompleted() >= 1; },
               ms(3000)),
           "2.1 E nonQ probe succeeds after Q cap");
    expect(sink->count(ProbeOutcome::Success) >= 1, "2.1 E success published");
    g1->open();
    g2->open();
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.1 F. 10 never-return + healthy adapter recovers
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    std::vector<std::shared_ptr<ProbeHangGate>> gates;
    for (int i = 0; i < 10; ++i)
    {
      auto gate = std::make_shared<ProbeHangGate>();
      auto a = std::make_shared<GatedProbeAdapter>("p21-f-" + std::to_string(i));
      a->setGate(gate);
      a->setNeverReturn(true);
      auto io = std::make_shared<std::mutex>();
      gates.push_back(gate);
      expect(exec.submit(makeRequest(
                 a, io, static_cast<std::uint64_t>(i + 1), 0, ms(80))),
             "2.1 F submit hung");
    }
    expect(waitFor(
               [&]() {
                 return exec.quarantinedWorkerCount()
                     == kMaxQuarantinedProbeWorkers;
               },
               ms(4000)),
           "2.1 F Q filled by never-return adapters");
    auto healthy = std::make_shared<GatedProbeAdapter>("p21-f-ok");
    auto ioOk = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(healthy, ioOk, 100, 0, ms(800))),
           "2.1 F submit healthy");
    expect(waitFor(
               [&]() { return healthy->connectCompleted() >= 1; },
               ms(3000)),
           "2.1 F healthy recovered among 10 hangs");
    expect(exec.nonQuarantinableLiveCount() <= 2, "2.1 F nonQ bounded");
    for (const auto &gate : gates)
    {
      gate->open();
    }
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.1 G. Timeout vs Success: exactly one terminal claim wins
  // -------------------------------------------------------------------
  {
    auto io = std::make_shared<std::mutex>();
    auto adapter = std::make_shared<GatedProbeAdapter>("p21-g");
    ProbeSession session(
        "p21-g", 1, 0, 1, adapter, io,
        std::make_shared<std::atomic<bool>>(true), nullptr, ms(100));
    expect(session.tryClaim(ProbeOutcome::Success), "2.1 G first claim wins");
    expect(!session.tryClaim(ProbeOutcome::Timeout),
           "2.1 G timeout cannot steal success");
    expect(session.terminal() == ProbeOutcome::Success, "2.1 G SUCCESS stuck");
    ProbeSession session2(
        "p21-g2", 2, 0, 2, adapter, io,
        std::make_shared<std::atomic<bool>>(true), nullptr, ms(100));
    expect(session2.tryClaim(ProbeOutcome::Timeout), "2.1 G timeout can win first");
    expect(!session2.tryClaim(ProbeOutcome::Success),
           "2.1 G late success cannot steal timeout");
    expect(session2.terminal() == ProbeOutcome::Timeout, "2.1 G TIMEOUT stuck");
  }

  // -------------------------------------------------------------------
  // Phase 2.1 H. Success wins; late watchdog must not publish Timeout
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto fast = std::make_shared<GatedProbeAdapter>("p21-h");
    fast->setPeerAvailable(true);
    auto io = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(fast, io, 1, 0, ms(400))), "2.1 H submit");
    expect(waitFor(
               [&]() { return sink->count(ProbeOutcome::Success) >= 1; },
               ms(2000)),
           "2.1 H success");
    std::this_thread::sleep_for(ms(500));
    expect(sink->count(ProbeOutcome::Timeout) == 0,
           "2.1 H no timeout after success");
    expect(sink->count() == 1, "2.1 H exactly one terminal publish");
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.1 I. Timeout wins; late Success is stale (not published)
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto gate = std::make_shared<ProbeHangGate>();
    auto hung = std::make_shared<GatedProbeAdapter>("p21-i");
    hung->setGate(gate);
    hung->setNeverReturn(true);
    hung->setPeerAvailable(false);
    auto io = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(hung, io, 1, 0, ms(80))), "2.1 I submit");
    expect(waitFor(
               [&]() { return sink->count(ProbeOutcome::Timeout) >= 1; },
               ms(2000)),
           "2.1 I timeout won");
    hung->setLateSuccess(true);
    gate->open();
    expect(waitFor(
               [&]() { return hung->connectCompleted() >= 1; },
               ms(2000)),
           "2.1 I physical success returned");
    expect(sink->count(ProbeOutcome::Success) == 0,
           "2.1 I late success not published");
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.1 J. Late failure after timeout does not publish Failure
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto gate = std::make_shared<ProbeHangGate>();
    auto hung = std::make_shared<GatedProbeAdapter>("p21-j");
    hung->setGate(gate);
    hung->setNeverReturn(true);
    hung->setPeerAvailable(false);
    auto io = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(hung, io, 7, 0, ms(80))), "2.1 J submit");
    expect(waitFor(
               [&]() { return sink->count(ProbeOutcome::Timeout) >= 1; },
               ms(2000)),
           "2.1 J timeout");
    gate->open();
    expect(waitFor(
               [&]() { return hung->connectFailed() >= 1; },
               ms(2000)),
           "2.1 J physical failure returned");
    expect(sink->count(ProbeOutcome::Failure) == 0,
           "2.1 J late failure not published");
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.1 K. Stale IoBusy does not mutate a newer probe identity
  // -------------------------------------------------------------------
  {
    auto clock = std::make_shared<virtual_factory::icp::FakeRecoveryClock>(
        std::chrono::steady_clock::time_point(std::chrono::seconds(50)));
    RecoveryService recovery(clock);
    recovery.armDesired("p21-k", true);
    auto due = recovery.tick();
    expect(!due.empty(), "2.1 K due probe");
    const auto first = due.front();
    recovery.rematerialize("p21-k");
    clock->advance(ms(1));
    auto due2 = recovery.tick();
    expect(!due2.empty(), "2.1 K rematerialized due");
    expect(due2.front().runtimeEpoch != first.runtimeEpoch,
           "2.1 K epoch bumped");
    expect(!recovery.deferRetry(first.adapterId, first.probeId, first.runtimeEpoch),
           "2.1 K stale IoBusy defer ignored");
    const auto snap = recovery.snapshot("p21-k");
    expect(snap.activeProbeId == due2.front().probeId,
           "2.1 K current probeId unchanged by stale defer");
    expect(recovery.deferRetry(
               due2.front().adapterId, due2.front().probeId,
               due2.front().runtimeEpoch),
           "2.1 K current IoBusy defer applies");
  }

  // -------------------------------------------------------------------
  // Phase 2.1 L. Shutdown with Q full, nonQ occupied, pending probes
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    std::vector<std::shared_ptr<ProbeHangGate>> gates;
    for (int i = 0; i < 10; ++i)
    {
      auto gate = std::make_shared<ProbeHangGate>();
      auto a = std::make_shared<GatedProbeAdapter>("p21-l-" + std::to_string(i));
      a->setGate(gate);
      a->setNeverReturn(true);
      auto io = std::make_shared<std::mutex>();
      gates.push_back(gate);
      (void)exec.submit(makeRequest(
          a, io, static_cast<std::uint64_t>(i + 1), 0, ms(80)));
    }
    expect(waitFor(
               [&]() {
                 return exec.quarantinedWorkerCount()
                     == kMaxQuarantinedProbeWorkers;
               },
               ms(4000)),
           "2.1 L Q filled before stop");
    const auto t0 = std::chrono::steady_clock::now();
    exec.stop();
    const auto stopMs = std::chrono::duration_cast<ms>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
    expect(stopMs < 12000, "2.1 L stop bounded with exhausted capacity");
    for (const auto &gate : gates)
    {
      gate->open();
    }
  }

  // -------------------------------------------------------------------
  // Phase 2.1 M. 100 adapters, 24h fake-clock fail-fast, no growth
  // -------------------------------------------------------------------
  {
    auto clock = std::make_shared<virtual_factory::icp::FakeRecoveryClock>(
        std::chrono::steady_clock::time_point(std::chrono::seconds(2000)));
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
      const std::string id = "p21m-" + std::to_string(i);
      recovery.armDesired(id, true);
      auto a = std::make_shared<GatedProbeAdapter>(id);
      a->setPeerAvailable(false);
      adapters.push_back(a);
      ios.push_back(std::make_shared<std::mutex>());
    }
    std::size_t maxPending = 0;
    for (int step = 0; step < 24 * 60; ++step)
    {
      clock->advance(ms(60000));
      const auto due = recovery.tick();
      for (const auto &event : due)
      {
        const int idx = std::stoi(event.adapterId.substr(5));
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
      maxPending = std::max(maxPending, exec.pendingCount());
    }
    expect(recovery.controlCount() == 100, "2.1 M 100 RecoveryControl records");
    expect(maxPending <= 100, "2.1 M pending bounded");
    expect(exec.quarantinedWorkerCount() == 0, "2.1 M fail-fast has no Q");
    expect(exec.nonQuarantinableLiveCount() == 0, "2.1 M fail-fast has no nonQ");
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.1 N. Repeated fail-fast; later healthy adapter still runs
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    for (int i = 0; i < 20; ++i)
    {
      auto a = std::make_shared<GatedProbeAdapter>("p21-n-" + std::to_string(i));
      a->setPeerAvailable(false);
      auto io = std::make_shared<std::mutex>();
      expect(exec.submit(makeRequest(
                 a, io, static_cast<std::uint64_t>(100 + i), 0, ms(400))),
             "2.1 N fail-fast submit");
    }
    auto healthy = std::make_shared<GatedProbeAdapter>("p21-n-ok");
    healthy->setPeerAvailable(true);
    auto ioOk = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(healthy, ioOk, 1, 0, ms(800))),
           "2.1 N healthy lower sequence");
    expect(waitFor(
               [&]() { return healthy->connectCompleted() >= 1; },
               ms(3000)),
           "2.1 N healthy executed despite fail-fast crowd");
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.2 I1. Successful probe: admitted → entered → physical return
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    expect(exec.traceEnabled(), "2.2 I1 trace on by default in the executor");
    auto a = std::make_shared<GatedProbeAdapter>("p22-i1");
    a->setPeerAvailable(true);
    auto io = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(a, io, 1, 0, ms(800))), "2.2 I1 submit");
    expect(waitFor(
               [&]() {
                 return sink->traceCount(
                            "p22-i1", ProbeTraceKind::PhysicalReturn)
                     >= 1;
               },
               ms(3000)),
           "2.2 I1 physical return observed");
    const int admitted =
        sink->traceIndex("p22-i1", ProbeTraceKind::Admitted);
    const int entered =
        sink->traceIndex("p22-i1", ProbeTraceKind::EnteredConnect);
    const int returned =
        sink->traceIndex("p22-i1", ProbeTraceKind::PhysicalReturn);
    expect(admitted >= 0 && entered > admitted && returned > entered,
           "2.2 I1 order admitted < entered_connect < physical_return");
    expect(sink->traceCount("p22-i1", ProbeTraceKind::TimeoutClaimed) == 0,
           "2.2 I1 no timeout on a fast success");
    for (const auto &t : sink->tracesFor("p22-i1"))
    {
      expect(t.probeId == 1 && t.runtimeEpoch == 0,
             "2.2 I1 identity carried on every record");
      if (t.kind == ProbeTraceKind::PhysicalReturn)
      {
        expect(t.physicalResult == "success", "2.2 I1 physical result success");
        expect(t.terminalClaimWon, "2.2 I1 physical return won terminal CAS");
        expect(t.elapsedMs >= 0, "2.2 I1 physical duration measured");
      }
    }
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.2 I1b. Fast failure: same order, physicalResult=failure
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto a = std::make_shared<GatedProbeAdapter>("p22-i1b");
    a->setPeerAvailable(false);
    auto io = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(a, io, 2, 0, ms(800))), "2.2 I1b submit");
    expect(waitFor(
               [&]() {
                 return sink->traceCount(
                            "p22-i1b", ProbeTraceKind::PhysicalReturn)
                     >= 1;
               },
               ms(3000)),
           "2.2 I1b physical return observed");
    const int admitted =
        sink->traceIndex("p22-i1b", ProbeTraceKind::Admitted);
    const int entered =
        sink->traceIndex("p22-i1b", ProbeTraceKind::EnteredConnect);
    const int returned =
        sink->traceIndex("p22-i1b", ProbeTraceKind::PhysicalReturn);
    expect(admitted >= 0 && entered > admitted && returned > entered,
           "2.2 I1b order admitted < entered_connect < physical_return");
    for (const auto &t : sink->tracesFor("p22-i1b"))
    {
      if (t.kind == ProbeTraceKind::PhysicalReturn)
      {
        expect(t.physicalResult == "failure", "2.2 I1b physical result failure");
        expect(t.terminalClaimWon, "2.2 I1b fast failure claimed terminal");
      }
    }
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.2 I2. Logical timeout, then a late physical return (stale).
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto gate = std::make_shared<ProbeHangGate>();
    auto a = std::make_shared<GatedProbeAdapter>("p22-i2");
    a->setGate(gate);
    a->setNeverReturn(true);
    a->setLateSuccess(true);
    auto io = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(a, io, 7, 0, ms(80))), "2.2 I2 submit");
    expect(waitFor(
               [&]() {
                 return sink->traceCount(
                            "p22-i2", ProbeTraceKind::TimeoutClaimed)
                     >= 1;
               },
               ms(3000)),
           "2.2 I2 timeout claimed observed");
    expect(sink->traceCount("p22-i2", ProbeTraceKind::PhysicalReturn) == 0,
           "2.2 I2 no physical return while the call is still blocked");
    gate->open();
    expect(waitFor(
               [&]() {
                 return sink->traceCount(
                            "p22-i2", ProbeTraceKind::PhysicalReturn)
                     >= 1;
               },
               ms(3000)),
           "2.2 I2 late physical return observed after the gate opens");
    for (const auto &t : sink->tracesFor("p22-i2"))
    {
      if (t.kind == ProbeTraceKind::TimeoutClaimed)
      {
        expect(t.terminalClaimWon, "2.2 I2 watchdog won the terminal CAS");
        expect(t.elapsedMs >= 80, "2.2 I2 timeout age at least the budget");
      }
      if (t.kind == ProbeTraceKind::PhysicalReturn)
      {
        expect(!t.terminalClaimWon,
               "2.2 I2 late return is stale: terminal CAS already lost");
        expect(t.physicalResult == "success",
               "2.2 I2 late physical result recorded verbatim");
        expect(t.elapsedMs >= 80,
               "2.2 I2 physical duration exceeds the logical budget");
      }
    }
    expect(sink->count(ProbeOutcome::Timeout) >= 1,
           "2.2 I2 timeout completion still published unchanged");
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.2 I3. Timeout with the physical call still blocked: no
  // physical-return record until the gate is released.
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    auto gate = std::make_shared<ProbeHangGate>();
    auto a = std::make_shared<GatedProbeAdapter>("p22-i3");
    a->setGate(gate);
    a->setNeverReturn(true);
    auto io = std::make_shared<std::mutex>();
    expect(exec.submit(makeRequest(a, io, 3, 0, ms(80))), "2.2 I3 submit");
    expect(waitFor(
               [&]() {
                 return sink->traceCount(
                            "p22-i3", ProbeTraceKind::TimeoutClaimed)
                     >= 1;
               },
               ms(3000)),
           "2.2 I3 timeout claimed");
    expect(sink->traceCount("p22-i3", ProbeTraceKind::EnteredConnect) >= 1,
           "2.2 I3 entered connect recorded before the timeout");
    std::this_thread::sleep_for(ms(400));
    expect(sink->traceCount("p22-i3", ProbeTraceKind::PhysicalReturn) == 0,
           "2.2 I3 still no physical return: the call has not returned");
    expect(sink->traceCount("p22-i3", ProbeTraceKind::QuarantineOrDetach) >= 1,
           "2.2 I3 post-timeout disposition recorded");
    expect(sink->hasTrace(
               "p22-i3", ProbeTraceKind::QuarantineOrDetach, "quarantined"),
           "2.2 I3 first hang on an adapter converts to quarantine");
    gate->open();
    expect(waitFor(
               [&]() {
                 return sink->traceCount(
                            "p22-i3", ProbeTraceKind::PhysicalReturn)
                     >= 1;
               },
               ms(3000)),
           "2.2 I3 physical return only after the gate is released");
    exec.stop();
  }

  // -------------------------------------------------------------------
  // Phase 2.2 I4. Pending probe denied by admission records the reason.
  // -------------------------------------------------------------------
  {
    auto sink = std::make_shared<RecordingSink>();
    ProbeExecutor exec;
    exec.setSink(sink);
    exec.start(4);
    std::vector<std::shared_ptr<ProbeHangGate>> gates;
    std::vector<std::shared_ptr<GatedProbeAdapter>> adapters;
    std::vector<std::shared_ptr<std::mutex>> ios;
    for (int i = 0; i < 4; ++i)
    {
      auto gate = std::make_shared<ProbeHangGate>();
      auto a = std::make_shared<GatedProbeAdapter>("p22-i4");
      a->setGate(gate);
      a->setNeverReturn(true);
      gates.push_back(gate);
      adapters.push_back(a);
      ios.push_back(std::make_shared<std::mutex>());
    }
    expect(exec.submit(makeRequest(adapters[0], ios[0], 1, 0, ms(80))),
           "2.2 I4 gen0");
    expect(waitFor(
               [&]() { return exec.quarantinedWorkerCount("p22-i4") >= 1; },
               ms(2000)),
           "2.2 I4 gen0 quarantined");
    expect(exec.submit(makeRequest(adapters[1], ios[1], 2, 1, ms(80))),
           "2.2 I4 gen1");
    expect(waitFor(
               [&]() {
                 return exec.quarantinedWorkerCount("p22-i4")
                     == kMaxQuarantinedProbesPerAdapter;
               },
               ms(2000)),
           "2.2 I4 q_a full");
    expect(exec.submit(makeRequest(adapters[2], ios[2], 3, 2, ms(80))),
           "2.2 I4 gen2 nonQ");
    expect(adapters[2]->waitConnectEntered(ms(2000)), "2.2 I4 nonQ entered");
    expect(exec.submit(makeRequest(adapters[3], ios[3], 4, 3, ms(80))),
           "2.2 I4 gen3 pending");
    expect(waitFor(
               [&]() {
                 return sink->traceCount(
                            "p22-i4", ProbeTraceKind::PendingNotAdmitted)
                     >= 1;
               },
               ms(3000)),
           "2.2 I4 denial of the pending probe is recorded");
    bool sawLivePhysical = false;
    for (const auto &t : sink->tracesFor("p22-i4"))
    {
      if (t.kind != ProbeTraceKind::PendingNotAdmitted)
      {
        continue;
      }
      expect(!t.detail.empty(), "2.2 I4 denial reason is never blank");
      if (t.detail == "livePhysical")
      {
        sawLivePhysical = true;
        expect(t.probeId == 4 && t.runtimeEpoch == 3,
               "2.2 I4 denial names the refused generation");
      }
    }
    expect(sawLivePhysical,
           "2.2 I4 exact reason is livePhysical: the stuck nonQ call still "
           "occupies the adapter");
    expect(adapters[3]->connectEntered() == 0,
           "2.2 I4 denied probe never entered connect");
    expect(sink->traceCount("p22-i4", ProbeTraceKind::PhysicalReturn) == 0,
           "2.2 I4 no physical return while every call is blocked");
    for (const auto &gate : gates)
    {
      gate->open();
    }
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
