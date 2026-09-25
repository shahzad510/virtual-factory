/// Lifecycle recovery: operator Connect/Reconnect follow-up vs automatic backoff.
///
/// Operator requests issued while a connect-style job is in-flight must not
/// start a parallel connect(), but a failed in-flight attempt must get one
/// prompt follow-up connect() without waiting for automatic backoff.
/// Rematerialize must not inherit a pending operator follow-up from the
/// previous runtime instance.

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

  bool connect() override
  {
    const bool peerUp = this->peerAvailable_.load();
    ++this->connectEntered_;
    {
      std::lock_guard<std::mutex> lock(this->mu_);
      this->entered_ = true;
    }
    this->enteredCv_.notify_all();
    if (this->gate_)
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
  std::atomic<int> connectEntered_{0};
  std::atomic<int> connectCompleted_{0};
  std::atomic<int> connectFailed_{0};
  std::atomic<int> disconnectCount_{0};
  mutable std::mutex mu_;
  std::condition_variable enteredCv_;
  bool entered_{false};
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

  if (failures == 0)
  {
    std::cout << "icp_lifecycle_recovery_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_lifecycle_recovery_test: " << failures << " failure(s)"
            << std::endl;
  return 1;
}
