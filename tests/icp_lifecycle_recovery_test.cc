/// Lifecycle recovery: operator Connect/Reconnect follow-up vs automatic backoff.
///
/// Operator requests issued while a connect-style job is in-flight must not
/// start a parallel connect(), but a failed in-flight attempt must get one
/// prompt follow-up connect() without waiting for automatic backoff.
/// Rematerialize must not inherit a pending operator follow-up from the
/// previous runtime instance. A generation bump that drops a pending
/// RecoveryConnect must reconcile the automatic-recovery latch so later
/// RecoveryConnect can run. Hung poll() isolation rematerializes a new
/// runtime after 2s so RecoveryConnect can proceed without cancelling the
/// old poll. An obstructed automatic RecoveryConnect that holds io_mutex past
/// the adapter timeoutMs plus slack is isolated the same way (replacement
/// runtime; old connect() is not cancelled). A timeout-bound RecoveryConnect
/// that returns {ok=false, ioBusy=false} must stay on the same runtime and
/// become FAULTED via applyConnectOutcome. A first automatic RecoveryConnect
/// that remains in-flight while still DISCONNECTED past timeoutMs+slack is
/// isolated the same way so automatic recovery can continue without HTTP
/// Connect. Operator Connect/Reconnect is not isolated merely because it is
/// slow.

#include "opcua_test_server.hh"

#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/icp/app/HttpApiServer.hh>
#include <virtual_factory/icp/LifecycleExecutor.hh>
#include <virtual_factory/equipment/GenericEquipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include <httplib.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
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
  stream << "/tmp/icp-life-rec-" << ::getpid() << "-" << suffix;
  return stream.str();
}

std::uint16_t reserveLoopbackPort()
{
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    return 0;
  }
  int yes = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
  {
    ::close(fd);
    return 0;
  }
  socklen_t len = sizeof(addr);
  if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
  {
    ::close(fd);
    return 0;
  }
  const std::uint16_t port = ntohs(addr.sin_port);
  ::close(fd);
  return port;
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

/// Captures peer availability at connect() entry, then waits on a gate so tests
/// can issue operator Connect/Reconnect while the first attempt is in-flight.
class GatedConnectAdapter : public virtual_factory::IndustrialAdapter
{
public:
  static constexpr double kStaleMarker = 999.0;

  GatedConnectAdapter(std::string id, std::shared_ptr<HangGate> gate)
      : id_(std::move(id)), gate_(std::move(gate)),
        equipment_("EQ-" + id_, "motor")
  {
    this->equipment_.setTelemetry("stale_marker", 0.0, "");
  }

  std::string id() const override { return this->id_; }
  std::string protocol() const override { return "mock"; }
  virtual_factory::ConnectionState connectionState() const override
  {
    return this->state_;
  }
  std::string lastError() const override { return this->lastError_; }

  void setPeerAvailable(bool available) { this->peerAvailable_.store(available); }
  /// When true, the first connect() (Disconnected) fails immediately if the
  /// peer is down; only a retry from FAULTED waits on the gate. Matches
  /// production: first RecoveryConnect returns, second hangs in connect().
  void setHangOnlyWhenFaulted(bool enabled)
  {
    this->hangOnlyWhenFaulted_.store(enabled);
  }
  void setStaleMarker(double value)
  {
    this->equipment_.setTelemetry("stale_marker", value, "");
  }
  void setPublishError(std::string error)
  {
    this->publishError_ = std::move(error);
  }

  bool connect() override
  {
    const bool peerUp = this->peerAvailable_.load();
    ++this->connectEntered_;
    {
      std::lock_guard<std::mutex> lock(this->mu_);
      this->entered_ = true;
    }
    this->enteredCv_.notify_all();
    const bool shouldHang = this->gate_
        && (!this->hangOnlyWhenFaulted_.load()
            || this->state_ == virtual_factory::ConnectionState::Faulted);
    if (shouldHang)
    {
      this->gate_->wait();
    }
    if (!peerUp)
    {
      this->state_ = virtual_factory::ConnectionState::Faulted;
      this->lastError_ =
          this->publishError_.empty() ? "peer unavailable" : this->publishError_;
      ++this->connectFailed_;
      return false;
    }
    this->state_ = virtual_factory::ConnectionState::Connected;
    this->lastError_.clear();
    ++this->connectCompleted_;
    return true;
  }

  void disconnect() override
  {
    ++this->disconnectCount_;
    this->state_ = virtual_factory::ConnectionState::Disconnected;
    this->lastError_.clear();
  }

  void poll() override {}

  std::vector<virtual_factory::Equipment *> equipment() override
  {
    return {&this->equipment_};
  }
  virtual_factory::Equipment *equipmentById(const std::string &eqId) override
  {
    return eqId == this->equipment_.id() ? &this->equipment_ : nullptr;
  }

  bool waitConnectEntered(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(this->mu_);
    return this->enteredCv_.wait_for(lock, timeout, [this]() {
      return this->entered_;
    });
  }

  bool waitConnectEnteredCount(int n, std::chrono::milliseconds timeout)
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
      if (this->connectEntered_.load() >= n)
      {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return this->connectEntered_.load() >= n;
  }

  int connectEntered() const { return this->connectEntered_.load(); }
  int connectCompleted() const { return this->connectCompleted_.load(); }
  int connectFailed() const { return this->connectFailed_.load(); }
  int disconnectCount() const { return this->disconnectCount_.load(); }

private:
  std::string id_;
  std::shared_ptr<HangGate> gate_;
  virtual_factory::GenericEquipment equipment_;
  virtual_factory::ConnectionState state_{
      virtual_factory::ConnectionState::Disconnected};
  std::string lastError_;
  std::string publishError_;
  std::atomic<bool> peerAvailable_{false};
  std::atomic<bool> hangOnlyWhenFaulted_{false};
  std::atomic<int> connectEntered_{0};
  std::atomic<int> connectCompleted_{0};
  std::atomic<int> connectFailed_{0};
  std::atomic<int> disconnectCount_{0};
  mutable std::mutex mu_;
  std::condition_variable enteredCv_;
  bool entered_{false};
};

/// poll() marks FAULTED then blocks so RecoveryConnect cannot take io_mutex.
class GatedPollAdapter : public virtual_factory::IndustrialAdapter
{
public:
  static constexpr double kStaleMarker = 999.0;

  GatedPollAdapter(std::string id, std::string equipmentId,
                   std::shared_ptr<HangGate> gate)
      : id_(std::move(id)), gate_(std::move(gate)),
        equipment_(std::move(equipmentId), "motor")
  {
    this->equipment_.setTelemetry("stale_marker", 1.0, "");
  }

  std::string id() const override { return this->id_; }
  std::string protocol() const override { return "mock"; }
  virtual_factory::ConnectionState connectionState() const override
  {
    return this->state_;
  }
  std::string lastError() const override { return this->lastError_; }

  bool connect() override
  {
    ++this->connectEntered_;
    this->state_ = virtual_factory::ConnectionState::Connected;
    this->lastError_.clear();
    ++this->connectCompleted_;
    return true;
  }

  void disconnect() override
  {
    ++this->disconnectCount_;
    this->state_ = virtual_factory::ConnectionState::Disconnected;
    this->lastError_.clear();
  }

  void poll() override
  {
    ++this->pollEntered_;
    {
      std::lock_guard<std::mutex> lock(this->mu_);
      this->pollSeen_ = true;
    }
    this->pollCv_.notify_all();
    this->state_ = virtual_factory::ConnectionState::Faulted;
    this->lastError_ = "hung poll";
    if (this->gate_)
    {
      this->gate_->wait();
    }
    this->equipment_.setTelemetry("stale_marker", kStaleMarker, "");
    ++this->pollReturned_;
  }

  std::vector<virtual_factory::Equipment *> equipment() override
  {
    return {&this->equipment_};
  }
  virtual_factory::Equipment *equipmentById(const std::string &eqId) override
  {
    return eqId == this->equipment_.id() ? &this->equipment_ : nullptr;
  }

  bool waitPollEntered(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(this->mu_);
    return this->pollCv_.wait_for(lock, timeout, [this]() {
      return this->pollSeen_;
    });
  }

  int connectEntered() const { return this->connectEntered_.load(); }
  int pollEntered() const { return this->pollEntered_.load(); }
  int pollReturned() const { return this->pollReturned_.load(); }

private:
  std::string id_;
  std::shared_ptr<HangGate> gate_;
  virtual_factory::GenericEquipment equipment_;
  virtual_factory::ConnectionState state_{
      virtual_factory::ConnectionState::Disconnected};
  std::string lastError_;
  std::atomic<int> connectEntered_{0};
  std::atomic<int> connectCompleted_{0};
  std::atomic<int> disconnectCount_{0};
  std::atomic<int> pollEntered_{0};
  std::atomic<int> pollReturned_{0};
  mutable std::mutex mu_;
  std::condition_variable pollCv_;
  bool pollSeen_{false};
};

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

bool lifecycleIdle(
    virtual_factory::icp::ApplicationService &service, const std::string &id)
{
  auto &life = service.lifecycle();
  return !life.hasInFlightOrPending(id, virtual_factory::icp::LifecycleOp::Connect)
      && !life.hasInFlightOrPending(
             id, virtual_factory::icp::LifecycleOp::RecoveryConnect)
      && !life.hasInFlightOrPending(id, virtual_factory::icp::LifecycleOp::Reconnect)
      && !life.hasInFlightOrPending(
             id, virtual_factory::icp::LifecycleOp::Disconnect)
      && !service.probes().hasLogicalProbe(id)
      && !service.probes().hasPhysicalFor(id);
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

virtual_factory::icp::AdapterConfigRecord makeMock(const std::string &id)
{
  virtual_factory::icp::AdapterConfigRecord mock;
  mock.adapterId = id;
  mock.protocol = "mock";
  mock.enabled = true;
  virtual_factory::icp::EquipmentMappingRecord eq;
  eq.equipmentId = "EQ-" + id;
  eq.type = "motor";
  mock.equipment.push_back(eq);
  return mock;
}

virtual_factory::icp::AdapterConfigRecord makeOpcUa(
    const std::string &id, const std::string &endpoint, int timeoutMs)
{
  virtual_factory::icp::AdapterConfigRecord opcua;
  opcua.adapterId = id;
  opcua.protocol = "opcua";
  opcua.enabled = true;
  opcua.connection.endpointUrl = endpoint;
  opcua.connection.timeoutMs = timeoutMs;
  virtual_factory::icp::EquipmentMappingRecord eq;
  eq.equipmentId = virtual_factory::test::OpcUaTestServer::kMixerId;
  eq.type = "mixer";
  virtual_factory::icp::TelemetryMappingRecord tel;
  tel.name = "speed";
  tel.unit = "rpm";
  tel.namespaceIndex = 1;
  tel.address = std::string("ns=1;s=")
      + virtual_factory::test::OpcUaTestServer::kMixerSpeedActual;
  eq.telemetry.push_back(tel);
  opcua.equipment.push_back(eq);
  return opcua;
}

GatedConnectAdapter *addGated(
    virtual_factory::icp::ApplicationService &service,
    const std::string &id,
    std::shared_ptr<HangGate> gate)
{
  auto hang = std::make_unique<GatedConnectAdapter>(id, std::move(gate));
  GatedConnectAdapter *raw = hang.get();
  expect(service.manager().addAdapter(std::move(hang)).ok, "add gated runtime " + id);
  return raw;
}

GatedPollAdapter *addGatedPoll(
    virtual_factory::icp::ApplicationService &service,
    const std::string &id,
    std::shared_ptr<HangGate> gate)
{
  auto hang = std::make_unique<GatedPollAdapter>(id, "EQ-" + id, std::move(gate));
  GatedPollAdapter *raw = hang.get();
  expect(service.manager().addAdapter(std::move(hang)).ok,
         "add gated-poll runtime " + id);
  return raw;
}

bool markerInCache(virtual_factory::icp::ApplicationService &service,
                   const std::string &equipmentId, double value)
{
  auto snap = service.cache().equipmentById(equipmentId);
  if (!snap)
  {
    return false;
  }
  for (const auto &tel : snap->telemetry)
  {
    if (tel.name == "stale_marker" && tel.value == value)
    {
      return true;
    }
  }
  return false;
}

int isolationEventCount(
    virtual_factory::icp::ApplicationService &service, const std::string &id)
{
  int n = 0;
  for (const auto &ev : service.events(100))
  {
    if (ev.adapterId == id && ev.eventType == "recovery_runtime_isolated")
    {
      ++n;
    }
  }
  return n;
}

bool statusResponsive(virtual_factory::icp::ApplicationService &service)
{
  const auto t0 = std::chrono::steady_clock::now();
  const auto st = service.status();
  const auto report = service.diagnosticsReport();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
  return ms < 500 && st.product == "ICP" && report.icp.serviceRunning;
}

int currentConnectStyleCount(
    virtual_factory::icp::ApplicationService &service, const std::string &id)
{
  auto &life = service.lifecycle();
  int n = 0;
  if (life.hasInFlightOrPending(id, virtual_factory::icp::LifecycleOp::Connect))
  {
    ++n;
  }
  if (life.hasInFlightOrPending(
          id, virtual_factory::icp::LifecycleOp::RecoveryConnect))
  {
    ++n;
  }
  if (life.hasInFlightOrPending(id, virtual_factory::icp::LifecycleOp::Reconnect))
  {
    ++n;
  }
  if (service.probes().hasLogicalProbe(id))
  {
    ++n;
  }
  return n;
}

struct IsolationPause
{
  std::mutex mu;
  std::condition_variable cv;
  bool arrived{false};
  bool release{false};

  void arrivedAndWait()
  {
    {
      std::lock_guard<std::mutex> lock(this->mu);
      this->arrived = true;
      this->cv.notify_all();
    }
    std::unique_lock<std::mutex> lock(this->mu);
    this->cv.wait(lock, [this]() { return this->release; });
  }

  bool waitArrived(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(this->mu);
    return this->cv.wait_for(lock, timeout, [this]() { return this->arrived; });
  }

  void go()
  {
    std::lock_guard<std::mutex> lock(this->mu);
    this->release = true;
    this->cv.notify_all();
  }
};

std::unique_ptr<GatedConnectAdapter> makeHanging(
    const std::string &id, std::shared_ptr<HangGate> gate)
{
  auto hang = std::make_unique<GatedConnectAdapter>(id, std::move(gate));
  hang->setPeerAvailable(false);
  return hang;
}

}  // namespace

int main()
{
  using virtual_factory::icp::ApplicationService;
  using virtual_factory::icp::LifecycleOp;

  // ---------------------------------------------------------------------
  // A) Operator Connect during in-flight connect-style job; first fails;
  //    follow-up connect() runs and succeeds.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("a-cfg.json");
    const std::string hist = tempPath("a-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeMock("hang-a")).ok, "A upsert");

    auto gate = std::make_shared<HangGate>();
    GatedConnectAdapter *raw = addGated(service, "hang-a", gate);
    raw->setPeerAvailable(false);

    auto started = service.connectAdapter("hang-a");
    expect(started.ok && started.accepted, "A Connect accepted to start in-flight");
    expect(raw->waitConnectEntered(std::chrono::milliseconds(2000)),
           "A first connect() entered (in-flight)");
    expect(raw->connectEntered() == 1, "A single in-flight connect() before operator");

    auto op = service.connectAdapter("hang-a");
    expect(op.ok && op.accepted, "A operator Connect accepted while in-flight");
    expect(raw->connectEntered() == 1,
           "A operator Connect did not start a parallel connect()");

    raw->setPeerAvailable(true);
    gate->open();

    expect(raw->waitConnectEnteredCount(2, std::chrono::milliseconds(2000)),
           "A follow-up connect() entered after original failure");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-a");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(2000)),
           "A adapter CONNECTED after follow-up connect()");
    expect(raw->connectFailed() >= 1, "A original attempt failed");
    expect(raw->connectCompleted() == 1, "A follow-up connect() succeeded");
    expect(raw->connectEntered() == 2, "A exactly one follow-up connect()");
    const auto sess = sessionOf(service, "hang-a");
    expect(sess.successfulConnections >= 1, "A successfulConnections incremented");
    expect(sess.failedConnections >= 1, "A failedConnections counts original fail");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // B) Operator Reconnect during in-flight connect; original fails; follow-up
  //    connect() runs and succeeds.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("b-cfg.json");
    const std::string hist = tempPath("b-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeMock("hang-b")).ok, "B upsert");

    auto gate = std::make_shared<HangGate>();
    GatedConnectAdapter *raw = addGated(service, "hang-b", gate);
    raw->setPeerAvailable(false);

    auto started = service.connectAdapter("hang-b");
    expect(started.ok && started.accepted, "B Connect accepted");
    expect(raw->waitConnectEntered(std::chrono::milliseconds(2000)),
           "B connect() entered");

    auto recon = service.reconnectAdapter("hang-b");
    expect(recon.ok && recon.accepted, "B Reconnect accepted during in-flight");
    expect(raw->connectEntered() == 1,
           "B Reconnect did not start a parallel connect()");
    expect(raw->disconnectCount() == 0,
           "B Reconnect did not disconnect the in-flight attempt");

    raw->setPeerAvailable(true);
    gate->open();

    expect(raw->waitConnectEnteredCount(2, std::chrono::milliseconds(2000)),
           "B follow-up connect() entered");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-b");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(2000)),
           "B CONNECTED after Reconnect follow-up");
    expect(raw->connectFailed() >= 1, "B original attempt failed");
    expect(raw->connectCompleted() == 1, "B follow-up succeeded");
    expect(raw->connectEntered() == 2, "B one follow-up connect()");
    const auto sess = sessionOf(service, "hang-b");
    expect(sess.successfulConnections >= 1, "B successfulConnections incremented");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // C) Repeated Connect/Reconnect coalesce to one follow-up connect().
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("c-cfg.json");
    const std::string hist = tempPath("c-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeMock("hang-c")).ok, "C upsert");

    auto gate = std::make_shared<HangGate>();
    GatedConnectAdapter *raw = addGated(service, "hang-c", gate);
    raw->setPeerAvailable(false);

    auto started = service.connectAdapter("hang-c");
    expect(started.ok && started.accepted, "C start Connect");
    expect(raw->waitConnectEntered(std::chrono::milliseconds(2000)),
           "C connect() entered");

    for (int i = 0; i < 5; ++i)
    {
      auto c = service.connectAdapter("hang-c");
      expect(c.ok && c.accepted, "C coalesced Connect accepted");
      auto r = service.reconnectAdapter("hang-c");
      expect(r.ok && r.accepted, "C coalesced Reconnect accepted");
    }
    expect(raw->connectEntered() == 1, "C no parallel connect() during requests");

    raw->setPeerAvailable(true);
    gate->open();

    expect(raw->waitConnectEnteredCount(2, std::chrono::milliseconds(2000)),
           "C one follow-up connect() started");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-c");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(2000)),
           "C CONNECTED");
    expect(raw->connectEntered() == 2, "C repeated requests produced one follow-up");
    expect(raw->connectCompleted() == 1, "C single successful connect()");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // D) Automatic recovery backoff while peer stays down (not every 250ms).
  // ---------------------------------------------------------------------
  {
    const std::uint16_t port = reserveLoopbackPort();
    expect(port != 0, "D reserve port");
    const std::string endpoint =
        "opc.tcp://127.0.0.1:" + std::to_string(port);
    const std::string cfg = tempPath("d-cfg.json");
    const std::string hist = tempPath("d-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());

    ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeOpcUa("opcua-d", endpoint, 500)).ok,
           "D upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "D save/load arms recovery");

    expect(waitFor(
               [&]() { return sessionOf(service, "opcua-d").failedConnections >= 1; },
               std::chrono::milliseconds(4000)),
           "D first RecoveryConnect failed");
    expect(waitFor(
               [&]() { return lifecycleIdle(service, "opcua-d"); },
               std::chrono::milliseconds(2000)),
           "D lifecycle idle after first failure");

    const auto afterFirst = std::chrono::steady_clock::now();
    const auto fail1 = sessionOf(service, "opcua-d").failedConnections;
    const auto rec1 = sessionOf(service, "opcua-d").reconnectCount;

    expect(waitFor(
               [&]() {
                 return sessionOf(service, "opcua-d").reconnectCount > rec1
                     || sessionOf(service, "opcua-d").failedConnections > fail1;
               },
               std::chrono::milliseconds(8000)),
           "D second automatic RecoveryConnect scheduled");
    const auto gap1 = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - afterFirst)
                          .count();
    expect(gap1 >= 1500, "D backoff >= ~2s (not 250ms poll loop)");
    expect(gap1 < 7000, "D first backoff is bounded");

    expect(waitFor(
               [&]() { return lifecycleIdle(service, "opcua-d"); },
               std::chrono::milliseconds(3000)),
           "D idle after second failure");
    const auto afterSecond = std::chrono::steady_clock::now();
    const auto rec2 = sessionOf(service, "opcua-d").reconnectCount;
    expect(waitFor(
               [&]() { return sessionOf(service, "opcua-d").reconnectCount > rec2; },
               std::chrono::milliseconds(10000)),
           "D third automatic RecoveryConnect scheduled");
    const auto gap2 = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - afterSecond)
                          .count();
    expect(gap2 >= 3000, "D exponential backoff grew to ~4s");

    const auto view = service.adapter("opcua-d");
    expect(view && view->connectionState != "CONNECTED",
           "D remains not CONNECTED while peer down");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // E) Automatic recovery after idle failure: peer ON, no HTTP Connect.
  // ---------------------------------------------------------------------
  {
    const std::uint16_t port = reserveLoopbackPort();
    expect(port != 0, "E reserve port");
    const std::string endpoint =
        "opc.tcp://127.0.0.1:" + std::to_string(port);
    const std::string cfg = tempPath("e-cfg.json");
    const std::string hist = tempPath("e-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());

    ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeOpcUa("opcua-e", endpoint, 500)).ok,
           "E upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "E save/load");

    expect(waitFor(
               [&]() {
                 auto v = service.adapter("opcua-e");
                 return v
                     && (v->connectionState == "FAULTED"
                         || v->connectionState == "DISCONNECTED")
                     && sessionOf(service, "opcua-e").failedConnections >= 1;
               },
               std::chrono::milliseconds(4000)),
           "E failed connection observed");
    expect(waitFor(
               [&]() { return lifecycleIdle(service, "opcua-e"); },
               std::chrono::milliseconds(2000)),
           "E lifecycle idle after failed RecoveryConnect");

    virtual_factory::test::OpcUaTestServer server;
    server.setPort(port);
    expect(server.start(), "E OPC UA server starts after idle failure");

    expect(waitFor(
               [&]() {
                 auto v = service.adapter("opcua-e");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(12000)),
           "E automatic RecoveryConnect reaches CONNECTED without HTTP Connect");
    expect(sessionOf(service, "opcua-e").successfulConnections >= 1,
           "E successfulConnections incremented by automatic recovery");

    service.stop();
    server.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // F) Genuine Disconnect still cancels in-flight Connect.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("f-cfg.json");
    const std::string hist = tempPath("f-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeMock("hang-f")).ok, "F upsert");

    auto gate = std::make_shared<HangGate>();
    GatedConnectAdapter *raw = addGated(service, "hang-f", gate);
    raw->setPeerAvailable(true);

    auto started = service.connectAdapter("hang-f");
    expect(started.ok && started.accepted, "F Connect accepted");
    expect(raw->waitConnectEntered(std::chrono::milliseconds(2000)),
           "F connect() entered");

    auto disc = service.disconnectAdapter("hang-f");
    expect(disc.ok && disc.accepted, "F Disconnect accepted");
    gate->open();
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-f");
                 return v && v->connectionState == "DISCONNECTED";
               },
               std::chrono::milliseconds(2000)),
           "F settles DISCONNECTED");

    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(300))
    {
      auto v = service.adapter("hang-f");
      expect(!(v && v->connectionState == "CONNECTED"),
             "F in-flight success must not stay CONNECTED after Disconnect");
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    expect(raw->disconnectCount() >= 1, "F disconnect ran after cancelled connect");
    expect(raw->connectEntered() == 1,
           "F Disconnect cancelled intent; no operator follow-up connect()");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // G) Rematerialize must drop pending operator follow-up for the old runtime.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("g-cfg.json");
    const std::string hist = tempPath("g-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    auto rec = makeMock("hang-g");
    rec.description = "original";
    expect(service.upsertAdapterConfig(rec).ok, "G upsert");

    auto gate = std::make_shared<HangGate>();
    GatedConnectAdapter *raw = addGated(service, "hang-g", gate);
    raw->setPeerAvailable(false);

    auto started = service.connectAdapter("hang-g");
    expect(started.ok && started.accepted, "G Connect accepted");
    expect(raw->waitConnectEntered(std::chrono::milliseconds(2000)),
           "G first connect() entered");
    expect(service.lifecycle().hasInFlightOrPending("hang-g", LifecycleOp::Connect)
               || service.lifecycle().hasInFlightOrPending(
                      "hang-g", LifecycleOp::RecoveryConnect),
           "G connect-style job in-flight");

    auto pending = service.connectAdapter("hang-g");
    expect(pending.ok && pending.accepted,
           "G operator Connect records pending follow-up");
    expect(raw->connectEntered() == 1, "G pending request did not start connect()");

    rec.description = "rematerialized";
    auto replaced = service.upsertAdapterConfig(rec);
    expect(replaced.ok, "G rematerialize upsert ok");
    virtual_factory::IndustrialAdapter *replacement =
        service.manager().adapter("hang-g");
    expect(replacement != nullptr && replacement != raw,
           "G rematerialize installed a new runtime instance");

    gate->open();
    expect(waitFor(
               [&]() { return raw->connectFailed() >= 1; },
               std::chrono::milliseconds(2000)),
           "G old in-flight connect() completed as failure");
    expect(raw->connectEntered() == 1,
           "G old pending follow-up did not connect() the extracted runtime");
    expect(raw->connectCompleted() == 0, "G extracted runtime did not become Connected");

    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-g");
                 return lifecycleIdle(service, "hang-g") && v
                     && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(3000)),
           "G replacement CONNECTED after rematerialize (new runtime, not extracted)");

    bool eqOwned = false;
    for (const auto &snap : service.equipment())
    {
      if (snap.equipmentId == "EQ-hang-g")
      {
        expect(snap.adapterId == "hang-g",
               "G equipment EQ-hang-g owned by hang-g after rematerialize");
        eqOwned = true;
      }
    }
    expect(eqOwned, "G equipment present and owned by hang-g after rematerialize");

    auto disc = service.disconnectAdapter("hang-g");
    expect(disc.ok, "G disconnect replacement before fresh Connect");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-g");
                 return lifecycleIdle(service, "hang-g") && v
                     && v->connectionState == "DISCONNECTED";
               },
               std::chrono::milliseconds(2000)),
           "G replacement idle DISCONNECTED; old pending follow-up did not reconnect");

    auto fresh = service.connectAdapter("hang-g");
    expect(fresh.ok && fresh.accepted, "G fresh Connect accepted on new runtime");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-g");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(2000)),
           "G new adapter CONNECTED via fresh explicit Connect");
    expect(raw->connectEntered() == 1,
           "G rematerialize follow-up never reclaimed the old adapter");
    eqOwned = false;
    for (const auto &snap : service.equipment())
    {
      if (snap.equipmentId == "EQ-hang-g")
      {
        expect(snap.adapterId == "hang-g",
               "G equipment EQ-hang-g owned by hang-g after fresh Connect");
        eqOwned = true;
      }
    }
    expect(eqOwned, "G equipment present and owned by replacement adapter");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // H) Occupied LifecycleExecutor workers must not block automatic recovery.
  //    ProbeExecutor owns automatic connect(); bumpGeneration on lifecycle
  //    does not drop the logical probe.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h-cfg.json");
    const std::string hist = tempPath("h-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();

    expect(service.upsertAdapterConfig(makeMock("hang-h")).ok, "H upsert target");
    auto targetGate = std::make_shared<HangGate>();
    targetGate->open();
    GatedConnectAdapter *target = addGated(service, "hang-h", targetGate);
    target->setPeerAvailable(false);

    auto armed = service.connectAdapter("hang-h");
    expect(armed.ok && armed.accepted, "H setup Connect arms autoConnectDesired");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-h");
                 return v
                     && (v->connectionState == "FAULTED"
                         || v->connectionState == "DISCONNECTED")
                     && sessionOf(service, "hang-h").failedConnections >= 1;
               },
               std::chrono::milliseconds(4000)),
           "H first Connect failed");
    expect(waitFor(
               [&]() { return lifecycleIdle(service, "hang-h"); },
               std::chrono::milliseconds(2000)),
           "H target idle after first failure");
    const auto attemptsBefore = sessionOf(service, "hang-h").connectionAttempts;
    const int enteredBefore = target->connectEntered();

    struct Blocker
    {
      std::string id;
      std::shared_ptr<HangGate> gate;
      GatedConnectAdapter *raw{nullptr};
    };
    std::vector<Blocker> blockers;
    for (int i = 0; i < 4; ++i)
    {
      Blocker b;
      b.id = "hang-h-block-" + std::to_string(i);
      expect(service.upsertAdapterConfig(makeMock(b.id)).ok, "H upsert blocker");
      b.gate = std::make_shared<HangGate>();
      b.raw = addGated(service, b.id, b.gate);
      b.raw->setPeerAvailable(false);
      auto started = service.connectAdapter(b.id);
      expect(started.ok && started.accepted, "H blocker Connect accepted");
      expect(b.raw->waitConnectEntered(std::chrono::milliseconds(2000)),
             "H blocker connect() entered (lifecycle worker occupied)");
      blockers.push_back(std::move(b));
    }

    const auto recBefore = sessionOf(service, "hang-h").reconnectCount;
    expect(waitFor(
               [&]() {
                 return sessionOf(service, "hang-h").reconnectCount > recBefore
                     || service.probes().hasLogicalProbe("hang-h")
                     || target->connectEntered() > enteredBefore;
               },
               std::chrono::milliseconds(8000)),
           "H automatic probe proceeds despite occupied lifecycle workers");
    expect(!service.lifecycle().hasInFlightOrPending(
               "hang-h", LifecycleOp::RecoveryConnect),
           "H automatic recovery does not consume RecoveryConnect");

    (void)service.lifecycle().bumpGeneration("hang-h");

    target->setPeerAvailable(true);
    for (auto &b : blockers)
    {
      b.gate->open();
    }

    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-h");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(12000)),
           "H automatic probe reaches CONNECTED while lifecycle was occupied");
    expect(sessionOf(service, "hang-h").successfulConnections >= 1,
           "H successfulConnections incremented by automatic recovery");
    expect(sessionOf(service, "hang-h").connectionAttempts == attemptsBefore,
           "H no HTTP Connect/Reconnect after lifecycle occupancy");
    expect(target->connectCompleted() >= 1, "H target connect() succeeded");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  auto armHungPoll = [&](ApplicationService &service, const std::string &id,
                         std::shared_ptr<HangGate> gate) -> GatedPollAdapter * {
    expect(service.upsertAdapterConfig(makeMock(id)).ok, "upsert " + id);
    GatedPollAdapter *raw = addGatedPoll(service, id, std::move(gate));
    auto started = service.connectAdapter(id);
    expect(started.ok && started.accepted, "Connect accepted " + id);
    expect(waitFor(
               [&]() {
                 auto v = service.adapter(id);
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(2000)),
           "CONNECTED before hung poll " + id);
    expect(raw->waitPollEntered(std::chrono::milliseconds(2000)),
           "poll() entered " + id);
    expect(waitFor(
               [&]() {
                 auto v = service.adapter(id);
                 return v && v->connectionState == "FAULTED";
               },
               std::chrono::milliseconds(2000)),
           "FAULTED while poll hung " + id);
    return raw;
  };

  auto waitIsolated = [&](ApplicationService &service, const std::string &id,
                          GatedPollAdapter *oldRaw) {
    expect(waitFor(
               [&]() {
                 virtual_factory::IndustrialAdapter *runtime =
                     service.manager().adapter(id);
                 auto v = service.adapter(id);
                 return runtime != nullptr && runtime != oldRaw && v
                     && v->connectionState == "CONNECTED"
                     && oldRaw->pollReturned() == 0;
               },
               std::chrono::milliseconds(8000)),
           "isolated replacement CONNECTED while old poll blocked " + id);
    expect(service.manager().adapterCount() == 1,
           "one enrolled runtime after isolation " + id);
    expect(oldRaw->connectEntered() == 1,
           "old session connect() not retried " + id);
    expect(oldRaw->pollReturned() == 0, "old poll still blocked " + id);
  };

  // ---------------------------------------------------------------------
  // I) Hung poll → FAULTED → recovery blocked → 2s isolation → new connect.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("i-cfg.json");
    const std::string hist = tempPath("i-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    auto gate = std::make_shared<HangGate>();
    GatedPollAdapter *raw = armHungPoll(service, "hang-i", gate);
    waitIsolated(service, "hang-i", raw);
    expect(raw->pollReturned() == 0, "I old poll remains blocked after new CONNECTED");
    service.stop();
    gate->open();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // J) Old poll later returns → no stale cache/state/telemetry publication.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("j-cfg.json");
    const std::string hist = tempPath("j-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    auto gate = std::make_shared<HangGate>();
    GatedPollAdapter *raw = armHungPoll(service, "hang-j", gate);
    waitIsolated(service, "hang-j", raw);
    virtual_factory::IndustrialAdapter *replacement =
        service.manager().adapter("hang-j");
    expect(!markerInCache(service, "EQ-hang-j", GatedPollAdapter::kStaleMarker),
           "J cache has no stale marker before old poll returns");
    gate->open();
    expect(waitFor(
               [&]() { return raw->pollReturned() >= 1; },
               std::chrono::milliseconds(2000)),
           "J old poll returned");
    expect(!markerInCache(service, "EQ-hang-j", GatedPollAdapter::kStaleMarker),
           "J old poll did not publish stale_marker=999");
    expect(service.manager().adapter("hang-j") == replacement,
           "J replacement runtime unchanged after stale poll return");
    auto v = service.adapter("hang-j");
    expect(v && v->connectionState == "CONNECTED",
           "J replacement stays CONNECTED after old poll returns");
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // K) Repeated recovery ticks → only one enrolled runtime for adapter ID.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("k-cfg.json");
    const std::string hist = tempPath("k-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    auto gate = std::make_shared<HangGate>();
    GatedPollAdapter *raw = armHungPoll(service, "hang-k", gate);
    waitIsolated(service, "hang-k", raw);
    virtual_factory::IndustrialAdapter *first =
        service.manager().adapter("hang-k");
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1500))
    {
      expect(service.manager().adapterCount() == 1,
             "K single enrolled runtime across recovery ticks");
      expect(service.manager().adapter("hang-k") == first,
             "K same replacement across recovery ticks");
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    service.stop();
    gate->open();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // L) Operator Connect/Reconnect during isolation → intent preserved,
  //    no parallel old-session connect.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("l-cfg.json");
    const std::string hist = tempPath("l-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    auto gate = std::make_shared<HangGate>();
    GatedPollAdapter *raw = armHungPoll(service, "hang-l", gate);
    auto opConnect = service.connectAdapter("hang-l");
    expect(opConnect.ok && opConnect.accepted,
           "L Connect accepted during hung poll");
    expect(raw->connectEntered() == 1,
           "L Connect did not start parallel old-session connect()");
    waitIsolated(service, "hang-l", raw);
    auto opRecon = service.reconnectAdapter("hang-l");
    expect(opRecon.ok && opRecon.accepted,
           "L Reconnect accepted after isolation");
    expect(raw->connectEntered() == 1,
           "L Reconnect did not connect() the extracted runtime");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-l");
                 return v && v->connectionState == "CONNECTED"
                     && raw->pollReturned() == 0;
               },
               std::chrono::milliseconds(4000)),
           "L replacement CONNECTED; old poll still blocked");
    service.stop();
    gate->open();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // M) Disconnect/disable during isolation → automatic recovery cancelled.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("m-cfg.json");
    const std::string hist = tempPath("m-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    auto gate = std::make_shared<HangGate>();
    GatedPollAdapter *raw = armHungPoll(service, "hang-m", gate);
    waitIsolated(service, "hang-m", raw);
    auto disc = service.disconnectAdapter("hang-m");
    expect(disc.ok && disc.accepted, "M Disconnect accepted during isolation");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-m");
                 return v && v->connectionState == "DISCONNECTED"
                     && !sessionOf(service, "hang-m").autoConnectDesired;
               },
               std::chrono::milliseconds(3000)),
           "M Disconnect sticky; autoConnectDesired cleared");
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(800))
    {
      auto v = service.adapter("hang-m");
      expect(!(v && v->connectionState == "CONNECTED"),
             "M no automatic reconnect after Disconnect");
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    expect(raw->connectEntered() == 1, "M old session not reconnect()'d");
    service.stop();
    gate->open();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }
  {
    const std::string cfg = tempPath("m2-cfg.json");
    const std::string hist = tempPath("m2-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    auto gate = std::make_shared<HangGate>();
    GatedPollAdapter *raw = armHungPoll(service, "hang-m2", gate);
    waitIsolated(service, "hang-m2", raw);
    auto rec = makeMock("hang-m2");
    rec.enabled = false;
    expect(service.upsertAdapterConfig(rec).ok, "M disable upsert");
    expect(waitFor(
               [&]() {
                 return service.manager().adapter("hang-m2") == nullptr
                     && !sessionOf(service, "hang-m2").autoConnectDesired;
               },
               std::chrono::milliseconds(3000)),
           "M disable extracts runtime and clears autoConnectDesired");
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(800))
    {
      expect(service.manager().adapter("hang-m2") == nullptr,
             "M disable does not rematerialize for automatic recovery");
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    expect(raw->connectEntered() == 1, "M disable did not connect old session");
    service.stop();
    gate->open();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // N) Hung RecoveryConnect → peer up while connect() blocked → isolation
  //    → NEW CONNECTED; OLD connect() stays blocked (production gap vs E).
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("n-cfg.json");
    const std::string hist = tempPath("n-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-n");
    hangCfg.connection.timeoutMs = 500;
    expect(service.upsertAdapterConfig(hangCfg).ok, "N upsert");
    expect(service.saveConfiguration().ok, "N save");
    // Materialize before start so autoConnectDesired is armed without HTTP
    // Connect, then swap in the gated runtime before RecoveryConnect runs.
    expect(service.loadConfiguration().ok, "N load arms autoConnectDesired");
    {
      auto extracted = service.manager().extractAdapter("hang-n");
      (void)extracted;
    }
    auto gate = std::make_shared<HangGate>();
    auto hang = std::make_unique<GatedConnectAdapter>("hang-n", gate);
    hang->setHangOnlyWhenFaulted(true);
    hang->setPeerAvailable(false);
    GatedConnectAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "N add gated runtime");
    service.start();

    expect(waitFor(
               [&]() {
                 return sessionOf(service, "hang-n").failedConnections >= 1
                     && sessionOf(service, "hang-n").connectionAttempts == 0
                     && raw->connectFailed() >= 1;
               },
               std::chrono::milliseconds(4000)),
           "N first RecoveryConnect failed without HTTP Connect");
    expect(sessionOf(service, "hang-n").autoConnectDesired,
           "N autoConnectDesired armed by materialize");
    expect(waitFor(
               [&]() { return raw->connectEntered() >= 2; },
               std::chrono::milliseconds(8000)),
           "N second RecoveryConnect entered connect() and is blocked");
    expect(raw->connectCompleted() == 0, "N OLD connect() still blocked");
    expect(sessionOf(service, "hang-n").connectionAttempts == 0,
           "N connectionAttempts stay 0 (no operator Connect)");
    expect(sessionOf(service, "hang-n").failedConnections == 1,
           "N failedConnections counts only the completed first attempt");
    expect(sessionOf(service, "hang-n").reconnectCount >= 2,
           "N reconnectCount shows RecoveryConnect was scheduled twice");

    const virtual_factory::icp::AdapterManager::IoHandle oldHandle =
        service.manager().resolveIoHandle("hang-n");
    expect(oldHandle.adapter.get() == raw, "N enrolled runtime is gated adapter");
    expect(oldHandle.io_mutex != nullptr, "N OLD io_mutex present");

    // Peer becomes available while OLD connect() remains blocked. Do not
    // wait for lifecycle idle and do not open the gate.
    raw->setPeerAvailable(true);

    expect(waitFor(
               [&]() {
                 virtual_factory::IndustrialAdapter *runtime =
                     service.manager().adapter("hang-n");
                 auto v = service.adapter("hang-n");
                 return runtime != nullptr && runtime != raw && v
                     && v->connectionState == "CONNECTED"
                     && raw->connectCompleted() == 0
                     && raw->connectEntered() >= 2;
               },
               std::chrono::milliseconds(8000)),
           "N isolated replacement CONNECTED while OLD connect() blocked");
    expect(service.manager().adapterCount() == 1,
           "N one enrolled runtime after isolation");
    virtual_factory::IndustrialAdapter *replacement =
        service.manager().adapter("hang-n");
    expect(replacement != nullptr && replacement != raw,
           "N NEW runtime is a different object");
    const virtual_factory::icp::AdapterManager::IoHandle newHandle =
        service.manager().resolveIoHandle("hang-n");
    expect(newHandle.io_mutex != nullptr
               && newHandle.io_mutex != oldHandle.io_mutex,
           "N NEW runtime has a fresh io_mutex");
    expect(oldHandle.enrolled && !oldHandle.enrolled->load(),
           "N OLD runtime unenrolled");
    expect(raw->connectCompleted() == 0 && raw->connectEntered() >= 2,
           "N OLD connect() still blocked after NEW CONNECTED");
    expect(sessionOf(service, "hang-n").connectionAttempts == 0,
           "N still no operator Connect after isolation");
    expect(sessionOf(service, "hang-n").successfulConnections >= 1,
           "N NEW RecoveryConnect succeeded");

    gate->open();
    expect(waitFor(
               [&]() { return raw->connectFailed() >= 1 || raw->connectCompleted() >= 1; },
               std::chrono::milliseconds(2000)),
           "N OLD connect() returned after gate open");
    expect(service.manager().adapter("hang-n") == replacement,
           "N OLD completion did not replace NEW runtime");
    expect(service.manager().adapterCount() == 1,
           "N adapter count remains 1 after OLD connect returns");
    auto after = service.adapter("hang-n");
    expect(after && after->connectionState == "CONNECTED",
           "N NEW remains CONNECTED after OLD connect returns");
    expect(after && after->lastError.find("peer unavailable") == std::string::npos,
           "N OLD error did not reappear on NEW runtime");
    expect(sessionOf(service, "hang-n").successfulConnections >= 1,
           "N OLD completion did not clear NEW success counters");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // O) Timeout-bound RecoveryConnect (peer down, connect() returns) must
  //    remain on the same runtime and reach FAULTED via applyConnectOutcome.
  //    Must not rematerialize at the hung-poll 2s mark.
  // ---------------------------------------------------------------------
  {
    const std::uint16_t port = reserveLoopbackPort();
    expect(port != 0, "O reserve port");
    const std::string endpoint =
        "opc.tcp://127.0.0.1:" + std::to_string(port);
    const std::string cfg = tempPath("o-cfg.json");
    const std::string hist = tempPath("o-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());

    ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeOpcUa("opcua-o", endpoint, 500)).ok,
           "O upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "O save/load arms recovery");

    virtual_factory::IndustrialAdapter *original =
        service.manager().adapter("opcua-o");
    expect(original != nullptr, "O runtime present after materialize");

    expect(waitFor(
               [&]() {
                 auto v = service.adapter("opcua-o");
                 return v && v->connectionState == "FAULTED"
                     && sessionOf(service, "opcua-o").failedConnections >= 1
                     && !v->lastError.empty();
               },
               std::chrono::milliseconds(4000)),
           "O timeout-bound RecoveryConnect reaches FAULTED on first attempt");
    expect(service.manager().adapter("opcua-o") == original,
           "O first FAULTED kept the same runtime (not rematerialized)");
    expect(service.manager().adapterCount() == 1, "O adapter count remains 1");
    expect(waitFor(
               [&]() { return lifecycleIdle(service, "opcua-o"); },
               std::chrono::milliseconds(2000)),
           "O lifecycle idle after timeout-bound failure");

    const auto afterFirst = std::chrono::steady_clock::now();
    const auto fail1 = sessionOf(service, "opcua-o").failedConnections;
    bool rematerialized = false;
    while (std::chrono::steady_clock::now() - afterFirst
           < std::chrono::milliseconds(3500))
    {
      if (service.manager().adapter("opcua-o") != original)
      {
        rematerialized = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    expect(!rematerialized,
           "O same runtime across hung-poll 2s window (timeout-bound connect)");
    expect(service.manager().adapter("opcua-o") == original,
           "O runtime identity unchanged after 2s poll-isolation window");
    auto still = service.adapter("opcua-o");
    expect(still && still->connectionState == "FAULTED",
           "O remains FAULTED (not silent DISCONNECTED) after 2s");
    expect(still && !still->lastError.empty(), "O FAULTED lastError retained");

    expect(waitFor(
               [&]() {
                 return sessionOf(service, "opcua-o").failedConnections > fail1
                     && service.manager().adapter("opcua-o") == original;
               },
               std::chrono::milliseconds(8000)),
           "O second timeout-bound RecoveryConnect failed on the same runtime");
    expect(lifecycleIdle(service, "opcua-o")
               || sessionOf(service, "opcua-o").failedConnections > fail1,
           "O second attempt counted as a real protocol failure");
    auto afterSecond = service.adapter("opcua-o");
    expect(afterSecond && afterSecond->connectionState == "FAULTED",
           "O second failure is FAULTED via applyConnectOutcome");
    expect(service.manager().adapterCount() == 1,
           "O still one enrolled runtime after two timeout-bound failures");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // P) First automatic RecoveryConnect starts while DISCONNECTED, remains
  //    in-flight past timeoutMs+slack, never becomes FAULTED via
  //    applyConnectOutcome, is isolated, and the replacement RecoveryConnect
  //    reaches CONNECTED without HTTP Connect/Reconnect. OLD connect() stays
  //    blocked. Cross-adapter peer remains independent.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("p-cfg.json");
    const std::string hist = tempPath("p-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-p");
    hangCfg.connection.timeoutMs = 500;
    expect(service.upsertAdapterConfig(hangCfg).ok, "P upsert hung adapter");
    expect(service.upsertAdapterConfig(makeMock("hang-p-other")).ok,
           "P upsert independent adapter");
    expect(service.saveConfiguration().ok, "P save");
    expect(service.loadConfiguration().ok, "P load arms autoConnectDesired");
    {
      auto extracted = service.manager().extractAdapter("hang-p");
      (void)extracted;
    }
    auto gate = std::make_shared<HangGate>();
    auto hang = std::make_unique<GatedConnectAdapter>("hang-p", gate);
    hang->setPeerAvailable(false);
    GatedConnectAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "P add gated runtime");
    virtual_factory::IndustrialAdapter *otherBeforeStart =
        service.manager().adapter("hang-p-other");
    expect(otherBeforeStart != nullptr, "P independent runtime present");
    service.start();

    expect(raw->waitConnectEntered(std::chrono::milliseconds(4000)),
           "P first RecoveryConnect entered connect() while DISCONNECTED");
    expect(raw->connectEntered() == 1, "P exactly one in-flight connect()");
    expect(raw->connectFailed() == 0 && raw->connectCompleted() == 0,
           "P OLD connect() has not returned");
    {
      auto v = service.adapter("hang-p");
      expect(v && v->connectionState == "DISCONNECTED",
             "P live state remains DISCONNECTED (applyConnectOutcome not reached)");
    }
    expect(sessionOf(service, "hang-p").failedConnections == 0,
           "P failedConnections stays 0 while RecoveryConnect is hung");
    expect(sessionOf(service, "hang-p").connectionAttempts == 0,
           "P no operator Connect on first-DISCONNECTED hang");
    expect(sessionOf(service, "hang-p").autoConnectDesired,
           "P autoConnectDesired armed by materialize");
    expect(sessionOf(service, "hang-p").reconnectCount >= 1,
           "P RecoveryConnect was scheduled");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-p-other");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(4000)),
           "P independent adapter CONNECTED while hang-p is stuck");

    const virtual_factory::icp::AdapterManager::IoHandle oldHandle =
        service.manager().resolveIoHandle("hang-p");
    expect(oldHandle.adapter.get() == raw, "P enrolled runtime is gated adapter");
    expect(oldHandle.io_mutex != nullptr, "P OLD io_mutex present");
    virtual_factory::IndustrialAdapter *otherDuringHang =
        service.manager().adapter("hang-p-other");
    expect(otherDuringHang == otherBeforeStart,
           "P independent adapter was not rematerialized during hang");

    raw->setPeerAvailable(true);

    expect(waitFor(
               [&]() {
                 virtual_factory::IndustrialAdapter *runtime =
                     service.manager().adapter("hang-p");
                 auto v = service.adapter("hang-p");
                 return runtime != nullptr && runtime != raw && v
                     && v->connectionState == "CONNECTED"
                     && raw->connectCompleted() == 0
                     && raw->connectEntered() == 1
                     && raw->connectFailed() == 0;
               },
               std::chrono::milliseconds(8000)),
           "P isolated replacement CONNECTED while OLD DISCONNECTED connect() blocked");
    expect(service.manager().adapterCount() == 2,
           "P two enrolled runtimes after isolation (hang-p + independent)");
    virtual_factory::IndustrialAdapter *replacement =
        service.manager().adapter("hang-p");
    expect(replacement != nullptr && replacement != raw,
           "P NEW runtime is a different object");
    const virtual_factory::icp::AdapterManager::IoHandle newHandle =
        service.manager().resolveIoHandle("hang-p");
    expect(newHandle.io_mutex != nullptr
               && newHandle.io_mutex != oldHandle.io_mutex,
           "P NEW runtime has a fresh io_mutex");
    expect(oldHandle.enrolled && !oldHandle.enrolled->load(),
           "P OLD runtime unenrolled");
    expect(sessionOf(service, "hang-p").connectionAttempts == 0,
           "P still no operator Connect after isolation");
    expect(sessionOf(service, "hang-p").successfulConnections >= 1,
           "P NEW RecoveryConnect incremented successfulConnections");
    expect(sessionOf(service, "hang-p").failedConnections == 0,
           "P hung DISCONNECTED connect was not classified as a returned failure");
    expect(service.manager().adapter("hang-p-other") == otherDuringHang,
           "P independent adapter identity unchanged after isolation");
    {
      auto other = service.adapter("hang-p-other");
      expect(other && other->connectionState == "CONNECTED",
             "P independent adapter remains CONNECTED");
    }

    bool sawIsolationEvent = false;
    for (const auto &ev : service.events(100))
    {
      if (ev.adapterId == "hang-p"
          && ev.eventType == "recovery_runtime_isolated")
      {
        sawIsolationEvent = true;
        expect(ev.reason == "hung automatic recovery connection",
               "P isolation reason is hung automatic recovery connection");
        expect(ev.durationMs >= 1000,
               "P isolation waited at least timeoutMs+slack");
        expect(ev.message.find("isolated/replaced") != std::string::npos,
               "P isolation message describes runtime replacement");
        expect(ev.message.find("timeoutMs=500") != std::string::npos,
               "P isolation message includes configured timeout");
      }
    }
    expect(sawIsolationEvent, "P recorded hung automatic recovery isolation event");

    const auto successBeforeOldReturn =
        sessionOf(service, "hang-p").successfulConnections;
    gate->open();
    expect(waitFor(
               [&]() {
                 return raw->connectFailed() >= 1 || raw->connectCompleted() >= 1;
               },
               std::chrono::milliseconds(2000)),
           "P OLD connect() returned after gate open");
    expect(service.manager().adapter("hang-p") == replacement,
           "P OLD completion did not replace NEW runtime");
    expect(service.manager().adapterCount() == 2,
           "P adapter count remains 2 after OLD connect returns");
    auto after = service.adapter("hang-p");
    expect(after && after->connectionState == "CONNECTED",
           "P NEW remains CONNECTED after OLD connect returns");
    expect(sessionOf(service, "hang-p").successfulConnections
               >= successBeforeOldReturn,
           "P OLD completion did not clear NEW success counters");
    expect(sessionOf(service, "hang-p").failedConnections == 0,
           "P OLD completion did not mark NEW FAULTED");
    expect(sessionOf(service, "hang-p").connectionAttempts == 0,
           "P OLD completion did not count an operator Connect");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // P2) Isolation with OLD RecoveryConnect still stuck must not prevent
  //     bounded shutdown. Open the gate only after stop() returns.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("p2-cfg.json");
    const std::string hist = tempPath("p2-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-p2");
    hangCfg.connection.timeoutMs = 500;
    expect(service.upsertAdapterConfig(hangCfg).ok, "P2 upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "P2 save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-p2");
      (void)extracted;
    }
    auto gate = std::make_shared<HangGate>();
    auto hang = std::make_unique<GatedConnectAdapter>("hang-p2", gate);
    hang->setPeerAvailable(false);
    GatedConnectAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "P2 add gated");
    service.start();
    expect(raw->waitConnectEntered(std::chrono::milliseconds(4000)),
           "P2 first RecoveryConnect entered");
    raw->setPeerAvailable(true);
    expect(waitFor(
               [&]() {
                 virtual_factory::IndustrialAdapter *runtime =
                     service.manager().adapter("hang-p2");
                 auto v = service.adapter("hang-p2");
                 return runtime != nullptr && runtime != raw && v
                     && v->connectionState == "CONNECTED"
                     && raw->connectEntered() == 1
                     && raw->connectCompleted() == 0;
               },
               std::chrono::milliseconds(8000)),
           "P2 NEW CONNECTED while OLD connect() still blocked");
    expect(raw->connectFailed() == 0 && raw->connectCompleted() == 0,
           "P2 OLD still in-flight at shutdown");
    const auto stopStarted = std::chrono::steady_clock::now();
    service.stop();
    const auto stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - stopStarted);
    expect(stopMs < std::chrono::milliseconds(12000),
           "P2 stop returned while isolated OLD connect() remained stuck");
    gate->open();
    expect(waitFor(
               [&]() {
                 return raw->connectFailed() >= 1 || raw->connectCompleted() >= 1;
               },
               std::chrono::milliseconds(2000)),
           "P2 OLD connect() returned after stop + gate open");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // Q) Operator Connect that remains in-flight past timeoutMs+slack must
  //    NOT be isolated. Same runtime becomes CONNECTED when connect()
  //    returns; no recovery_runtime_isolated event.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("q-cfg.json");
    const std::string hist = tempPath("q-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-q");
    hangCfg.connection.timeoutMs = 500;
    expect(service.upsertAdapterConfig(hangCfg).ok, "Q upsert");

    auto gate = std::make_shared<HangGate>();
    GatedConnectAdapter *raw = addGated(service, "hang-q", gate);
    raw->setPeerAvailable(true);

    auto started = service.connectAdapter("hang-q");
    expect(started.ok && started.accepted, "Q operator Connect accepted");
    expect(raw->waitConnectEntered(std::chrono::milliseconds(2000)),
           "Q operator Connect entered connect()");
    virtual_factory::IndustrialAdapter *original =
        service.manager().adapter("hang-q");
    expect(original == raw, "Q enrolled runtime is gated adapter");

    const auto t0 = std::chrono::steady_clock::now();
    bool rematerialized = false;
    while (std::chrono::steady_clock::now() - t0
           < std::chrono::milliseconds(2500))
    {
      if (service.manager().adapter("hang-q") != original)
      {
        rematerialized = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    expect(!rematerialized,
           "Q operator Connect was not isolated past timeoutMs+slack");
    expect(service.manager().adapter("hang-q") == original,
           "Q same runtime after timeoutMs+slack window");
    expect(service.manager().adapterCount() == 1, "Q one enrolled runtime");
    expect(raw->connectEntered() == 1, "Q no replacement RecoveryConnect");
    expect(raw->connectCompleted() == 0 && raw->connectFailed() == 0,
           "Q operator connect() still in-flight");
    {
      auto v = service.adapter("hang-q");
      expect(v && v->connectionState == "DISCONNECTED",
             "Q still DISCONNECTED while operator Connect is in-flight");
    }
    bool sawIsolationEvent = false;
    for (const auto &ev : service.events(100))
    {
      if (ev.adapterId == "hang-q"
          && ev.eventType == "recovery_runtime_isolated")
      {
        sawIsolationEvent = true;
      }
    }
    expect(!sawIsolationEvent, "Q did not emit hung-recovery isolation event");

    raw->setPeerAvailable(true);
    gate->open();
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-q");
                 return v && v->connectionState == "CONNECTED"
                     && service.manager().adapter("hang-q") == original;
               },
               std::chrono::milliseconds(2000)),
           "Q same runtime CONNECTED after operator Connect returns");
    expect(raw->connectCompleted() == 1, "Q operator connect() succeeded");
    expect(sessionOf(service, "hang-q").connectionAttempts >= 1,
           "Q connectionAttempts counted the operator Connect");
    expect(sessionOf(service, "hang-q").successfulConnections >= 1,
           "Q successfulConnections incremented on the original runtime");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // H1 — Run B: two hung RecoveryConnect isolations/abandons; Gen2 connects
  //      while Gen0 and Gen1 remain blocked. HTTP/status stay responsive.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h1-cfg.json");
    const std::string hist = tempPath("h1-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-h1");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "H1 upsert hung");
    expect(service.upsertAdapterConfig(makeMock("hang-h1-other")).ok,
           "H1 upsert sibling");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "H1 save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-h1");
      (void)extracted;
    }
    auto gate0 = std::make_shared<HangGate>();
    auto hang0 = makeHanging("hang-h1", gate0);
    GatedConnectAdapter *raw0 = hang0.get();
    raw0->setStaleMarker(GatedConnectAdapter::kStaleMarker);
    raw0->setPublishError("stale gen0");
    expect(service.manager().addAdapter(std::move(hang0)).ok, "H1 add Gen0");

    auto gate1 = std::make_shared<HangGate>();
    auto hang1 = makeHanging("hang-h1", gate1);
    GatedConnectAdapter *raw1 = hang1.get();
    service.injectRuntimeAdapterForTest(std::move(hang1));

    service.start();
    expect(raw0->waitConnectEntered(std::chrono::milliseconds(4000)),
           "H1 Gen0 RecoveryConnect entered");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-h1-other");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(4000)),
           "H1 sibling CONNECTED during Gen0 hang");

    const int httpPort = 19200 + (::getpid() % 800);
    virtual_factory::icp::HttpApiServer api(service, "", "127.0.0.1", httpPort);
    expect(api.start(), "H1 HTTP API starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    httplib::Client client("127.0.0.1", httpPort);
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(2, 0);

    expect(waitFor(
               [&]() {
                 return raw1->connectEntered() >= 1
                     && service.manager().adapter("hang-h1") == raw1
                     && isolationEventCount(service, "hang-h1") >= 1
                     && raw0->connectCompleted() == 0
                     && raw0->connectFailed() == 0;
               },
               std::chrono::milliseconds(6000)),
           "H1 Gen1 hung replacement entered after first isolation");
    expect(service.lifecycle().liveWorkerCount() == 4,
           "H1 live workers == 4 after first abandon");
    expect(service.probes().liveWorkerCount() == 4,
           "H1 probe live workers == 4 after first hang");
    expect(service.lifecycle().abandonedWorkerCount() <= 8,
           "H1 abandonedCount <= 8 after first abandon");
    expect(service.probes().quarantinedWorkerCount() <= 8,
           "H1 probe quarantine <= 8 after first hang");

    expect(waitFor(
               [&]() {
                 virtual_factory::IndustrialAdapter *runtime =
                     service.manager().adapter("hang-h1");
                 auto v = service.adapter("hang-h1");
                 return runtime != nullptr && runtime != raw0 && runtime != raw1
                     && v && v->connectionState == "CONNECTED"
                     && raw0->connectEntered() == 1
                     && raw1->connectEntered() == 1
                     && raw0->connectCompleted() == 0
                     && raw1->connectCompleted() == 0;
               },
               std::chrono::milliseconds(8000)),
           "H1 Gen2 CONNECTED while Gen0 and Gen1 remain blocked");
    expect(isolationEventCount(service, "hang-h1") >= 2,
           "H1 two isolation events");
    expect(service.lifecycle().liveWorkerCount() == 4,
           "H1 live workers == 4 after Gen2 connected");
    expect(service.lifecycle().abandonedWorkerCount() <= 8,
           "H1 abandonedCount <= 8");
    expect(service.lifecycle().abandonedWorkerCount("hang-h1") <= 2,
           "H1 per-adapter abandoned <= 2");
    expect(statusResponsive(service), "H1 status/diagnostics responsive");
    {
      const auto tHttp = std::chrono::steady_clock::now();
      auto res = client.Get("/api/v1/status");
      const auto httpMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - tHttp)
                              .count();
      expect(res && res->status == 200, "H1 HTTP GET /api/v1/status 200");
      expect(httpMs < 500, "H1 HTTP GET remained under 500ms");
    }
    {
      auto other = service.adapter("hang-h1-other");
      expect(other && other->connectionState == "CONNECTED",
             "H1 sibling remains CONNECTED");
    }

    virtual_factory::IndustrialAdapter *gen2 =
        service.manager().adapter("hang-h1");
    const auto successBefore = sessionOf(service, "hang-h1").successfulConnections;
    gate0->open();
    gate1->open();
    expect(waitFor(
               [&]() {
                 return (raw0->connectFailed() >= 1 || raw0->connectCompleted() >= 1)
                     && (raw1->connectFailed() >= 1 || raw1->connectCompleted() >= 1)
                     && service.lifecycle().abandonedWorkerCount("hang-h1") == 0;
               },
               std::chrono::milliseconds(3000)),
           "H1/H5 stale workers returned and abandoned count decreased");
    expect(service.manager().adapter("hang-h1") == gen2,
           "H5 stale return did not replace Gen2 runtime");
    auto after = service.adapter("hang-h1");
    expect(after && after->connectionState == "CONNECTED",
           "H5 Gen2 remains CONNECTED after stale return");
    expect(sessionOf(service, "hang-h1").successfulConnections >= successBefore,
           "H5 stale return did not clear Gen2 success counters");
    expect(sessionOf(service, "hang-h1").failedConnections == 0,
           "H5 stale return did not mark Gen2 FAULTED");
    expect(!markerInCache(service, "EQ-hang-h1", 999.0),
           "H5 Gen0 stale_marker must not land in Gen2 cache");

    api.stop();
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // H2 — Short outage: one isolation, replacement connects, sibling stays up.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h2-cfg.json");
    const std::string hist = tempPath("h2-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-h2");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "H2 upsert hung");
    expect(service.upsertAdapterConfig(makeMock("hang-h2-other")).ok,
           "H2 upsert sibling");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "H2 save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-h2");
      (void)extracted;
    }
    auto gate = std::make_shared<HangGate>();
    auto hang = makeHanging("hang-h2", gate);
    GatedConnectAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "H2 add gated");
    service.start();
    expect(raw->waitConnectEntered(std::chrono::milliseconds(4000)),
           "H2 Gen0 entered");
    expect(waitFor(
               [&]() {
                 virtual_factory::IndustrialAdapter *runtime =
                     service.manager().adapter("hang-h2");
                 auto v = service.adapter("hang-h2");
                 auto other = service.adapter("hang-h2-other");
                 return runtime != nullptr && runtime != raw && v
                     && v->connectionState == "CONNECTED" && other
                     && other->connectionState == "CONNECTED"
                     && raw->connectEntered() == 1
                     && raw->connectCompleted() == 0;
               },
               std::chrono::milliseconds(8000)),
           "H2 isolated replacement CONNECTED; sibling Mock CONNECTED");
    expect(isolationEventCount(service, "hang-h2") == 1,
           "H2 exactly one isolation event");
    expect(service.lifecycle().liveWorkerCount() == 4, "H2 live workers == 4");
    gate->open();
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // H3 — Normal returned failure: no isolation, same runtime, backoff.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h3-cfg.json");
    const std::string hist = tempPath("h3-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-h3");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "H3 upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "H3 save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-h3");
      (void)extracted;
    }
    auto hang = std::make_unique<GatedConnectAdapter>("hang-h3", nullptr);
    hang->setPeerAvailable(false);
    GatedConnectAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "H3 add returning adapter");
    service.start();
    expect(waitFor(
               [&]() {
                 return sessionOf(service, "hang-h3").failedConnections >= 1
                     && raw->connectFailed() >= 1;
               },
               std::chrono::milliseconds(4000)),
           "H3 connect returned failure");
    virtual_factory::IndustrialAdapter *original =
        service.manager().adapter("hang-h3");
    expect(original == raw, "H3 same runtime after returned failure");
    const auto t0 = std::chrono::steady_clock::now();
    bool rematerialized = false;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1500))
    {
      if (service.manager().adapter("hang-h3") != original)
      {
        rematerialized = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    expect(!rematerialized, "H3 no isolation after returned failure");
    expect(isolationEventCount(service, "hang-h3") == 0, "H3 no isolation event");
    expect(sessionOf(service, "hang-h3").failedConnections >= 1,
           "H3 failedConnections incremented");
    expect(sessionOf(service, "hang-h3").autoReconnectBackoffMs.count() >= 1000,
           "H3 backoff remains intact");
    expect(service.lifecycle().abandonedWorkerCount() == 0,
           "H3 no runtime worker abandonment");
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // H4 — Budget: more than two isolations on one adapter never abandon >2
  //      and never grow the live pool.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h4-cfg.json");
    const std::string hist = tempPath("h4-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-h4");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "H4 upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "H4 save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-h4");
      (void)extracted;
    }
    std::vector<std::shared_ptr<HangGate>> gates;
    std::vector<GatedConnectAdapter *> raws;
    auto gate0 = std::make_shared<HangGate>();
    gates.push_back(gate0);
    auto hang0 = makeHanging("hang-h4", gate0);
    raws.push_back(hang0.get());
    expect(service.manager().addAdapter(std::move(hang0)).ok, "H4 add Gen0");
    for (int i = 0; i < 3; ++i)
    {
      auto g = std::make_shared<HangGate>();
      gates.push_back(g);
      auto h = makeHanging("hang-h4", g);
      raws.push_back(h.get());
      service.injectRuntimeAdapterForTest(std::move(h));
    }
    service.start();
    expect(waitFor(
               [&]() { return isolationEventCount(service, "hang-h4") >= 3; },
               std::chrono::milliseconds(12000)),
           "H4 at least three isolation attempts");
    expect(service.lifecycle().abandonedWorkerCount() <= 8,
           "H4 abandonedCount never exceeds 8");
    expect(service.lifecycle().abandonedWorkerCount("hang-h4") <= 2,
           "H4 per-adapter abandoned never exceeds 2");
    expect(service.lifecycle().liveWorkerCount() == 4,
           "H4 no extra joinable workers beyond 4");
    expect(service.probes().quarantinedWorkerCount() <= 8,
           "H4 probe quarantine never exceeds 8");
    expect(service.probes().quarantinedWorkerCount("hang-h4") <= 2,
           "H4 per-adapter probe quarantine never exceeds 2");
    expect(service.probes().liveWorkerCount() == 4,
           "H4 probe live workers stay at 4");
    expect(statusResponsive(service), "H4 status remains responsive");
    for (auto &g : gates)
    {
      g->open();
    }
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // H6 — Shutdown bounded while stale runtime workers remain blocked.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h6-cfg.json");
    const std::string hist = tempPath("h6-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-h6");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "H6 upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "H6 save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-h6");
      (void)extracted;
    }
    auto gate0 = std::make_shared<HangGate>();
    auto hang0 = makeHanging("hang-h6", gate0);
    GatedConnectAdapter *raw0 = hang0.get();
    expect(service.manager().addAdapter(std::move(hang0)).ok, "H6 add Gen0");
    auto gate1 = std::make_shared<HangGate>();
    auto hang1 = makeHanging("hang-h6", gate1);
    GatedConnectAdapter *raw1 = hang1.get();
    service.injectRuntimeAdapterForTest(std::move(hang1));
    service.start();
    expect(raw0->waitConnectEntered(std::chrono::milliseconds(4000)),
           "H6 Gen0 entered");
    expect(waitFor(
               [&]() {
                 virtual_factory::IndustrialAdapter *runtime =
                     service.manager().adapter("hang-h6");
                 auto v = service.adapter("hang-h6");
                 return runtime != nullptr && runtime != raw0 && runtime != raw1
                     && v && v->connectionState == "CONNECTED"
                     && raw0->connectCompleted() == 0
                     && raw1->connectCompleted() == 0
                     && raw1->connectEntered() >= 1;
               },
               std::chrono::milliseconds(10000)),
           "H6 replacement CONNECTED while two stale connects remain blocked");
    expect(raw0->connectFailed() == 0 && raw1->connectFailed() == 0,
           "H6 stale workers still in-flight at shutdown");
    const auto t0 = std::chrono::steady_clock::now();
    service.stop();
    const auto stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
    expect(stopMs < 12000, "H6 stop bounded under P2 grace while hangs remain");
    gate0->open();
    gate1->open();
    expect(waitFor(
               [&]() {
                 return (raw0->connectFailed() >= 1 || raw0->connectCompleted() >= 1)
                     && (raw1->connectFailed() >= 1 || raw1->connectCompleted() >= 1);
               },
               std::chrono::milliseconds(2000)),
           "H6 stale connects returned after stop + gate open");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // H7 — Cross-adapter: A isolates/hangs; B mock connects and commands.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h7-cfg.json");
    const std::string hist = tempPath("h7-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-h7");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "H7 upsert hung");
    auto sibling = makeMock("hang-h7-other");
    virtual_factory::icp::CommandMappingRecord start;
    start.command = "start";
    sibling.equipment.front().commands.push_back(start);
    sibling.equipment.front().capabilities.push_back("start");
    expect(service.upsertAdapterConfig(sibling).ok, "H7 upsert sibling");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "H7 save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-h7");
      (void)extracted;
    }
    auto gate0 = std::make_shared<HangGate>();
    auto hang0 = makeHanging("hang-h7", gate0);
    GatedConnectAdapter *raw0 = hang0.get();
    expect(service.manager().addAdapter(std::move(hang0)).ok, "H7 add Gen0");
    auto gate1 = std::make_shared<HangGate>();
    service.injectRuntimeAdapterForTest(makeHanging("hang-h7", gate1));
    service.start();
    expect(raw0->waitConnectEntered(std::chrono::milliseconds(4000)),
           "H7 A entered hang");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-h7-other");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(4000)),
           "H7 B CONNECTED while A hangs");
    expect(waitFor(
               [&]() { return isolationEventCount(service, "hang-h7") >= 1; },
               std::chrono::milliseconds(6000)),
           "H7 A isolated at least once");
    auto cmd = service.executeEquipmentCommand("EQ-hang-h7-other", "start");
    expect(cmd.ok, "H7 sibling command succeeds while A hangs");
    {
      auto v = service.adapter("hang-h7-other");
      expect(v && v->connectionState == "CONNECTED",
             "H7 B remains CONNECTED after command");
    }
    expect(statusResponsive(service), "H7 status responsive");
    gate0->open();
    gate1->open();
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // H8 — Long outage: budget + backoff; HTTP/diagnostics/scheduler/Mock ok.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h8-cfg.json");
    const std::string hist = tempPath("h8-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-h8");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "H8 upsert hung");
    expect(service.upsertAdapterConfig(makeMock("hang-h8-other")).ok,
           "H8 upsert sibling");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "H8 save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-h8");
      (void)extracted;
    }
    std::vector<std::shared_ptr<HangGate>> gates;
    auto gate0 = std::make_shared<HangGate>();
    gates.push_back(gate0);
    auto hang0 = makeHanging("hang-h8", gate0);
    expect(service.manager().addAdapter(std::move(hang0)).ok, "H8 add Gen0");
    for (int i = 0; i < 4; ++i)
    {
      auto g = std::make_shared<HangGate>();
      gates.push_back(g);
      service.injectRuntimeAdapterForTest(makeHanging("hang-h8", g));
    }
    service.start();
    const int httpPort = 19300 + (::getpid() % 800);
    virtual_factory::icp::HttpApiServer api(service, "", "127.0.0.1", httpPort);
    expect(api.start(), "H8 HTTP API starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    httplib::Client client("127.0.0.1", httpPort);
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(2, 0);

    expect(waitFor(
               [&]() { return isolationEventCount(service, "hang-h8") >= 2; },
               std::chrono::milliseconds(10000)),
           "H8 at least two isolations during long outage");
    const auto tHold = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - tHold
           < std::chrono::milliseconds(1500))
    {
      expect(service.lifecycle().abandonedWorkerCount() <= 8,
             "H8 abandonedCount <= 8 throughout");
      expect(service.lifecycle().liveWorkerCount() == 4,
             "H8 live workers == 4 throughout");
      expect(statusResponsive(service), "H8 status/diagnostics responsive");
      auto other = service.adapter("hang-h8-other");
      expect(other && other->connectionState == "CONNECTED",
             "H8 sibling Mock remains CONNECTED");
      const auto tHttp = std::chrono::steady_clock::now();
      auto res = client.Get("/api/v1/status");
      const auto httpMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - tHttp)
                              .count();
      expect(res && res->status == 200, "H8 HTTP GET /api/v1/status 200");
      expect(httpMs < 500, "H8 HTTP GET under 500ms");
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    expect(service.status().schedulerRunning, "H8 scheduler still running");
    for (auto &g : gates)
    {
      g->open();
    }
    api.stop();
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // H9 — Operator Connect is not auto-abandoned; Disconnect cancels recovery.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h9-cfg.json");
    const std::string hist = tempPath("h9-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-h9");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "H9 upsert");
    auto gate = std::make_shared<HangGate>();
    GatedConnectAdapter *raw = addGated(service, "hang-h9", gate);
    raw->setPeerAvailable(true);
    auto started = service.connectAdapter("hang-h9");
    expect(started.ok && started.accepted, "H9 operator Connect accepted");
    expect(raw->waitConnectEntered(std::chrono::milliseconds(2000)),
           "H9 operator Connect entered");
    virtual_factory::IndustrialAdapter *original =
        service.manager().adapter("hang-h9");
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    expect(service.manager().adapter("hang-h9") == original,
           "H9 operator Connect was not isolated/abandoned");
    expect(isolationEventCount(service, "hang-h9") == 0,
           "H9 no isolation event for operator Connect");
    expect(service.lifecycle().abandonedWorkerCount() == 0,
           "H9 operator Connect was not runtime-abandoned");
    expect(raw->connectEntered() == 1, "H9 no replacement RecoveryConnect");
    gate->open();
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-h9");
                 return v && v->connectionState == "CONNECTED"
                     && service.manager().adapter("hang-h9") == original;
               },
               std::chrono::milliseconds(2000)),
           "H9 same runtime CONNECTED after operator Connect");

    expect(service.disconnectAdapter("hang-h9").ok, "H9 Disconnect accepted");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-h9");
                 return v && v->connectionState == "DISCONNECTED"
                     && !sessionOf(service, "hang-h9").autoConnectDesired;
               },
               std::chrono::milliseconds(3000)),
           "H9 Disconnect cleared autoConnectDesired");
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(800))
    {
      auto v = service.adapter("hang-h9");
      expect(!(v && v->connectionState == "CONNECTED"),
             "H9 no unwanted replacement recovery after Disconnect");
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // Race — operator Reconnect after isolation generation bump, before abandon.
  // Forced interleaving via IsolationHookPoint::AfterGenerationBump.
  // ---------------------------------------------------------------------
  for (int raceIter = 0; raceIter < 3; ++raceIter)
  {
    const std::string cfg = tempPath("race-re-cfg.json");
    const std::string hist = tempPath("race-re-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-race");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "race Reconnect upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "race Reconnect save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-race");
      (void)extracted;
    }
    auto gate0 = std::make_shared<HangGate>();
    auto hang0 = makeHanging("hang-race", gate0);
    GatedConnectAdapter *raw0 = hang0.get();
    expect(service.manager().addAdapter(std::move(hang0)).ok, "race add Gen0");
    auto hang1 = std::make_unique<GatedConnectAdapter>("hang-race", nullptr);
    hang1->setPeerAvailable(true);
    hang1->setStaleMarker(2.0);
    service.injectRuntimeAdapterForTest(std::move(hang1));

    IsolationPause pause;
    service.setIsolationTestHook(
        virtual_factory::icp::IsolationHookPoint::AfterGenerationBump,
        [&]() { pause.arrivedAndWait(); });
    service.start();
    expect(raw0->waitConnectEntered(std::chrono::milliseconds(4000)),
           "race Reconnect Gen0 entered");
    expect(pause.waitArrived(std::chrono::milliseconds(8000)),
           "race Reconnect isolation paused after bump");
    const std::uint64_t genAtPause = service.lifecycle().generation("hang-race");
    expect(genAtPause == 1, "race Reconnect isolation bumped once to gen 1");
    expect(service.probes().hasPhysicalFor("hang-race"),
           "race Reconnect stale physical probe still tracked");
    expect(!service.lifecycle().hasInFlightOrPending(
               "hang-race", LifecycleOp::RecoveryConnect),
           "race Reconnect automatic recovery is not on LifecycleExecutor");

    auto recon = service.reconnectAdapter("hang-race");
    expect(recon.ok && recon.accepted, "race Reconnect accepted during pause");
    expect(service.lifecycle().generation("hang-race") == genAtPause,
           "race Reconnect did not bump generation a second time");
    expect(service.hasPendingOperatorConnectForTest("hang-race"),
           "race Reconnect preserved operator intent");
    expect(currentConnectStyleCount(service, "hang-race") == 0,
           "race Reconnect coalesced; no current-generation connect-style yet");

    pause.go();
    service.setIsolationTestHook(
        virtual_factory::icp::IsolationHookPoint::AfterGenerationBump, nullptr);

    expect(waitFor(
               [&]() {
                 return service.probes().quarantinedWorkerCount("hang-race") >= 1
                     || service.probes().quarantinedWorkerCount() >= 1
                     || service.lifecycle().abandonedWorkerCount("hang-race") == 1
                     || service.lifecycle().abandonedWorkerCount() == 1;
               },
               std::chrono::milliseconds(3000)),
           "race Reconnect stale Gen0 probe quarantined or lifecycle reclaimed");
    expect(service.lifecycle().generation("hang-race") == genAtPause,
           "race Reconnect generation stayed at isolation bump");
    expect(currentConnectStyleCount(service, "hang-race") <= 1,
           "race Reconnect at most one current-generation connect-style");
    expect(!service.lifecycle().hasInFlightOrPending(
               "hang-race", LifecycleOp::RecoveryConnect)
               || !service.lifecycle().hasInFlightOrPending(
                      "hang-race", LifecycleOp::Connect),
           "race Reconnect no duplicate Connect+RecoveryConnect");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-race");
                 return v && v->connectionState == "CONNECTED"
                     && currentConnectStyleCount(service, "hang-race") == 0;
               },
               std::chrono::milliseconds(5000)),
           "race Reconnect eventual CONNECTED after operator intent");
    expect(service.lifecycle().liveWorkerCount() == 4,
           "race Reconnect live workers remain 4");
    expect(raw0->connectEntered() == 1, "race Reconnect no extra Gen0 connect");

    gate0->open();
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // Race — operator Connect in the extract/add window.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("race-co-cfg.json");
    const std::string hist = tempPath("race-co-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-race-c");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "race Connect upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "race Connect save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-race-c");
      (void)extracted;
    }
    auto gate0 = std::make_shared<HangGate>();
    auto hang0 = makeHanging("hang-race-c", gate0);
    GatedConnectAdapter *raw0 = hang0.get();
    expect(service.manager().addAdapter(std::move(hang0)).ok, "race Connect add");
    auto hang1 = std::make_unique<GatedConnectAdapter>("hang-race-c", nullptr);
    hang1->setPeerAvailable(true);
    service.injectRuntimeAdapterForTest(std::move(hang1));

    IsolationPause pause;
    service.setIsolationTestHook(
        virtual_factory::icp::IsolationHookPoint::AfterExtractBeforeAdd,
        [&]() { pause.arrivedAndWait(); });
    service.start();
    expect(raw0->waitConnectEntered(std::chrono::milliseconds(4000)),
           "race Connect Gen0 entered");
    expect(pause.waitArrived(std::chrono::milliseconds(8000)),
           "race Connect paused in extract/add window");
    expect(service.manager().adapter("hang-race-c") == nullptr,
           "race Connect manager empty in extract/add window");
    const std::uint64_t genAtPause = service.lifecycle().generation("hang-race-c");

    auto started = service.connectAdapter("hang-race-c");
    expect(started.ok && started.accepted, "race Connect accepted in gap");
    expect(service.hasPendingOperatorConnectForTest("hang-race-c"),
           "race Connect recorded pending operator intent");
    expect(service.lifecycle().generation("hang-race-c") == genAtPause,
           "race Connect did not bump generation");
    expect(service.manager().adapter("hang-race-c") == nullptr,
           "race Connect did not create a runtime in the extract/add gap");
    expect(currentConnectStyleCount(service, "hang-race-c") == 0,
           "race Connect coalesced against stale RecoveryConnect");

    pause.go();
    service.setIsolationTestHook(
        virtual_factory::icp::IsolationHookPoint::AfterExtractBeforeAdd, nullptr);
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-race-c");
                 return v && v->connectionState == "CONNECTED"
                     && currentConnectStyleCount(service, "hang-race-c") <= 1;
               },
               std::chrono::milliseconds(5000)),
           "race Connect eventual CONNECTED");
    expect(service.lifecycle().liveWorkerCount() == 4,
           "race Connect live workers remain 4");
    gate0->open();
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // Race — Disconnect during isolation bump/abandon window cancels recovery.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("race-di-cfg.json");
    const std::string hist = tempPath("race-di-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-race-d");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "race Disconnect upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "race Disconnect save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-race-d");
      (void)extracted;
    }
    auto gate0 = std::make_shared<HangGate>();
    auto hang0 = makeHanging("hang-race-d", gate0);
    GatedConnectAdapter *raw0 = hang0.get();
    expect(service.manager().addAdapter(std::move(hang0)).ok, "race Disconnect add");
    service.injectRuntimeAdapterForTest(makeHanging("hang-race-d", gate0));

    IsolationPause pause;
    service.setIsolationTestHook(
        virtual_factory::icp::IsolationHookPoint::AfterGenerationBump,
        [&]() { pause.arrivedAndWait(); });
    service.start();
    expect(raw0->waitConnectEntered(std::chrono::milliseconds(4000)),
           "race Disconnect Gen0 entered");
    expect(pause.waitArrived(std::chrono::milliseconds(8000)),
           "race Disconnect isolation paused");
    const std::uint64_t genAtPause = service.lifecycle().generation("hang-race-d");
    expect(service.disconnectAdapter("hang-race-d").ok,
           "race Disconnect accepted during pause");
    expect(service.lifecycle().generation("hang-race-d") == genAtPause + 1,
           "race Disconnect bumped generation");
    expect(!service.hasPendingOperatorConnectForTest("hang-race-d"),
           "race Disconnect cleared pending operator intent");
    expect(!sessionOf(service, "hang-race-d").autoConnectDesired,
           "race Disconnect cleared autoConnectDesired");

    pause.go();
    service.setIsolationTestHook(
        virtual_factory::icp::IsolationHookPoint::AfterGenerationBump, nullptr);
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1200))
    {
      auto v = service.adapter("hang-race-d");
      expect(!(v && v->connectionState == "CONNECTED"),
             "race Disconnect no automatic RecoveryConnect");
      expect(!service.lifecycle().hasInFlightOrPending(
                 "hang-race-d", LifecycleOp::RecoveryConnect),
             "race Disconnect no current-generation RecoveryConnect");
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    expect(service.lifecycle().liveWorkerCount() == 4,
           "race Disconnect live workers remain 4");
    gate0->open();
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // Race — disable during BeforeCommit cancels automatic recovery.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("race-dis-cfg.json");
    const std::string hist = tempPath("race-dis-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-race-z");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "race disable upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "race disable save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-race-z");
      (void)extracted;
    }
    auto gate0 = std::make_shared<HangGate>();
    auto hang0 = makeHanging("hang-race-z", gate0);
    GatedConnectAdapter *raw0 = hang0.get();
    expect(service.manager().addAdapter(std::move(hang0)).ok, "race disable add");
    service.injectRuntimeAdapterForTest(makeHanging("hang-race-z", gate0));

    IsolationPause pause;
    service.setIsolationTestHook(
        virtual_factory::icp::IsolationHookPoint::BeforeCommit,
        [&]() { pause.arrivedAndWait(); });
    service.start();
    expect(raw0->waitConnectEntered(std::chrono::milliseconds(4000)),
           "race disable Gen0 entered");
    expect(pause.waitArrived(std::chrono::milliseconds(8000)),
           "race disable paused before commit");
    virtual_factory::icp::AdapterConfigRecord disabled = hangCfg;
    disabled.enabled = false;
    expect(service.upsertAdapterConfig(disabled).ok, "race disable upsert false");
    pause.go();
    service.setIsolationTestHook(
        virtual_factory::icp::IsolationHookPoint::BeforeCommit, nullptr);
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1200))
    {
      expect(!service.lifecycle().hasInFlightOrPending(
                 "hang-race-z", LifecycleOp::RecoveryConnect),
             "race disable no automatic RecoveryConnect");
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    expect(!sessionOf(service, "hang-race-z").autoConnectDesired,
           "race disable cleared autoConnectDesired");
    gate0->open();
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // Race — remove during BeforeCommit cancels automatic recovery.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("race-rm-cfg.json");
    const std::string hist = tempPath("race-rm-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-race-r");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "race remove upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "race remove save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-race-r");
      (void)extracted;
    }
    auto gate0 = std::make_shared<HangGate>();
    auto hang0 = makeHanging("hang-race-r", gate0);
    GatedConnectAdapter *raw0 = hang0.get();
    expect(service.manager().addAdapter(std::move(hang0)).ok, "race remove add");
    service.injectRuntimeAdapterForTest(makeHanging("hang-race-r", gate0));

    IsolationPause pause;
    service.setIsolationTestHook(
        virtual_factory::icp::IsolationHookPoint::BeforeCommit,
        [&]() { pause.arrivedAndWait(); });
    service.start();
    expect(raw0->waitConnectEntered(std::chrono::milliseconds(4000)),
           "race remove Gen0 entered");
    expect(pause.waitArrived(std::chrono::milliseconds(8000)),
           "race remove paused before commit");
    expect(service.removeAdapterConfig("hang-race-r").ok, "race remove accepted");
    pause.go();
    service.setIsolationTestHook(
        virtual_factory::icp::IsolationHookPoint::BeforeCommit, nullptr);
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1200))
    {
      expect(!service.lifecycle().hasInFlightOrPending(
                 "hang-race-r", LifecycleOp::RecoveryConnect),
             "race remove no automatic RecoveryConnect");
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    gate0->open();
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // H4-global — 9 distinct hung RecoveryConnect adapters: 8 abandons, 9th fails.
  // Covered at executor level; ApplicationService must keep liveWorkerCount==4.
  // ---------------------------------------------------------------------
  // (executor test H4g). Reserve-worker + H5 cache follow.

  // ---------------------------------------------------------------------
  // Reserve-worker — 3 live blocking RecoveryConnect jobs; no 4th blocking
  // RecoveryConnect from hung-poll isolation; HTTP/status stay responsive.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h8r-cfg.json");
    const std::string hist = tempPath("h8r-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    std::vector<std::shared_ptr<HangGate>> gates;
    std::vector<GatedConnectAdapter *> raws;
    for (int i = 0; i < 3; ++i)
    {
      const std::string id = "hang-res-" + std::to_string(i);
      expect(service.upsertAdapterConfig(makeMock(id)).ok, "reserve upsert hang");
      auto g = std::make_shared<HangGate>();
      gates.push_back(g);
      auto hang = std::make_unique<GatedConnectAdapter>(id, g);
      hang->setPeerAvailable(false);
      raws.push_back(hang.get());
      expect(service.manager().addAdapter(std::move(hang)).ok, "reserve add " + id);
      auto started = service.connectAdapter(id);
      expect(started.ok && started.accepted, "reserve operator Connect");
      expect(raws.back()->waitConnectEntered(std::chrono::milliseconds(4000)),
             "reserve hang entered");
    }
    expect(service.upsertAdapterConfig(makeMock("hang-res-poll")).ok,
           "reserve upsert poll");
    auto pollGate = std::make_shared<HangGate>();
    GatedPollAdapter *pollRaw =
        addGatedPoll(service, "hang-res-poll", pollGate);
    auto pollConnect = service.connectAdapter("hang-res-poll");
    expect(pollConnect.ok && pollConnect.accepted, "reserve poll Connect");
    expect(pollRaw->waitPollEntered(std::chrono::milliseconds(4000)),
           "reserve poll entered");
    expect(waitFor(
               [&]() { return !service.lifecycle().canAdmitBlockingIsolation(); },
               std::chrono::milliseconds(2000)),
           "reserve canAdmit false with 3 blocking jobs");

    const int httpPort = 19400 + (::getpid() % 800);
    virtual_factory::icp::HttpApiServer api(service, "", "127.0.0.1", httpPort);
    expect(api.start(), "reserve HTTP API starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    httplib::Client client("127.0.0.1", httpPort);
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(2, 0);

    const auto tHold = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - tHold
           < std::chrono::milliseconds(2500))
    {
      expect(isolationEventCount(service, "hang-res-poll") == 0,
             "reserve poll isolation did not start a 4th RecoveryConnect");
      expect(service.lifecycle().liveWorkerCount() == 4,
             "reserve live workers == 4");
      expect(statusResponsive(service), "reserve status responsive");
      auto res = client.Get("/api/v1/status");
      expect(res && res->status == 200, "reserve HTTP 200");
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    expect(pollRaw->connectEntered() == 1,
           "reserve poll adapter did not start RecoveryConnect");
    expect(!service.lifecycle().hasInFlightOrPending(
               "hang-res-poll", LifecycleOp::RecoveryConnect),
           "reserve no RecoveryConnect on poll adapter");

    for (auto &g : gates)
    {
      g->open();
    }
    pollGate->open();
    api.stop();
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // H5-cache — Gen2 CONNECTED; release Gen0; Gen0 cannot publish stale
  // telemetry/error/state into Gen2.
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("h5c-cfg.json");
    const std::string hist = tempPath("h5c-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    virtual_factory::icp::AdapterConfigRecord hangCfg = makeMock("hang-h5c");
    hangCfg.connection.timeoutMs = 200;
    expect(service.upsertAdapterConfig(hangCfg).ok, "H5c upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "H5c save/load");
    {
      auto extracted = service.manager().extractAdapter("hang-h5c");
      (void)extracted;
    }
    auto gate0 = std::make_shared<HangGate>();
    auto hang0 = makeHanging("hang-h5c", gate0);
    GatedConnectAdapter *raw0 = hang0.get();
    raw0->setStaleMarker(GatedConnectAdapter::kStaleMarker);
    raw0->setPublishError("stale gen0");
    expect(service.manager().addAdapter(std::move(hang0)).ok, "H5c add Gen0");
    auto hang2 = std::make_unique<GatedConnectAdapter>("hang-h5c", nullptr);
    hang2->setPeerAvailable(true);
    hang2->setStaleMarker(2.0);
    GatedConnectAdapter *raw2 = hang2.get();
    service.injectRuntimeAdapterForTest(std::move(hang2));

    service.start();
    expect(raw0->waitConnectEntered(std::chrono::milliseconds(4000)),
           "H5c Gen0 entered");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-h5c");
                 return raw2->connectCompleted() >= 1 && v
                     && v->connectionState == "CONNECTED"
                     && markerInCache(service, "EQ-hang-h5c", 2.0);
               },
               std::chrono::milliseconds(8000)),
           "H5c Gen2 CONNECTED with marker 2.0");
    raw0->setPeerAvailable(true);
    gate0->open();
    expect(waitFor(
               [&]() {
                 return raw0->connectCompleted() >= 1 || raw0->connectFailed() >= 1;
               },
               std::chrono::milliseconds(3000)),
           "H5c Gen0 returned");
    expect(markerInCache(service, "EQ-hang-h5c", 2.0),
           "H5c Gen2 marker remains after Gen0 return");
    expect(!markerInCache(service, "EQ-hang-h5c", GatedConnectAdapter::kStaleMarker),
           "H5c Gen0 marker did not overwrite Gen2");
    auto after = service.adapter("hang-h5c");
    expect(after && after->connectionState == "CONNECTED",
           "H5c Gen2 remains CONNECTED");
    expect(after && after->lastError.find("stale gen0") == std::string::npos,
           "H5c Gen0 error did not publish into Gen2");
    expect(service.manager().adapter("hang-h5c") == raw2,
           "H5c runtime still Gen2");
    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  if (failures == 0)
  {
    std::cout << "icp_lifecycle_recovery_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_lifecycle_recovery_test: " << failures << " failure(s)"
            << std::endl;
  return 1;
}
