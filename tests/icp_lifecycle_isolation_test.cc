/// Regression: per-adapter lifecycle isolation.
/// Slow OPC UA connect must not stall mock connect, mock polling, or HTTP GETs.
#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/icp/app/HttpApiServer.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include <httplib.h>

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
  stream << "/tmp/icp-lifecycle-" << ::getpid() << "-" << suffix;
  return stream.str();
}

/// TCP listener that completes the handshake into the accept queue but never
/// accepts — OPC UA Hello waits until the client timeout (deterministic blackhole).
class BlackholeTcpListener
{
public:
  bool start()
  {
    this->fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (this->fd_ < 0)
    {
      return false;
    }
    int yes = 1;
    ::setsockopt(this->fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(this->fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
      this->closeFd();
      return false;
    }
    socklen_t len = sizeof(addr);
    if (::getsockname(this->fd_, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
    {
      this->closeFd();
      return false;
    }
    this->port_ = ntohs(addr.sin_port);
    // Backlog large enough for one SYN so the client TCP handshake completes
    // while the application never accepts — open62541 then waits for Hello ACK.
    if (::listen(this->fd_, 8) != 0)
    {
      this->closeFd();
      return false;
    }
    return this->port_ != 0;
  }

  void stop()
  {
    this->closeFd();
  }

  ~BlackholeTcpListener()
  {
    this->stop();
  }

  std::uint16_t port() const
  {
    return this->port_;
  }

  std::string opcUaEndpoint() const
  {
    return "opc.tcp://127.0.0.1:" + std::to_string(this->port_);
  }

private:
  void closeFd()
  {
    if (this->fd_ >= 0)
    {
      ::close(this->fd_);
      this->fd_ = -1;
    }
  }

  int fd_{-1};
  std::uint16_t port_{0};
};

bool waitForAdapterState(
    virtual_factory::icp::ApplicationService &service,
    const std::string &adapterId,
    const std::string &state,
    int timeoutMs)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline)
  {
    auto view = service.adapter(adapterId);
    if (view && view->connectionState == state)
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
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
  eq.equipmentId = "EQ-OPC-BH";
  eq.type = "plc";
  virtual_factory::icp::TelemetryMappingRecord tel;
  tel.name = "speed";
  tel.address = "ns=1;s=Speed";
  tel.namespaceIndex = 1;
  eq.telemetry.push_back(tel);
  opcua.equipment.push_back(eq);
  return opcua;
}

virtual_factory::icp::AdapterConfigRecord makeMock(const std::string &id)
{
  virtual_factory::icp::AdapterConfigRecord mock;
  mock.adapterId = id;
  mock.protocol = "mock";
  mock.enabled = true;
  virtual_factory::icp::EquipmentMappingRecord eq;
  eq.equipmentId = "EQ-MOCK-BH";
  eq.type = "motor";
  eq.capabilities = {"start", "stop"};
  virtual_factory::icp::TelemetryMappingRecord tel;
  tel.name = "speed";
  tel.unit = "rpm";
  eq.telemetry.push_back(tel);
  mock.equipment.push_back(eq);
  return mock;
}

}  // namespace

int main()
{
  using virtual_factory::icp::ApplicationService;
  using virtual_factory::icp::HttpApiServer;

  constexpr int kOpcTimeoutMs = 2500;

  BlackholeTcpListener blackhole;
  expect(blackhole.start(), "blackhole TCP listener starts");
  if (blackhole.port() == 0)
  {
    return 1;
  }

  const std::string configPath = tempPath("config.json");
  const std::string historyPath = tempPath("history.sqlite");
  ::unlink(configPath.c_str());
  ::unlink(historyPath.c_str());

  ApplicationService service(configPath, historyPath);
  service.start();

  // OPC UA listed first so a serial lifecycle design would delay mock.
  expect(service.upsertAdapterConfig(
                     makeOpcUa("opcua-bh", blackhole.opcUaEndpoint(), kOpcTimeoutMs))
             .ok,
         "upsert opcua-bh");
  expect(service.upsertAdapterConfig(makeMock("mock-bh")).ok, "upsert mock-bh");
  expect(service.saveConfiguration().ok, "save config");

  // Materialize + arm recovery without blocking on OPC UA.
  expect(service.loadConfiguration().ok, "reload materializes adapters");

  const auto t0 = std::chrono::steady_clock::now();

  // Explicit HTTP Connect on unreachable OPC UA must return immediately.
  const int port = 19000 + (::getpid() % 1000);
  HttpApiServer api(service, "", "127.0.0.1", port);
  expect(api.start(), "HTTP API starts");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(2, 0);

  const auto tConnect0 = std::chrono::steady_clock::now();
  auto connectRes = client.Post("/api/v1/adapters/opcua-bh/connect");
  const auto connectMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - tConnect0)
                             .count();
  expect(connectRes && connectRes->status == 200,
         "POST connect opcua-bh accepted: "
             + (connectRes ? connectRes->body : "no response"));
  expect(connectMs < 500,
         "HTTP Connect returns without waiting for OPC UA timeout ("
             + std::to_string(connectMs) + "ms)");
  if (connectRes)
  {
    expect(connectRes->body.find("\"accepted\":true") != std::string::npos
               || connectRes->body.find("\"accepted\": true") != std::string::npos,
           "connect response includes accepted:true");
  }

  // While OPC UA connect is in-flight, sibling GETs must stay responsive.
  std::atomic<bool> stopProbe{false};
  std::atomic<int> probeOk{0};
  std::atomic<int> probeSlow{0};
  std::thread probe([&]() {
    httplib::Client c("127.0.0.1", port);
    c.set_connection_timeout(1, 0);
    c.set_read_timeout(1, 0);
    while (!stopProbe.load())
    {
      const auto t = std::chrono::steady_clock::now();
      auto res = c.Get("/api/v1/adapters");
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t)
                          .count();
      if (res && res->status == 200 && ms < 200)
      {
        ++probeOk;
      }
      else if (ms >= 200)
      {
        ++probeSlow;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });

  // Mock must connect without waiting for the OPC UA timeout window.
  const bool mockConnected =
      waitForAdapterState(service, "mock-bh", "CONNECTED", kOpcTimeoutMs - 400);
  const auto mockMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
  expect(mockConnected, "mock CONNECTED independently of OPC UA blackhole");
  expect(mockMs < kOpcTimeoutMs,
         "mock connected before OPC UA timeout elapsed (" + std::to_string(mockMs)
             + "ms)");

  // Mock remains usable (command) while OPC UA is still connecting/faulting.
  auto cmd = service.executeEquipmentCommand("EQ-MOCK-BH", "start", 0.0);
  expect(cmd.ok, "mock command usable during OPC UA lifecycle: " + cmd.message);

  // Polling of mock continues: telemetry should refresh while OPC UA blocked.
  double speedBefore = -1.0;
  {
    auto snap = service.equipmentById("EQ-MOCK-BH");
    expect(snap.has_value(), "mock equipment present");
    if (snap && !snap->telemetry.empty())
    {
      speedBefore = snap->telemetry.front().value;
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  {
    auto snap = service.equipmentById("EQ-MOCK-BH");
    expect(snap.has_value() && !snap->telemetry.empty(), "mock telemetry after poll");
    if (snap && !snap->telemetry.empty() && speedBefore >= 0.0)
    {
      expect(snap->telemetry.front().value != speedBefore
                 || snap->communicationState
                        == virtual_factory::ConnectionState::Connected,
             "mock polling continues (connected + telemetry path live)");
    }
  }

  // Let OPC UA finish its bounded timeout → FAULTED, without stalling siblings.
  expect(waitForAdapterState(service, "opcua-bh", "FAULTED", kOpcTimeoutMs + 3000),
         "opcua-bh reaches FAULTED after blackhole timeout");
  expect(waitForAdapterState(service, "mock-bh", "CONNECTED", 1000),
         "mock remains CONNECTED after OPC UA faults");

  stopProbe.store(true);
  probe.join();
  expect(probeOk.load() > 0, "HTTP GET /adapters remained responsive during OPC UA connect");
  expect(probeSlow.load() == 0,
         "no slow sibling GET responses during OPC UA connect ("
             + std::to_string(probeSlow.load()) + " slow)");

  // Explicit Disconnect cancels/invalidates queued recovery for that adapter.
  expect(service.disconnectAdapter("opcua-bh").ok, "disconnect opcua-bh accepted");
  expect(waitForAdapterState(service, "opcua-bh", "DISCONNECTED", 5000),
         "opcua-bh DISCONNECTED after explicit disconnect");

  // Wait longer than one recovery backoff window; must stay DISCONNECTED.
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  {
    auto view = service.adapter("opcua-bh");
    expect(view && view->connectionState == "DISCONNECTED",
           "queued recovery does not reconnect after explicit Disconnect");
  }
  expect(waitForAdapterState(service, "mock-bh", "CONNECTED", 1000),
         "mock unaffected by opcua disconnect");

  // HTTP Reconnect on blackhole: accept quickly; sibling GET stays responsive.
  const auto tRe0 = std::chrono::steady_clock::now();
  auto recon = client.Post("/api/v1/adapters/opcua-bh/reconnect");
  const auto reconMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - tRe0)
                           .count();
  expect(recon && recon->status == 200, "POST reconnect accepted");
  expect(reconMs < 500, "HTTP Reconnect returns without OPC UA timeout wait");

  auto getDuring = client.Get("/api/v1/status");
  expect(getDuring && getDuring->status == 200,
         "GET /status responsive during reconnect lifecycle");

  expect(waitForAdapterState(service, "opcua-bh", "FAULTED", kOpcTimeoutMs + 3000),
         "reconnect to blackhole eventually FAULTED");

  // Clean shutdown: no hang / crash after draining lifecycle workers.
  service.disconnectAdapter("mock-bh");
  expect(waitForAdapterState(service, "mock-bh", "DISCONNECTED", 5000),
         "mock disconnect settles");
  api.stop();
  service.stop();
  blackhole.stop();

  ::unlink(configPath.c_str());
  ::unlink(historyPath.c_str());
  ::unlink((historyPath + "-wal").c_str());
  ::unlink((historyPath + "-shm").c_str());

  if (failures == 0)
  {
    std::cout << "icp_lifecycle_isolation_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_lifecycle_isolation_test: " << failures << " failure(s)"
            << std::endl;
  return 1;
}
