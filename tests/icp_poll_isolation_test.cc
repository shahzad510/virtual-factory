/// P3 regression: poll isolation (protocol-neutral dedicated PollExecutor).
///
/// Hung poll on adapter A must not block B polling, ControlPlaneTick, HTTP,
/// commands, lifecycle, configuration, or bounded shutdown.

#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/icp/LifecycleExecutor.hh>
#include <virtual_factory/icp/LiveStateCache.hh>
#include <virtual_factory/icp/PollExecutor.hh>
#include <virtual_factory/icp/PollScheduler.hh>
#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/icp/app/HttpApiServer.hh>
#include <virtual_factory/icp/config/ConfigurationModel.hh>
#include <virtual_factory/equipment/GenericEquipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>
#include <virtual_factory/industrial/MockIndustrialAdapter.hh>

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
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

std::string tempPath(const char *suffix)
{
  std::ostringstream stream;
  stream << "/tmp/icp-poll-" << ::getpid() << "-" << suffix;
  return stream.str();
}

struct HangGate
{
  std::mutex mu;
  std::condition_variable cv;
  bool release{false};

  void wait()
  {
    std::unique_lock<std::mutex> lock(this->mu);
    this->cv.wait(lock, [this]() { return this->release; });
  }

  void open()
  {
    std::lock_guard<std::mutex> lock(this->mu);
    this->release = true;
    this->cv.notify_all();
  }
};

/// Protocol-blind test adapter with optional hung poll() and counters.
class ProbeAdapter : public virtual_factory::IndustrialAdapter
{
public:
  class Eq : public virtual_factory::Equipment
  {
  public:
    explicit Eq(std::string id) : inner_(std::move(id), "probe")
    {
      this->inner_.addCapability("start");
      this->inner_.setTelemetry("speed", 1.0, "rpm");
    }
    std::string id() const override { return this->inner_.id(); }
    std::string type() const override { return this->inner_.type(); }
    virtual_factory::OperationalState operationalState() const override
    {
      return this->inner_.operationalState();
    }
    bool fault() const override { return this->inner_.fault(); }
    std::vector<std::string> capabilities() const override
    {
      return this->inner_.capabilities();
    }
    std::vector<std::string> commands() const override
    {
      return {"start", "block"};
    }
    virtual_factory::CommandResult execute(
        const std::string &command, double parameter) override
    {
      if (this->onEnter_)
      {
        this->onEnter_("execute");
      }
      if (command == "block" && this->gate_)
      {
        this->gate_->wait();
        if (this->onLeave_)
        {
          this->onLeave_("execute");
        }
        return {true, "blocked"};
      }
      auto result = this->inner_.execute(command, parameter);
      if (this->onLeave_)
      {
        this->onLeave_("execute");
      }
      return result;
    }
    std::vector<virtual_factory::TelemetryPoint> telemetry() const override
    {
      return this->inner_.telemetry();
    }
    virtual_factory::EquipmentStatus status() const override
    {
      return this->inner_.status();
    }
    void bumpSpeed()
    {
      const auto points = this->inner_.telemetry();
      double value = 1.0;
      for (const auto &p : points)
      {
        if (p.name == "speed")
        {
          value = p.value + 1.0;
        }
      }
      this->inner_.setTelemetry("speed", value, "rpm");
    }
    void setGate(std::shared_ptr<HangGate> gate) { this->gate_ = std::move(gate); }
    void setHooks(
        std::function<void(const char *)> enter,
        std::function<void(const char *)> leave)
    {
      this->onEnter_ = std::move(enter);
      this->onLeave_ = std::move(leave);
    }

  private:
    virtual_factory::GenericEquipment inner_;
    std::shared_ptr<HangGate> gate_;
    std::function<void(const char *)> onEnter_;
    std::function<void(const char *)> onLeave_;
  };

  ProbeAdapter(
      std::string id,
      std::string equipmentId,
      std::shared_ptr<HangGate> pollGate = nullptr)
      : id_(std::move(id))
      , pollGate_(std::move(pollGate))
      , equipment_(std::make_unique<Eq>(std::move(equipmentId)))
  {
    this->equipment_->setHooks(
        [this](const char *op) { this->enterIo(op); },
        [this](const char *op) { this->leaveIo(op); });
  }

  std::string id() const override { return this->id_; }
  std::string protocol() const override { return "probe"; }
  virtual_factory::ConnectionState connectionState() const override
  {
    return this->state_;
  }
  std::string lastError() const override { return this->lastError_; }
  bool connect() override
  {
    this->enterIo("connect");
    this->state_ = virtual_factory::ConnectionState::Connected;
    this->lastError_.clear();
    this->leaveIo("connect");
    return true;
  }
  void disconnect() override
  {
    this->enterIo("disconnect");
    this->state_ = virtual_factory::ConnectionState::Disconnected;
    this->leaveIo("disconnect");
  }
  void poll() override
  {
    this->enterIo("poll");
    ++this->pollEntered_;
    {
      std::lock_guard<std::mutex> lock(this->pollMu_);
      this->pollEnteredFlag_ = true;
    }
    this->pollEnteredCv_.notify_all();

    if (this->pollGate_)
    {
      this->pollGate_->wait();
    }
    if (this->state_ == virtual_factory::ConnectionState::Connected)
    {
      this->equipment_->bumpSpeed();
      ++this->pollCompleted_;
    }
    this->leaveIo("poll");
  }
  std::vector<virtual_factory::Equipment *> equipment() override
  {
    return {this->equipment_.get()};
  }
  virtual_factory::Equipment *equipmentById(const std::string &id) override
  {
    return this->equipment_->id() == id ? this->equipment_.get() : nullptr;
  }

  bool waitPollEntered(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(this->pollMu_);
    return this->pollEnteredCv_.wait_for(lock, timeout, [this]() {
      return this->pollEnteredFlag_;
    });
  }

  int pollEntered() const { return this->pollEntered_.load(); }
  int pollCompleted() const { return this->pollCompleted_.load(); }
  int concurrentViolations() const { return this->concurrent_.load(); }

private:
  void enterIo(const char *)
  {
    if (this->inIo_.exchange(true))
    {
      ++this->concurrent_;
    }
  }
  void leaveIo(const char *) { this->inIo_.store(false); }

  std::string id_;
  std::shared_ptr<HangGate> pollGate_;
  std::unique_ptr<Eq> equipment_;
  virtual_factory::ConnectionState state_{
      virtual_factory::ConnectionState::Disconnected};
  std::string lastError_;
  std::atomic<int> pollEntered_{0};
  std::atomic<int> pollCompleted_{0};
  std::atomic<int> concurrent_{0};
  std::atomic<bool> inIo_{false};
  std::mutex pollMu_;
  std::condition_variable pollEnteredCv_;
  bool pollEnteredFlag_{false};
};

virtual_factory::icp::AdapterConfigRecord makeMockRecord(
    const std::string &id, const std::string &eqId)
{
  virtual_factory::icp::AdapterConfigRecord mock;
  mock.adapterId = id;
  mock.protocol = "mock";
  mock.enabled = true;
  virtual_factory::icp::EquipmentMappingRecord eq;
  eq.equipmentId = eqId;
  eq.type = "mock";
  virtual_factory::icp::TelemetryMappingRecord tel;
  tel.name = "speed";
  tel.unit = "rpm";
  eq.telemetry.push_back(tel);
  virtual_factory::icp::CommandMappingRecord cmd;
  cmd.command = "start";
  eq.commands.push_back(cmd);
  mock.equipment.push_back(eq);
  return mock;
}

bool waitFor(
    const std::function<bool()> &pred, std::chrono::milliseconds timeout)
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

}  // namespace

int main()
{
  using virtual_factory::icp::AdapterManager;
  using virtual_factory::icp::ApplicationService;
  using virtual_factory::icp::HttpApiServer;
  using virtual_factory::icp::LiveStateCache;
  using virtual_factory::icp::PollExecutor;
  using virtual_factory::icp::PollScheduler;
  using virtual_factory::icp::kPollShutdownGrace;

  constexpr auto kStopSlack = std::chrono::milliseconds(1500);
  // stop() may spend one grace on PollExecutor and one on disconnectAllBounded
  // when abandoned polls still hold io_mutex.
  const auto kStopBound = kPollShutdownGrace + kPollShutdownGrace + kStopSlack;

  // -----------------------------------------------------------------------
  // 1-2) Hung A does not stop B polling; B telemetry advances
  // -----------------------------------------------------------------------
  {
    auto manager = std::make_shared<AdapterManager>();
    auto cache = std::make_shared<LiveStateCache>();
    auto executor = std::make_shared<PollExecutor>(manager, cache);
    executor->start(4);

    auto gate = std::make_shared<HangGate>();
    auto hung = std::make_unique<ProbeAdapter>("hung-a", "EQ-HUNG", gate);
    ProbeAdapter *rawHung = hung.get();
    auto live = std::make_unique<ProbeAdapter>("live-b", "EQ-LIVE");
    ProbeAdapter *rawLive = live.get();

    expect(manager->addAdapter(std::move(hung)).ok, "add hung-a");
    expect(manager->addAdapter(std::move(live)).ok, "add live-b");
    expect(manager->connectAdapter("hung-a").ok, "connect hung");
    expect(manager->connectAdapter("live-b").ok, "connect live");

    expect(executor->enqueue("hung-a"), "enqueue hung");
    expect(rawHung->waitPollEntered(std::chrono::milliseconds(1000)),
           "hung poll entered");

    const int liveBefore = rawLive->pollCompleted();
    expect(executor->enqueue("live-b"), "enqueue live while hung");
    expect(waitFor([&]() { return rawLive->pollCompleted() > liveBefore; },
                   std::chrono::milliseconds(2000)),
           "live-b poll completed while A hung");

    // Coalesce: many enqueues while hung should not stack unbounded work.
    for (int i = 0; i < 20; ++i)
    {
      expect(executor->enqueue("hung-a"), "coalesce enqueue");
    }

    gate->open();
    expect(executor->waitUntilIdle(std::chrono::milliseconds(2000)),
           "executor idle after release");
    expect(rawHung->pollCompleted() >= 1, "hung eventually completed");
    expect(rawHung->pollCompleted() <= 3,
           "hung polls coalesced (not 20+) got="
               + std::to_string(rawHung->pollCompleted()));

    executor->stop();
    manager->disconnectAll();
  }

  // -----------------------------------------------------------------------
  // 9) Same-adapter poll vs command never concurrent
  // -----------------------------------------------------------------------
  {
    AdapterManager manager;
    LiveStateCache cache;
    auto probe = std::make_unique<ProbeAdapter>("probe-a", "EQ-PROBE");
    ProbeAdapter *raw = probe.get();
    expect(manager.addAdapter(std::move(probe)).ok, "add probe");
    expect(manager.connectAdapter("probe-a").ok, "connect probe");

    PollScheduler scheduler(manager, cache, std::chrono::milliseconds(20));
    scheduler.start();
    expect(waitFor([&]() { return raw->pollCompleted() >= 1; },
                   std::chrono::milliseconds(1000)),
           "poll ran");

    auto cmd = manager.executeEquipmentCommand("EQ-PROBE", "start", 0.0);
    expect(cmd.equipmentFound && cmd.command.accepted, "command ok");
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    scheduler.stop();
    expect(raw->concurrentViolations() == 0,
           "no concurrent poll/execute");
    manager.removeAdapter("probe-a");
  }

  // -----------------------------------------------------------------------
  // 10) Same-adapter poll vs lifecycle serialized (disconnect under poll)
  // -----------------------------------------------------------------------
  {
    auto manager = std::make_shared<AdapterManager>();
    auto cache = std::make_shared<LiveStateCache>();
    auto executor = std::make_shared<PollExecutor>(manager, cache);
    executor->start(2);

    auto gate = std::make_shared<HangGate>();
    auto hung = std::make_unique<ProbeAdapter>("ser-a", "EQ-SER", gate);
    ProbeAdapter *raw = hung.get();
    expect(manager->addAdapter(std::move(hung)).ok, "add ser");
    expect(manager->connectAdapter("ser-a").ok, "connect ser");
    expect(executor->enqueue("ser-a"), "enqueue ser poll");
    expect(raw->waitPollEntered(std::chrono::milliseconds(1000)),
           "ser poll entered");

    std::atomic<bool> disconnectDone{false};
    std::thread life([&]() {
      expect(manager->disconnectAdapter("ser-a").ok, "disconnect under poll");
      disconnectDone.store(true);
    });

    expect(!waitFor([&]() { return disconnectDone.load(); },
                    std::chrono::milliseconds(200)),
           "disconnect waits on io_mutex held by poll");
    gate->open();
    life.join();
    expect(disconnectDone.load(), "disconnect finished after poll");
    expect(raw->concurrentViolations() == 0, "no concurrent poll/disconnect");
    executor->stop();
  }

  // -----------------------------------------------------------------------
  // 11-12) Extract during poll + rematerialize: stale cache gate
  // -----------------------------------------------------------------------
  {
    auto manager = std::make_shared<AdapterManager>();
    auto cache = std::make_shared<LiveStateCache>();
    auto executor = std::make_shared<PollExecutor>(manager, cache);
    executor->start(2);

    auto gate = std::make_shared<HangGate>();
    auto oldAd = std::make_unique<ProbeAdapter>("same-id", "EQ-SAME", gate);
    ProbeAdapter *rawOld = oldAd.get();
    expect(manager->addAdapter(std::move(oldAd)).ok, "add old");
    expect(manager->connectAdapter("same-id").ok, "connect old");
    expect(executor->enqueue("same-id"), "enqueue old poll");
    expect(rawOld->waitPollEntered(std::chrono::milliseconds(1000)),
           "old poll entered");

    auto extracted = manager->extractAdapter("same-id");
    expect(extracted.adapter != nullptr, "extracted");
    cache->removeAdapterEquipment("same-id");
    expect(!cache->equipmentById("EQ-SAME").has_value(), "cache cleared");

    // Old poll finishes while unenrolled — must not republish.
    gate->open();
    expect(waitFor([&]() { return rawOld->pollCompleted() >= 1; },
                   std::chrono::milliseconds(2000)),
           "old poll finished");
    expect(executor->waitUntilIdle(std::chrono::milliseconds(1000)),
           "executor idle after old poll");
    expect(!cache->equipmentById("EQ-SAME").has_value(),
           "unenrolled poll did not publish cache");

    auto neu = std::make_unique<ProbeAdapter>("same-id", "EQ-SAME");
    ProbeAdapter *rawNew = neu.get();
    expect(manager->addAdapter(std::move(neu)).ok, "rematerialize");
    expect(manager->connectAdapter("same-id").ok, "connect new");
    expect(executor->enqueue("same-id"), "enqueue new poll");
    expect(waitFor([&]() { return rawNew->pollCompleted() >= 1; },
                   std::chrono::milliseconds(2000)),
           "new poll completed");
    auto snapAfter = cache->equipmentById("EQ-SAME");
    expect(snapAfter.has_value(), "new instance published cache");
    expect(snapAfter && !snapAfter->stale, "new cache fresh");

    if (extracted.io_mutex)
    {
      std::lock_guard<std::mutex> io(*extracted.io_mutex);
      extracted.adapter->disconnect();
    }
    executor->stop();
    manager->disconnectAll();
  }

  // -----------------------------------------------------------------------
  // 3-8, 14-16) ApplicationService + HTTP + ControlPlaneTick + shutdown
  // -----------------------------------------------------------------------
  {
    const std::string cfg = tempPath("cfg.json");
    const std::string hist = tempPath("hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();

    expect(service.upsertAdapterConfig(makeMockRecord("mock-svc", "EQ-MOCK-SVC")).ok,
           "upsert mock");
    expect(service.saveConfiguration().ok, "save");
    expect(service.loadConfiguration().ok, "load");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("mock-svc");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(3000)),
           "mock CONNECTED before hung peer");

    auto gate = std::make_shared<HangGate>();
    auto hung = std::make_unique<ProbeAdapter>("hung-svc", "EQ-HUNG-SVC", gate);
    ProbeAdapter *rawHung = hung.get();
    expect(service.manager().addAdapter(std::move(hung)).ok, "add hung svc");
    expect(service.manager().connectAdapter("hung-svc").ok, "connect hung svc");

    auto live = std::make_unique<ProbeAdapter>("live-svc", "EQ-LIVE-SVC");
    ProbeAdapter *rawLive = live.get();
    expect(service.manager().addAdapter(std::move(live)).ok, "add live svc");
    expect(service.manager().connectAdapter("live-svc").ok, "connect live svc");

    expect(waitFor([&]() { return rawHung->pollEntered() >= 1; },
                   std::chrono::milliseconds(2000)),
           "service scheduler dispatched hung poll");

    const int liveBefore = rawLive->pollCompleted();
    expect(waitFor([&]() { return rawLive->pollCompleted() > liveBefore; },
                   std::chrono::milliseconds(2000)),
           "live-svc poll completes while hung-svc blocked");

    // Cache publication for live probe while A hung.
    expect(waitFor(
               [&]() {
                 auto snap = service.equipmentById("EQ-LIVE-SVC");
                 return snap.has_value() && !snap->stale;
               },
               std::chrono::milliseconds(2000)),
           "live equipment cached while A hung");

    const int port = 19400 + (::getpid() % 500);
    HttpApiServer api(service, "", "127.0.0.1", port);
    expect(api.start(), "http start");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(1, 0);

    const auto tHttp0 = std::chrono::steady_clock::now();
    auto st = client.Get("/api/v1/status");
    const auto httpMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - tHttp0)
                            .count();
    expect(st && st->status == 200, "GET status while hung");
    expect(httpMs < 500, "status responsive (" + std::to_string(httpMs) + "ms)");

    auto ad = client.Get("/api/v1/adapters");
    expect(ad && ad->status == 200, "GET adapters");
    auto eq = client.Get("/api/v1/equipment");
    expect(eq && eq->status == 200, "GET equipment");

    const auto tCmd0 = std::chrono::steady_clock::now();
    auto cmd = service.executeEquipmentCommand("EQ-MOCK-SVC", "start", 0.0);
    const auto cmdMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - tCmd0)
                           .count();
    expect(cmd.ok, "mock command while hung: " + cmd.message);
    expect(cmdMs < 500, "command not blocked by hung poll ("
                            + std::to_string(cmdMs) + "ms)");

    const auto tLife0 = std::chrono::steady_clock::now();
    auto disc = service.disconnectAdapter("mock-svc");
    expect(disc.ok || disc.accepted, "disconnect mock accepted");
    auto conn = service.connectAdapter("mock-svc");
    expect(conn.ok || conn.accepted, "connect mock accepted");
    const auto lifeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - tLife0)
                            .count();
    expect(lifeMs < 1000, "lifecycle on B not blocked ("
                              + std::to_string(lifeMs) + "ms)");

    expect(service.upsertAdapterConfig(makeMockRecord("mock-cfg", "EQ-CFG")).ok,
           "config upsert while hung");

    auto diag = service.diagnosticsReport();
    expect(diag.system.schedulerRunning,
           "ControlPlaneTick keeps scheduler marked running");

    auto gate2 = std::make_shared<HangGate>();
    auto hung2 =
        std::make_unique<ProbeAdapter>("hung-svc-2", "EQ-HUNG-2", gate2);
    ProbeAdapter *rawHung2 = hung2.get();
    expect(service.manager().addAdapter(std::move(hung2)).ok, "add hung2");
    expect(service.manager().connectAdapter("hung-svc-2").ok, "connect hung2");
    expect(waitFor([&]() { return rawHung2->pollEntered() >= 1; },
                   std::chrono::milliseconds(2000)),
           "second hung poll entered");

    api.stop();
    const auto tStop0 = std::chrono::steady_clock::now();
    service.stop();
    const auto stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - tStop0)
                            .count();
    expect(stopMs < kStopBound.count(),
           "shutdown bounded with hung polls (" + std::to_string(stopMs)
               + "ms)");

    const auto tStop1 = std::chrono::steady_clock::now();
    service.stop();
    const auto stop2Ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - tStop1)
                             .count();
    expect(stop2Ms < 1000, "repeated stop idempotent ("
                               + std::to_string(stop2Ms) + "ms)");

    gate->open();
    gate2->open();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // -----------------------------------------------------------------------
  // RecoveryConnect / connect accepted for B while A hung (ControlPlaneTick)
  // -----------------------------------------------------------------------
  {
    const std::string cfg = tempPath("rec-cfg.json");
    const std::string hist = tempPath("rec-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();

    expect(service.upsertAdapterConfig(makeMockRecord("mock-rec", "EQ-MOCK-REC")).ok,
           "upsert mock-rec");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "save/load rec");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("mock-rec");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(3000)),
           "mock-rec CONNECTED");

    auto gate = std::make_shared<HangGate>();
    auto hung = std::make_unique<ProbeAdapter>("hung-rec", "EQ-HUNG-REC", gate);
    ProbeAdapter *rawHung = hung.get();
    expect(service.manager().addAdapter(std::move(hung)).ok, "add hung rec");
    expect(service.manager().connectAdapter("hung-rec").ok, "connect hung rec");
    expect(waitFor([&]() { return rawHung->pollEntered() >= 1; },
                   std::chrono::milliseconds(2000)),
           "hung-rec poll entered");

    (void)service.disconnectAdapter("mock-rec");
    // ControlPlaneTick should keep running and accept reconnect while A hung.
    auto rec = service.connectAdapter("mock-rec");
    expect(rec.ok || rec.accepted,
           "connect/recovery path accepted while A hung: " + rec.message);

    const auto t0 = std::chrono::steady_clock::now();
    service.stop();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    expect(ms < kStopBound.count(),
           "rec shutdown bounded");
    gate->open();
  }

  if (failures == 0)
  {
    std::cout << "icp_poll_isolation_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_poll_isolation_test: " << failures << " failure(s)"
            << std::endl;
  return 1;
}
