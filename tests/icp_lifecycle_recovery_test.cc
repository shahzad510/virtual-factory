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

  if (failures == 0)
  {
    std::cout << "icp_lifecycle_recovery_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_lifecycle_recovery_test: " << failures << " failure(s)"
            << std::endl;
  return 1;
}
