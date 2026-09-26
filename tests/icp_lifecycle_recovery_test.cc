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
/// become FAULTED via applyConnectOutcome.

#include "opcua_test_server.hh"

#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/icp/LifecycleExecutor.hh>
#include <virtual_factory/equipment/GenericEquipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

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
  GatedConnectAdapter(std::string id, std::shared_ptr<HangGate> gate)
      : id_(std::move(id)), gate_(std::move(gate))
  {
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
      this->lastError_ = "peer unavailable";
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

  std::vector<virtual_factory::Equipment *> equipment() override { return {}; }
  virtual_factory::Equipment *equipmentById(const std::string &) override
  {
    return nullptr;
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
  virtual_factory::ConnectionState state_{
      virtual_factory::ConnectionState::Disconnected};
  std::string lastError_;
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
             id, virtual_factory::icp::LifecycleOp::Disconnect);
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
  // H) Generation bump drops a pending RecoveryConnect; the phantom latch
  //    is reconciled; automatic recovery proceeds without HTTP Connect.
  //    Four gated Connect jobs occupy the lifecycle pool so the target
  //    RecoveryConnect stays pending (not in-flight) until the bump.
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
                     && service.lifecycle().hasInFlightOrPending(
                            "hang-h", LifecycleOp::RecoveryConnect);
               },
               std::chrono::milliseconds(8000)),
           "H RecoveryConnect queued while workers occupied");
    expect(target->connectEntered() == enteredBefore,
           "H pending RecoveryConnect has not entered connect()");
    expect(sessionOf(service, "hang-h").autoReconnectInFlight,
           "H latch set when RecoveryConnect was enqueued");

    (void)service.lifecycle().bumpGeneration("hang-h");
    expect(!service.lifecycle().hasInFlightOrPending(
               "hang-h", LifecycleOp::RecoveryConnect),
           "H bumpGeneration dropped pending RecoveryConnect");

    expect(waitFor(
               [&]() {
                 const bool leBusy =
                     service.lifecycle().hasInFlightOrPending(
                         "hang-h", LifecycleOp::RecoveryConnect)
                     || service.lifecycle().hasInFlightOrPending(
                            "hang-h", LifecycleOp::Connect)
                     || service.lifecycle().hasInFlightOrPending(
                            "hang-h", LifecycleOp::Reconnect);
                 return !sessionOf(service, "hang-h").autoReconnectInFlight
                     || leBusy;
               },
               std::chrono::milliseconds(2000)),
           "H latch reconciled or a new connect-style job was scheduled");

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
           "H automatic RecoveryConnect reaches CONNECTED after latch reconcile");
    expect(sessionOf(service, "hang-h").successfulConnections >= 1,
           "H successfulConnections incremented by automatic recovery");
    expect(sessionOf(service, "hang-h").connectionAttempts == attemptsBefore,
           "H no HTTP Connect/Reconnect after generation drop");
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

  if (failures == 0)
  {
    std::cout << "icp_lifecycle_recovery_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_lifecycle_recovery_test: " << failures << " failure(s)"
            << std::endl;
  return 1;
}
