/// P0 regression: configuration isolation.
/// Adapter DELETE / disable / upsert-rematerialize must not block HTTP/API on
/// adapter io_mutex or protocol disconnect while Connect/Recovery is in-flight.
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
#include <memory>
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
  stream << "/tmp/icp-config-iso-" << ::getpid() << "-" << suffix;
  return stream.str();
}

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

bool waitForRuntimeAbsent(
    virtual_factory::icp::ApplicationService &service,
    const std::string &adapterId,
    int timeoutMs)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (service.manager().adapter(adapterId) == nullptr)
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

bool waitForRecoveryInFlight(
    virtual_factory::icp::ApplicationService &service,
    const std::string &adapterId,
    int timeoutMs)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline)
  {
    const auto report = service.diagnosticsReport();
    for (const auto &row : report.adapters)
    {
      if (row.adapter.adapterId != adapterId)
      {
        continue;
      }
      if (row.session.reconnectCount >= 1
          && row.adapter.connectionState != "FAULTED"
          && row.adapter.connectionState != "CONNECTED")
      {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

bool bodyHasAcceptedTrue(const std::string &body)
{
  return body.find("\"accepted\":true") != std::string::npos
      || body.find("\"accepted\": true") != std::string::npos;
}

virtual_factory::icp::AdapterConfigRecord makeOpcUa(
    const std::string &id,
    const std::string &endpoint,
    int timeoutMs,
    const std::string &equipmentId = "EQ-OPC-CFG")
{
  virtual_factory::icp::AdapterConfigRecord opcua;
  opcua.adapterId = id;
  opcua.protocol = "opcua";
  opcua.enabled = true;
  opcua.connection.endpointUrl = endpoint;
  opcua.connection.timeoutMs = timeoutMs;
  virtual_factory::icp::EquipmentMappingRecord eq;
  eq.equipmentId = equipmentId;
  eq.type = "plc";
  virtual_factory::icp::TelemetryMappingRecord tel;
  tel.name = "speed";
  tel.address = "ns=1;s=Speed";
  tel.namespaceIndex = 1;
  eq.telemetry.push_back(tel);
  opcua.equipment.push_back(eq);
  return opcua;
}

virtual_factory::icp::AdapterConfigRecord makeMock(
    const std::string &id, const std::string &equipmentId = "EQ-MOCK-CFG")
{
  virtual_factory::icp::AdapterConfigRecord mock;
  mock.adapterId = id;
  mock.protocol = "mock";
  mock.enabled = true;
  virtual_factory::icp::EquipmentMappingRecord eq;
  eq.equipmentId = equipmentId;
  eq.type = "motor";
  eq.capabilities = {"start", "stop"};
  virtual_factory::icp::TelemetryMappingRecord tel;
  tel.name = "speed";
  tel.unit = "rpm";
  eq.telemetry.push_back(tel);
  mock.equipment.push_back(eq);
  return mock;
}

struct Fixture
{
  BlackholeTcpListener blackhole;
  std::string configPath;
  std::string historyPath;
  std::unique_ptr<virtual_factory::icp::ApplicationService> service;
  std::unique_ptr<virtual_factory::icp::HttpApiServer> api;
  int port{0};

  bool start(const char *suffix)
  {
    if (!this->blackhole.start())
    {
      return false;
    }
    this->configPath = tempPath(suffix) + "-config.json";
    this->historyPath = tempPath(suffix) + "-history.sqlite";
    ::unlink(this->configPath.c_str());
    ::unlink(this->historyPath.c_str());
    this->service = std::make_unique<virtual_factory::icp::ApplicationService>(
        this->configPath, this->historyPath);
    this->service->start();
    this->port = 19100 + (::getpid() % 800) + static_cast<int>(suffix[0] % 17);
    this->api = std::make_unique<virtual_factory::icp::HttpApiServer>(
        *this->service, "", "127.0.0.1", this->port);
    return this->api->start();
  }

  void stop()
  {
    if (this->api)
    {
      this->api->stop();
    }
    if (this->service)
    {
      this->service->stop();
    }
    this->blackhole.stop();
    ::unlink(this->configPath.c_str());
    ::unlink(this->historyPath.c_str());
    ::unlink((this->historyPath + "-wal").c_str());
    ::unlink((this->historyPath + "-shm").c_str());
  }
};

bool seedOpcuaAndMock(Fixture &fx, int opcTimeoutMs)
{
  expect(fx.service
             ->upsertAdapterConfig(makeOpcUa(
                 "opcua-cfg", fx.blackhole.opcUaEndpoint(), opcTimeoutMs))
             .ok,
         "upsert opcua-cfg");
  expect(fx.service->upsertAdapterConfig(makeMock("mock-cfg")).ok, "upsert mock-cfg");
  expect(fx.service->saveConfiguration().ok, "save config");
  expect(fx.service->loadConfiguration().ok, "load materializes");
  return failures == 0;
}

}  // namespace

int main()
{
  using virtual_factory::icp::AdapterConfigRecord;
  using virtual_factory::icp::ApplicationService;

  constexpr int kOpcTimeoutMs = 2500;

  // -------------------------------------------------------------------------
  // 1) DELETE while Connect / recovery I/O is in-flight
  // -------------------------------------------------------------------------
  {
    Fixture fx;
    expect(fx.start("del"), "fixture start (DELETE)");
    if (!seedOpcuaAndMock(fx, kOpcTimeoutMs))
    {
      fx.stop();
      return 1;
    }

    expect(waitForRecoveryInFlight(*fx.service, "opcua-cfg", kOpcTimeoutMs),
           "OPC UA recovery in-flight before DELETE");
    expect(waitForAdapterState(*fx.service, "mock-cfg", "CONNECTED", 500),
           "mock CONNECTED before DELETE");

    httplib::Client client("127.0.0.1", fx.port);
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(2, 0);

    std::atomic<bool> stopProbe{false};
    std::atomic<int> probeOk{0};
    std::atomic<int> probeSlow{0};
    std::thread probe([&]() {
      httplib::Client c("127.0.0.1", fx.port);
      c.set_connection_timeout(1, 0);
      c.set_read_timeout(1, 0);
      while (!stopProbe.load())
      {
        const auto t = std::chrono::steady_clock::now();
        auto res = c.Get("/api/v1/status");
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
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
      }
    });

    const auto tDel = std::chrono::steady_clock::now();
    auto delRes = client.Delete("/api/v1/adapters/opcua-cfg");
    const auto delMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - tDel)
                           .count();
    expect(delRes && delRes->status == 200,
           "HTTP DELETE returns 200: "
               + (delRes ? delRes->body : "no response"));
    expect(delMs < 500,
           "HTTP DELETE does not wait for OPC UA timeout ("
               + std::to_string(delMs) + "ms)");
    if (delRes)
    {
      expect(bodyHasAcceptedTrue(delRes->body),
             "DELETE response includes accepted:true");
    }

    expect(waitForRuntimeAbsent(*fx.service, "opcua-cfg", 200),
           "runtime adapter removed from manager promptly after DELETE");
    expect(fx.service->manager().ownerAdapterId("EQ-OPC-CFG").empty(),
           "equipment ownership cleared after DELETE extract");
    expect(fx.service->catalog().adapter("opcua-cfg") == nullptr,
           "catalog entry removed on DELETE");

    // Stale Connect must not recreate/reconnect after DELETE.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    expect(fx.service->manager().adapter("opcua-cfg") == nullptr,
           "no runtime recreation after DELETE (stale Connect invalidated)");
    expect(waitForAdapterState(*fx.service, "mock-cfg", "CONNECTED", 1000),
           "sibling mock remains CONNECTED after DELETE");
    {
      const auto tCmd = std::chrono::steady_clock::now();
      auto cmd = fx.service->executeEquipmentCommand("EQ-MOCK-CFG", "start", 0.0);
      const auto cmdMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - tCmd)
                             .count();
      expect(cmd.ok, "mock usable after DELETE: " + cmd.message);
      expect(cmdMs < 500, "mock command not blocked by OPC UA teardown");
    }

    // Allow blackhole timeout + teardown to finish without crash/UAF.
    std::this_thread::sleep_for(std::chrono::milliseconds(kOpcTimeoutMs + 500));
    expect(fx.service->manager().adapter("opcua-cfg") == nullptr,
           "opcua remains absent after blackhole timeout window");
    expect(waitForAdapterState(*fx.service, "mock-cfg", "CONNECTED", 1000),
           "mock still CONNECTED after OPC UA teardown settles");

    stopProbe.store(true);
    probe.join();
    expect(probeOk.load() > 0, "GET /status responsive during DELETE teardown");
    expect(probeSlow.load() == 0,
           "no slow GET /status during DELETE ("
               + std::to_string(probeSlow.load()) + " slow)");

    fx.stop();
  }

  // -------------------------------------------------------------------------
  // 2) Disable while RecoveryConnect pending / in-flight
  // -------------------------------------------------------------------------
  {
    Fixture fx;
    expect(fx.start("dis"), "fixture start (disable)");
    if (!seedOpcuaAndMock(fx, kOpcTimeoutMs))
    {
      fx.stop();
      return 1;
    }

    expect(waitForRecoveryInFlight(*fx.service, "opcua-cfg", kOpcTimeoutMs),
           "OPC UA recovery in-flight before disable");
    expect(waitForAdapterState(*fx.service, "mock-cfg", "CONNECTED", 500),
           "mock CONNECTED before disable");

    AdapterConfigRecord disabled =
        makeOpcUa("opcua-cfg", fx.blackhole.opcUaEndpoint(), kOpcTimeoutMs);
    disabled.enabled = false;

    httplib::Client client("127.0.0.1", fx.port);
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(2, 0);

    const auto tDis = std::chrono::steady_clock::now();
    // PUT via ApplicationService path (same as HTTP handler).
    auto disRes = fx.service->upsertAdapterConfig(disabled);
    const auto disMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - tDis)
                           .count();
    expect(disRes.ok, "disable upsert ok: " + disRes.message);
    expect(disRes.accepted, "disable upsert accepted async teardown");
    expect(disMs < 500,
           "disable does not wait for OPC UA timeout ("
               + std::to_string(disMs) + "ms)");

    expect(waitForRuntimeAbsent(*fx.service, "opcua-cfg", 200),
           "runtime extracted promptly on disable");
    expect(fx.service->catalog().adapter("opcua-cfg") != nullptr,
           "catalog retains disabled adapter");
    expect(fx.service->catalog().adapter("opcua-cfg")->enabled == false,
           "catalog marks adapter disabled");

    // Stale RecoveryConnect must not reconnect after disable.
    std::this_thread::sleep_for(std::chrono::milliseconds(kOpcTimeoutMs + 500));
    expect(fx.service->manager().adapter("opcua-cfg") == nullptr,
           "disabled adapter does not regain runtime via stale recovery");
    expect(waitForAdapterState(*fx.service, "mock-cfg", "CONNECTED", 1000),
           "mock unaffected by disable");

    auto status = client.Get("/api/v1/status");
    expect(status && status->status == 200, "GET /status after disable");

    fx.stop();
  }

  // -------------------------------------------------------------------------
  // 3) Upsert rematerialize while Connect in-flight
  // -------------------------------------------------------------------------
  {
    Fixture fx;
    expect(fx.start("ups"), "fixture start (upsert)");
    if (!seedOpcuaAndMock(fx, kOpcTimeoutMs))
    {
      fx.stop();
      return 1;
    }

    expect(waitForRecoveryInFlight(*fx.service, "opcua-cfg", kOpcTimeoutMs),
           "OPC UA recovery in-flight before rematerialize");
    expect(waitForAdapterState(*fx.service, "mock-cfg", "CONNECTED", 500),
           "mock CONNECTED before rematerialize");

    // Change NodeId so rematerialize is required (same endpoint / timeout).
    AdapterConfigRecord updated =
        makeOpcUa("opcua-cfg", fx.blackhole.opcUaEndpoint(), kOpcTimeoutMs);
    updated.equipment.front().telemetry.front().address = "ns=1;s=Speed-v2";

    const virtual_factory::IndustrialAdapter *before =
        fx.service->manager().adapter("opcua-cfg");
    expect(before != nullptr, "runtime present before rematerialize");
    const virtual_factory::IndustrialAdapter *beforePtr = before;

    const auto tUp = std::chrono::steady_clock::now();
    auto upRes = fx.service->upsertAdapterConfig(updated);
    const auto upMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - tUp)
                          .count();
    expect(upRes.ok, "rematerialize upsert ok: " + upRes.message);
    expect(upRes.accepted, "rematerialize accepted async teardown");
    expect(upMs < 500,
           "rematerialize does not wait for OPC UA timeout ("
               + std::to_string(upMs) + "ms)");

    const virtual_factory::IndustrialAdapter *after =
        fx.service->manager().adapter("opcua-cfg");
    expect(after != nullptr, "new runtime present after rematerialize");
    expect(after != beforePtr,
           "rematerialize installed a new runtime instance");
    expect(fx.service->manager().ownerAdapterId("EQ-OPC-CFG").empty()
               || fx.service->manager().ownerAdapterId("EQ-OPC-CFG")
                      == "opcua-cfg",
           "equipment ownership not stale on foreign adapter");

    expect(waitForAdapterState(*fx.service, "mock-cfg", "CONNECTED", 1000),
           "mock remains CONNECTED during rematerialize");

    // New instance may connect/fault independently; old must not keep ownership.
    std::this_thread::sleep_for(std::chrono::milliseconds(kOpcTimeoutMs + 500));
    expect(fx.service->manager().adapter("opcua-cfg") != nullptr,
           "rematerialized runtime still present after timeout window");
    expect(waitForAdapterState(*fx.service, "mock-cfg", "CONNECTED", 1000),
           "mock still CONNECTED after rematerialize settles");

    fx.stop();
  }

  // -------------------------------------------------------------------------
  // 4) DELETE followed by stale queued Connect (ApplicationService path)
  // -------------------------------------------------------------------------
  {
    Fixture fx;
    expect(fx.start("stale"), "fixture start (stale Connect)");
    expect(fx.service
               ->upsertAdapterConfig(makeOpcUa(
                   "opcua-cfg",
                   fx.blackhole.opcUaEndpoint(),
                   kOpcTimeoutMs,
                   "EQ-OPC-STALE"))
               .ok,
           "upsert opcua stale case");
    expect(fx.service->upsertAdapterConfig(makeMock("mock-cfg", "EQ-MOCK-STALE")).ok,
           "upsert mock stale case");
    expect(fx.service->saveConfiguration().ok, "save");
    expect(fx.service->loadConfiguration().ok, "load");

    expect(waitForRecoveryInFlight(*fx.service, "opcua-cfg", kOpcTimeoutMs),
           "recovery in-flight before connect+delete race");

    // Explicit Connect (may coalesce with recovery) then immediate DELETE.
    auto conn = fx.service->connectAdapter("opcua-cfg");
    expect(conn.ok && conn.accepted, "connect accepted before DELETE");

    const auto tDel = std::chrono::steady_clock::now();
    auto removed = fx.service->removeAdapterConfig("opcua-cfg");
    const auto delMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - tDel)
                           .count();
    expect(removed.ok && removed.accepted, "removeAdapterConfig accepted");
    expect(delMs < 500, "removeAdapterConfig prompt (" + std::to_string(delMs) + "ms)");

    std::this_thread::sleep_for(std::chrono::milliseconds(kOpcTimeoutMs + 500));
    expect(fx.service->manager().adapter("opcua-cfg") == nullptr,
           "stale Connect does not recreate after DELETE");
    expect(fx.service->manager().ownerAdapterId("EQ-OPC-STALE").empty(),
           "stale ownership cleared");
    expect(waitForAdapterState(*fx.service, "mock-cfg", "CONNECTED", 2000),
           "mock ok after stale Connect+DELETE");

    fx.stop();
  }

  // -------------------------------------------------------------------------
  // 5) Disable followed by stale RecoveryConnect
  // -------------------------------------------------------------------------
  {
    Fixture fx;
    expect(fx.start("recv"), "fixture start (stale recovery)");
    if (!seedOpcuaAndMock(fx, kOpcTimeoutMs))
    {
      fx.stop();
      return 1;
    }
    expect(waitForRecoveryInFlight(*fx.service, "opcua-cfg", kOpcTimeoutMs),
           "recovery in-flight before disable+stale");

    AdapterConfigRecord disabled =
        makeOpcUa("opcua-cfg", fx.blackhole.opcUaEndpoint(), kOpcTimeoutMs);
    disabled.enabled = false;
    const auto tDis = std::chrono::steady_clock::now();
    auto dis = fx.service->upsertAdapterConfig(disabled);
    const auto disMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - tDis)
                           .count();
    expect(dis.ok && dis.accepted, "disable accepted with recovery pending");
    expect(disMs < 500, "disable prompt with recovery pending");

    std::this_thread::sleep_for(std::chrono::milliseconds(kOpcTimeoutMs + 800));
    expect(fx.service->manager().adapter("opcua-cfg") == nullptr,
           "stale RecoveryConnect does not rematerialize disabled adapter");
    expect(waitForAdapterState(*fx.service, "mock-cfg", "CONNECTED", 1000),
           "mock ok after disable+stale recovery");

    fx.stop();
  }

  if (failures == 0)
  {
    std::cout << "icp_configuration_isolation_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_configuration_isolation_test: " << failures << " failure(s)"
            << std::endl;
  return 1;
}
