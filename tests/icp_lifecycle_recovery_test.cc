/// Regression: Reconnect must not discard an in-flight Connect/RecoveryConnect.
///
/// Startup RecoveryConnect + operator Reconnect previously bumped generation,
/// treated a successful UA_Client_connect as stale, disconnected it, and skipped
/// applyConnectOutcome (attempts > 0 with success=0 and fail=0).

#include "opcua_test_server.hh"

#include <virtual_factory/icp/app/ApplicationService.hh>
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

class HangConnectAdapter : public virtual_factory::IndustrialAdapter
{
public:
  HangConnectAdapter(std::string id, std::shared_ptr<HangGate> gate)
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

  bool connect() override
  {
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

  int connectEntered() const { return this->connectEntered_.load(); }
  int connectCompleted() const { return this->connectCompleted_.load(); }
  int disconnectCount() const { return this->disconnectCount_.load(); }

private:
  std::string id_;
  std::shared_ptr<HangGate> gate_;
  virtual_factory::ConnectionState state_{
      virtual_factory::ConnectionState::Disconnected};
  std::string lastError_;
  std::atomic<int> connectEntered_{0};
  std::atomic<int> connectCompleted_{0};
  std::atomic<int> disconnectCount_{0};
  std::mutex mu_;
  std::condition_variable enteredCv_;
  bool entered_{false};
};

/// First connect() returns failure immediately (peer down). Later connect()
/// waits on the gate until released (permanently blocked unless opened).
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

  bool connect() override
  {
    const int n = ++this->connectEntered_;
    {
      std::lock_guard<std::mutex> lock(this->mu_);
      this->entered_ = true;
    }
    this->enteredCv_.notify_all();
    if (n == 1)
    {
      this->state_ = virtual_factory::ConnectionState::Faulted;
      this->lastError_ = "peer down";
      ++this->connectReturned_;
      return false;
    }
    if (this->gate_)
    {
      this->gate_->wait();
    }
    ++this->connectReturned_;
    if (this->peerAvailable_.load())
    {
      this->state_ = virtual_factory::ConnectionState::Connected;
      this->lastError_.clear();
      ++this->connectSucceeded_;
      return true;
    }
    this->state_ = virtual_factory::ConnectionState::Faulted;
    this->lastError_ = "peer down";
    return false;
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

  bool waitEnteredAtLeast(int count, std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(this->mu_);
    return this->enteredCv_.wait_for(lock, timeout, [this, count]() {
      return this->connectEntered_.load() >= count;
    });
  }

  void setPeerAvailable(bool available) { this->peerAvailable_.store(available); }

  int connectEntered() const { return this->connectEntered_.load(); }
  int connectReturned() const { return this->connectReturned_.load(); }
  int connectSucceeded() const { return this->connectSucceeded_.load(); }
  int disconnectCount() const { return this->disconnectCount_.load(); }

private:
  std::string id_;
  std::shared_ptr<HangGate> gate_;
  virtual_factory::ConnectionState state_{
      virtual_factory::ConnectionState::Disconnected};
  std::string lastError_;
  std::atomic<int> connectEntered_{0};
  std::atomic<int> connectReturned_{0};
  std::atomic<int> connectSucceeded_{0};
  std::atomic<int> disconnectCount_{0};
  std::atomic<bool> peerAvailable_{false};
  std::mutex mu_;
  std::condition_variable enteredCv_;
  bool entered_{false};
};

/// connect() blocks for delay then returns failure (normal timeout path).
class DelayedFailConnectAdapter : public virtual_factory::IndustrialAdapter
{
public:
  DelayedFailConnectAdapter(std::string id, std::chrono::milliseconds delay)
      : id_(std::move(id)), delay_(delay)
  {
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
    std::this_thread::sleep_for(this->delay_);
    this->state_ = virtual_factory::ConnectionState::Faulted;
    this->lastError_ = "peer timeout";
    ++this->connectReturned_;
    return false;
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

  int connectEntered() const { return this->connectEntered_.load(); }
  int connectReturned() const { return this->connectReturned_.load(); }

private:
  std::string id_;
  std::chrono::milliseconds delay_;
  virtual_factory::ConnectionState state_{
      virtual_factory::ConnectionState::Disconnected};
  std::string lastError_;
  std::atomic<int> connectEntered_{0};
  std::atomic<int> connectReturned_{0};
  std::atomic<int> disconnectCount_{0};
};

int isolationEventCount(
    virtual_factory::icp::ApplicationService &service, const std::string &id)
{
  int n = 0;
  for (const auto &ev : service.events(500))
  {
    if (ev.adapterId == id && ev.eventType == "recovery_runtime_isolated")
    {
      ++n;
    }
  }
  return n;
}

virtual_factory::icp::AdapterConfigRecord makeMockTimed(
    const std::string &id, int timeoutMs)
{
  virtual_factory::icp::AdapterConfigRecord mock;
  mock.adapterId = id;
  mock.protocol = "mock";
  mock.enabled = true;
  mock.connection.timeoutMs = timeoutMs;
  virtual_factory::icp::EquipmentMappingRecord eq;
  eq.equipmentId = "EQ-" + id;
  eq.type = "motor";
  mock.equipment.push_back(eq);
  return mock;
}

GatedConnectAdapter *installGatedBeforeStart(
    virtual_factory::icp::ApplicationService &service,
    const std::string &id,
    const std::shared_ptr<HangGate> &gate,
    int timeoutMs,
    const char *tag)
{
  expect(service.upsertAdapterConfig(makeMockTimed(id, timeoutMs)).ok,
         std::string(tag) + " upsert");
  expect(service.saveConfiguration().ok, std::string(tag) + " save");
  expect(service.loadConfiguration().ok, std::string(tag) + " load");
  (void)service.manager().extractAdapter(id);
  auto gated = std::make_unique<GatedConnectAdapter>(id, gate);
  GatedConnectAdapter *raw = gated.get();
  expect(service.manager().addAdapter(std::move(gated)).ok,
         std::string(tag) + " add gated runtime");
  return raw;
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

}  // namespace

int main()
{
  using virtual_factory::icp::ApplicationService;

  // ---------------------------------------------------------------------
  // A) OPC UA down at startup → start server → Connect → CONNECTED
  // ---------------------------------------------------------------------
  {
    const std::uint16_t port = reserveLoopbackPort();
    expect(port != 0, "reserve OPC UA port");
    const std::string endpoint =
        "opc.tcp://127.0.0.1:" + std::to_string(port);
    const std::string cfg = tempPath("a-cfg.json");
    const std::string hist = tempPath("a-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());

    ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeOpcUa("opcua-rec-a", endpoint, 1500)).ok,
           "A upsert opcua");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "A save/load");

    expect(waitFor(
               [&]() {
                 auto v = service.adapter("opcua-rec-a");
                 return v
                     && (v->connectionState == "FAULTED"
                         || v->connectionState == "DISCONNECTED");
               },
               std::chrono::milliseconds(4000)),
           "A initial peer-down settles");

    virtual_factory::test::OpcUaTestServer server;
    server.setPort(port);
    expect(server.start(), "A OPC UA server starts after ICP");

    auto conn = service.connectAdapter("opcua-rec-a");
    expect(conn.ok && conn.accepted, "A Connect accepted: " + conn.message);
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("opcua-rec-a");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(8000)),
           "A adapter CONNECTED after Connect with server up");
    const auto sess = sessionOf(service, "opcua-rec-a");
    expect(sess.successfulConnections >= 1,
           "A successfulConnections incremented");
    service.stop();
    server.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // B) Reconnect during in-flight RecoveryConnect must not tear down success
  // C) Repeated Connect coalesces to one protocol connect()
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("b-cfg.json");
    const std::string hist = tempPath("b-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeMock("hang-rec")).ok, "B upsert");

    auto gate = std::make_shared<HangGate>();
    auto hang = std::make_unique<HangConnectAdapter>("hang-rec", gate);
    HangConnectAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "B add hang runtime");

    // Connect-style in-flight (same coalescing class as startup RecoveryConnect).
    auto started = service.connectAdapter("hang-rec");
    expect(started.ok && started.accepted, "B Connect accepted to start in-flight");
    expect(raw->waitConnectEntered(std::chrono::milliseconds(2000)),
           "B Connect/RecoveryConnect entered connect()");

    auto recon = service.reconnectAdapter("hang-rec");
    expect(recon.ok && recon.accepted, "B Reconnect accepted during in-flight");
    expect(raw->connectEntered() == 1, "B Reconnect did not start a second connect()");
    expect(raw->disconnectCount() == 0,
           "B Reconnect did not disconnect in-flight connect");

    for (int i = 0; i < 5; ++i)
    {
      auto c = service.connectAdapter("hang-rec");
      expect(c.ok && c.accepted, "C coalesced Connect accepted");
    }
    expect(raw->connectEntered() == 1, "C Connect requests coalesced (one connect())");

    gate->open();
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-rec");
                 return v && v->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(2000)),
           "B/C adapter CONNECTED after in-flight connect completes");
    expect(raw->connectCompleted() == 1, "B/C single successful connect()");
    expect(raw->disconnectCount() == 0,
           "B successful connect was not torn down by Reconnect");

    const auto sess = sessionOf(service, "hang-rec");
    expect(sess.successfulConnections >= 1,
           "B/C successfulConnections reflects completed connect");
    expect(sess.failedConnections == 0,
           "B/C no failedConnections on successful connect");

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto still = service.adapter("hang-rec");
    expect(still && still->connectionState == "CONNECTED",
           "B remains CONNECTED after Reconnect coalesced");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // D) Genuine Disconnect still cancels in-flight Connect
  // ---------------------------------------------------------------------
  {
    const std::string cfg = tempPath("d-cfg.json");
    const std::string hist = tempPath("d-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeMock("hang-disc")).ok, "D upsert");

    auto gate = std::make_shared<HangGate>();
    auto hang = std::make_unique<HangConnectAdapter>("hang-disc", gate);
    HangConnectAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "D add hang");
    auto started = service.connectAdapter("hang-disc");
    expect(started.ok && started.accepted, "D Connect accepted");
    expect(raw->waitConnectEntered(std::chrono::milliseconds(2000)),
           "D connect() entered");

    auto disc = service.disconnectAdapter("hang-disc");
    expect(disc.ok && disc.accepted, "D Disconnect accepted");
    gate->open();
    expect(waitFor(
               [&]() {
                 auto v = service.adapter("hang-disc");
                 return v && v->connectionState == "DISCONNECTED";
               },
               std::chrono::milliseconds(2000)),
           "D remains/settles DISCONNECTED");

    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(300))
    {
      auto v = service.adapter("hang-disc");
      expect(!(v && v->connectionState == "CONNECTED"),
             "D in-flight success must not stay CONNECTED after Disconnect");
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    expect(raw->disconnectCount() >= 1, "D disconnect ran after cancelled connect");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // HR-A/B/C) startup peer-down: first automatic connect fails, second hangs,
  // isolation rematerializes, NEW runtime connects; old completion cannot
  // overwrite NEW; repeated ticks do not create extra replacements.
  // ---------------------------------------------------------------------
  {
    const std::string id = "hang-hr-a";
    const std::string cfg = tempPath("hr-a-cfg.json");
    const std::string hist = tempPath("hr-a-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    auto gate = std::make_shared<HangGate>();
    GatedConnectAdapter *raw =
        installGatedBeforeStart(service, id, gate, 500, "HR-A");
    service.start();

    expect(raw->waitEnteredAtLeast(1, std::chrono::milliseconds(4000)),
           "HR-A first automatic connect entered");
    expect(waitFor(
               [&]() { return raw->connectEntered() >= 1 && raw->connectReturned() >= 1; },
               std::chrono::milliseconds(2000)),
           "HR-A first automatic connect returned failure");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter(id);
                 return v
                     && (v->connectionState == "FAULTED"
                         || v->connectionState == "DISCONNECTED");
               },
               std::chrono::milliseconds(2000)),
           "HR-A first failure settles FAULTED/DISCONNECTED");

    expect(raw->waitEnteredAtLeast(2, std::chrono::milliseconds(8000)),
           "HR-A second automatic connect is blocked");
    expect(raw->connectReturned() == 1, "HR-A second connect has not returned");

    expect(waitFor(
               [&]() { return isolationEventCount(service, id) >= 1; },
               std::chrono::milliseconds(6000)),
           "HR-A hung RecoveryConnect isolated");

    expect(waitFor(
               [&]() {
                 auto v = service.adapter(id);
                 return v && v->connectionState == "CONNECTED"
                     && service.manager().adapter(id) != raw;
               },
               std::chrono::milliseconds(4000)),
           "HR-A NEW runtime CONNECTED while old connect remains blocked");
    expect(raw->connectEntered() == 2, "HR-A old runtime still on second connect()");
    expect(raw->connectReturned() == 1, "HR-A old connect still blocked");

    const int isolatedAfterConnect = isolationEventCount(service, id);
    expect(isolatedAfterConnect == 1, "HR-C single isolation replacement");
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    expect(isolationEventCount(service, id) == isolatedAfterConnect,
           "HR-C repeated recovery ticks did not rematerialize again");
    expect(service.manager().adapter(id) != raw,
           "HR-C replacement runtime still enrolled");

    const auto sessConnected = sessionOf(service, id);
    const std::size_t successBeforeOldReturn = sessConnected.successfulConnections;
    const std::size_t failedBeforeOldReturn = sessConnected.failedConnections;
    expect(successBeforeOldReturn >= 1, "HR-A NEW runtime counted a success");

    raw->setPeerAvailable(true);
    gate->open();
    expect(waitFor(
               [&]() { return raw->connectReturned() >= 2; },
               std::chrono::milliseconds(2000)),
           "HR-B old blocked connect eventually returned");

    expect(waitFor(
               [&]() {
                 auto v = service.adapter(id);
                 return v && v->connectionState == "CONNECTED"
                     && service.manager().adapter(id) != raw;
               },
               std::chrono::milliseconds(2000)),
           "HR-B NEW runtime remains CONNECTED after old completion");
    const auto sessAfter = sessionOf(service, id);
    expect(sessAfter.successfulConnections == successBeforeOldReturn,
           "HR-B old completion did not increment NEW successfulConnections");
    expect(sessAfter.failedConnections == failedBeforeOldReturn,
           "HR-B old completion did not mutate NEW failedConnections");
    expect(raw->connectSucceeded() <= 1,
           "HR-B old runtime success stayed on the extracted instance");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // HR-D) normal connect returning at/near timeoutMs stays on the same
  // runtime and follows FAULTED/backoff (no rematerialize).
  // ---------------------------------------------------------------------
  {
    const std::string id = "delay-hr-d";
    const std::string cfg = tempPath("hr-d-cfg.json");
    const std::string hist = tempPath("hr-d-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    expect(service.upsertAdapterConfig(makeMockTimed(id, 500)).ok, "HR-D upsert");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "HR-D save/load");
    (void)service.manager().extractAdapter(id);
    auto delayed = std::make_unique<DelayedFailConnectAdapter>(
        id, std::chrono::milliseconds(600));
    DelayedFailConnectAdapter *raw = delayed.get();
    expect(service.manager().addAdapter(std::move(delayed)).ok, "HR-D add delayed");
    service.start();

    expect(waitFor(
               [&]() { return raw->connectReturned() >= 1; },
               std::chrono::milliseconds(4000)),
           "HR-D connect returned failure near timeoutMs");
    expect(waitFor(
               [&]() {
                 auto v = service.adapter(id);
                 return v && v->connectionState == "FAULTED";
               },
               std::chrono::milliseconds(2000)),
           "HR-D FAULTED on same runtime");
    expect(service.manager().adapter(id) == raw,
           "HR-D did not rematerialize after a returned failure");
    expect(isolationEventCount(service, id) == 0,
           "HR-D no hung-connect isolation");
    const auto sess = sessionOf(service, id);
    expect(sess.failedConnections >= 1, "HR-D failedConnections counted");
    expect(sess.successfulConnections == 0, "HR-D no successfulConnections");
    expect(sess.autoReconnectBackoffMs.count() >= 2000,
           "HR-D backoff doubled after returned failure");

    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    expect(service.manager().adapter(id) == raw,
           "HR-D still the original runtime after extra recovery ticks");
    expect(isolationEventCount(service, id) == 0,
           "HR-D still no isolation after extra ticks");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // HR-E) operator Connect/Reconnect blocked past the automatic isolation
  // threshold is NOT isolated.
  // ---------------------------------------------------------------------
  {
    const std::string id = "hang-hr-e";
    const std::string cfg = tempPath("hr-e-cfg.json");
    const std::string hist = tempPath("hr-e-hist.sqlite");
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    service.start();
    expect(service.upsertAdapterConfig(makeMockTimed(id, 500)).ok, "HR-E upsert");
    auto gate = std::make_shared<HangGate>();
    auto hang = std::make_unique<HangConnectAdapter>(id, gate);
    HangConnectAdapter *raw = hang.get();
    expect(service.manager().addAdapter(std::move(hang)).ok, "HR-E add hang");

    auto started = service.connectAdapter(id);
    expect(started.ok && started.accepted, "HR-E Connect accepted");
    expect(raw->waitConnectEntered(std::chrono::milliseconds(2000)),
           "HR-E operator connect() entered");

    auto recon = service.reconnectAdapter(id);
    expect(recon.ok && recon.accepted, "HR-E Reconnect accepted during operator Connect");
    expect(raw->connectEntered() == 1, "HR-E no second connect() behind operator");

    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    expect(isolationEventCount(service, id) == 0,
           "HR-E operator Connect was not automatically isolated");
    expect(service.manager().adapter(id) == raw,
           "HR-E same runtime after isolation threshold");
    expect(raw->connectCompleted() == 0, "HR-E operator connect still blocked");
    auto v = service.adapter(id);
    expect(!(v && v->connectionState == "CONNECTED"),
           "HR-E replacement mock did not silently connect");

    gate->open();
    expect(waitFor(
               [&]() {
                 auto now = service.adapter(id);
                 return now && now->connectionState == "CONNECTED";
               },
               std::chrono::milliseconds(2000)),
           "HR-E operator Connect completes on the original runtime");
    expect(service.manager().adapter(id) == raw, "HR-E original runtime connected");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  }

  // ---------------------------------------------------------------------
  // HR-F) Disconnect / disable / remove during/after isolation prevent a
  // subsequent automatic reconnect.
  // ---------------------------------------------------------------------
  auto runHrF = [](const char *suffix, const char *mode) {
    const std::string id = std::string("hang-hr-f-") + suffix;
    const std::string cfg = tempPath((std::string("hr-f-") + suffix + "-cfg.json").c_str());
    const std::string hist = tempPath((std::string("hr-f-") + suffix + "-hist.sqlite").c_str());
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
    ApplicationService service(cfg, hist);
    auto gate = std::make_shared<HangGate>();
    GatedConnectAdapter *raw =
        installGatedBeforeStart(service, id, gate, 500, "HR-F");
    service.start();
    expect(raw->waitEnteredAtLeast(2, std::chrono::milliseconds(10000)),
           std::string("HR-F ") + mode + " second connect blocked");
    expect(waitFor(
               [&]() { return isolationEventCount(service, id) >= 1; },
               std::chrono::milliseconds(6000)),
           std::string("HR-F ") + mode + " isolated");

    if (std::string(mode) == "disconnect")
    {
      auto disc = service.disconnectAdapter(id);
      expect(disc.ok && disc.accepted,
             std::string("HR-F ") + mode + " accepted");
    }
    else if (std::string(mode) == "disable")
    {
      auto rec = makeMockTimed(id, 500);
      rec.enabled = false;
      auto disabled = service.upsertAdapterConfig(rec);
      expect(disabled.ok, std::string("HR-F ") + mode + " upsert ok");
    }
    else
    {
      auto removed = service.removeAdapterConfig(id);
      expect(removed.ok, std::string("HR-F ") + mode + " remove ok");
    }

    expect(waitFor(
               [&]() {
                 auto v = service.adapter(id);
                 return !v || v->connectionState != "CONNECTED";
               },
               std::chrono::milliseconds(4000)),
           std::string("HR-F ") + mode + " leaves CONNECTED");

    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(2500))
    {
      auto v = service.adapter(id);
      expect(!(v && v->connectionState == "CONNECTED"),
             std::string("HR-F ") + mode
                 + " must not automatically reconnect");
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    gate->open();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto after = service.adapter(id);
    expect(!(after && after->connectionState == "CONNECTED"),
           std::string("HR-F ") + mode
               + " stays down after old connect returns");

    service.stop();
    ::unlink(cfg.c_str());
    ::unlink(hist.c_str());
  };
  runHrF("disc", "disconnect");
  runHrF("dis", "disable");
  runHrF("rm", "remove");

  if (failures == 0)
  {
    std::cout << "icp_lifecycle_recovery_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_lifecycle_recovery_test: " << failures << " failure(s)"
            << std::endl;
  return 1;
}
