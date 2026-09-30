/// P1 regression: protocol command isolation.
/// Commands serialize with poll/connect/disconnect on the owning adapter's
/// io_mutex, never race a raw Equipment* after unlock, and never block peers.
#include <virtual_factory/equipment/GenericEquipment.hh>
#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/icp/LiveStateCache.hh>
#include <virtual_factory/icp/PollScheduler.hh>
#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/icp/app/HttpApiServer.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>
#include <virtual_factory/industrial/MockIndustrialAdapter.hh>

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
  stream << "/tmp/icp-cmd-iso-" << ::getpid() << "-" << suffix;
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
  eq.equipmentId = "EQ-OPC-CMD";
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
    const std::string &id, const std::string &equipmentId)
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

/// Test-only adapter that detects concurrent protocol-client access.
class ConcurrentProbeAdapter : public virtual_factory::IndustrialAdapter
{
public:
  class ProbeEquipment : public virtual_factory::Equipment
  {
  public:
    ProbeEquipment(ConcurrentProbeAdapter *adapter, std::string id)
        : adapter_(adapter), inner_(std::move(id), "probe")
    {
      this->inner_.addCapability("start");
      this->inner_.addCapability("stop");
      this->inner_.addCapability("block");
    }

    std::string id() const override
    {
      return this->inner_.id();
    }
    std::string type() const override
    {
      return this->inner_.type();
    }
    virtual_factory::OperationalState operationalState() const override
    {
      return this->inner_.operationalState();
    }
    bool fault() const override
    {
      return this->inner_.fault();
    }
    std::vector<std::string> capabilities() const override
    {
      return this->inner_.capabilities();
    }
    std::vector<std::string> commands() const override
    {
      return {"start", "stop", "block"};
    }
    virtual_factory::CommandResult execute(
        const std::string &command, double parameter) override
    {
      (void)parameter;
      this->adapter_->enterIo("execute");
      if (command == "block")
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        this->adapter_->leaveIo();
        return {true, "blocked"};
      }
      auto result = this->inner_.execute(command, parameter);
      this->adapter_->leaveIo();
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

  private:
    ConcurrentProbeAdapter *adapter_;
    virtual_factory::GenericEquipment inner_;
  };

  explicit ConcurrentProbeAdapter(std::string id)
      : id_(std::move(id)), equipment_(this, "EQ-PROBE")
  {
  }

  std::string id() const override
  {
    return this->id_;
  }
  std::string protocol() const override
  {
    return "probe";
  }
  virtual_factory::ConnectionState connectionState() const override
  {
    return this->state_;
  }
  std::string lastError() const override
  {
    return this->lastError_;
  }

  bool connect() override
  {
    this->enterIo("connect");
    if (this->connectDelayMs_ > 0)
    {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(this->connectDelayMs_));
    }
    this->state_ = virtual_factory::ConnectionState::Connected;
    this->leaveIo();
    return true;
  }

  void disconnect() override
  {
    this->enterIo("disconnect");
    if (this->disconnectDelayMs_ > 0)
    {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(this->disconnectDelayMs_));
    }
    this->state_ = virtual_factory::ConnectionState::Disconnected;
    this->leaveIo();
  }

  std::vector<virtual_factory::Equipment *> equipment() override
  {
    if (this->state_ == virtual_factory::ConnectionState::Disconnected)
    {
      return {};
    }
    return {&this->equipment_};
  }

  virtual_factory::Equipment *equipmentById(const std::string &id) override
  {
    if (this->state_ == virtual_factory::ConnectionState::Disconnected)
    {
      return nullptr;
    }
    return id == this->equipment_.id() ? &this->equipment_ : nullptr;
  }

  void poll() override
  {
    this->enterIo("poll");
    ++this->pollCount_;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    this->leaveIo();
  }

  void setConnectDelayMs(int ms)
  {
    this->connectDelayMs_ = ms;
  }

  void setDisconnectDelayMs(int ms)
  {
    this->disconnectDelayMs_ = ms;
  }

  int concurrentViolations() const
  {
    return this->concurrentViolations_.load();
  }

  int pollCount() const
  {
    return this->pollCount_.load();
  }

private:
  void enterIo(const char * /*label*/)
  {
    const int prior = this->inIo_.fetch_add(1);
    if (prior != 0)
    {
      ++this->concurrentViolations_;
    }
  }

  void leaveIo()
  {
    this->inIo_.fetch_sub(1);
  }

  std::string id_;
  ProbeEquipment equipment_;
  virtual_factory::ConnectionState state_{
      virtual_factory::ConnectionState::Disconnected};
  std::string lastError_;
  int connectDelayMs_{0};
  int disconnectDelayMs_{0};
  std::atomic<int> inIo_{0};
  std::atomic<int> concurrentViolations_{0};
  std::atomic<int> pollCount_{0};
};

}  // namespace

int main()
{
  using virtual_factory::icp::AdapterManager;
  using virtual_factory::icp::ApplicationService;
  using virtual_factory::icp::HttpApiServer;
  using virtual_factory::icp::LiveStateCache;
  using virtual_factory::icp::PollScheduler;

  constexpr int kOpcTimeoutMs = 2500;

  // -----------------------------------------------------------------------
  // 1) COMMAND vs POLL — same adapter (no concurrent protocol access)
  // -----------------------------------------------------------------------
  {
    AdapterManager manager;
    LiveStateCache cache;
    auto probe = std::make_unique<ConcurrentProbeAdapter>("probe-a");
    ConcurrentProbeAdapter *raw = probe.get();
    expect(manager.addAdapter(std::move(probe)).ok, "add probe-a");
    expect(manager.connectAdapter("probe-a").ok, "connect probe-a");

    PollScheduler scheduler(manager, cache, std::chrono::milliseconds(20));
    scheduler.start();

    // Let a few polls run, then hold io_mutex via a blocking command.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    const int pollsBefore = raw->pollCount();

    const auto t0 = std::chrono::steady_clock::now();
    auto cmd = manager.executeEquipmentCommand("EQ-PROBE", "block", 0.0);
    const auto cmdMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - t0)
                           .count();
    expect(cmd.equipmentFound && cmd.adapterConnected && cmd.command.accepted,
           "block command accepted under owner io_mutex");
    expect(cmdMs >= 150, "block command actually blocked");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler.stop();

    expect(raw->concurrentViolations() == 0,
           "no concurrent poll/execute on same adapter ("
               + std::to_string(raw->concurrentViolations()) + ")");
    expect(raw->pollCount() >= pollsBefore,
           "poll continued around command (non-blocking skip or after)");
    manager.removeAdapter("probe-a");
  }

  // -----------------------------------------------------------------------
  // 2) COMMAND vs LIFECYCLE — same adapter serialization
  // -----------------------------------------------------------------------
  {
    AdapterManager manager;
    auto probe = std::make_unique<ConcurrentProbeAdapter>("probe-life");
    ConcurrentProbeAdapter *raw = probe.get();
    expect(manager.addAdapter(std::move(probe)).ok, "add probe-life");
    expect(manager.connectAdapter("probe-life").ok, "connect probe-life");

    std::atomic<bool> blockDone{false};
    std::thread blocker([&]() {
      auto cmd = manager.executeEquipmentCommand("EQ-PROBE", "block", 0.0);
      expect(cmd.command.accepted, "blocking command held io_mutex");
      blockDone.store(true);
    });

    // Command holds io_mutex; disconnect must serialize behind it.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto t0 = std::chrono::steady_clock::now();
    expect(manager.disconnectAdapter("probe-life").ok, "disconnect after command");
    const auto discMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
    blocker.join();

    expect(blockDone.load(), "blocking command finished");
    expect(raw->concurrentViolations() == 0,
           "command/disconnect serialized on same adapter");
    expect(discMs >= 100,
           "disconnect waited on owner io_mutex held by command ("
               + std::to_string(discMs) + "ms)");
    manager.removeAdapter("probe-life");
  }

  // -----------------------------------------------------------------------
  // 3) COMMAND vs COMMAND — different adapters do not share a global lock
  // -----------------------------------------------------------------------
  {
    AdapterManager manager;
    auto mockA = std::make_unique<virtual_factory::MockIndustrialAdapter>("mock-x");
    mockA->addDevice("EQ-X", "motor");
    mockA->addCapability("EQ-X", "start");
    mockA->addCapability("EQ-X", "stop");
    auto mockB = std::make_unique<virtual_factory::MockIndustrialAdapter>("mock-y");
    mockB->addDevice("EQ-Y", "motor");
    mockB->addCapability("EQ-Y", "start");
    mockB->addCapability("EQ-Y", "stop");
    expect(manager.addAdapter(std::move(mockA)).ok, "add mock-x");
    expect(manager.addAdapter(std::move(mockB)).ok, "add mock-y");
    expect(manager.connectAdapter("mock-x").ok, "connect mock-x");
    expect(manager.connectAdapter("mock-y").ok, "connect mock-y");

    std::atomic<int> done{0};
    const auto t0 = std::chrono::steady_clock::now();
    std::thread t1([&]() {
      auto r = manager.executeEquipmentCommand("EQ-X", "start", 0.0);
      expect(r.command.accepted, "parallel command X");
      done.fetch_add(1);
    });
    std::thread t2([&]() {
      auto r = manager.executeEquipmentCommand("EQ-Y", "start", 0.0);
      expect(r.command.accepted, "parallel command Y");
      done.fetch_add(1);
    });
    t1.join();
    t2.join();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    expect(done.load() == 2, "both cross-adapter commands finished");
    expect(ms < 500, "cross-adapter commands not globally serialized ("
                         + std::to_string(ms) + "ms)");
    manager.removeAdapter("mock-x");
    manager.removeAdapter("mock-y");
  }

  // -----------------------------------------------------------------------
  // 4–8) ApplicationService / HTTP: blackhole OPC UA + mock command
  // -----------------------------------------------------------------------
  {
    BlackholeTcpListener blackhole;
    expect(blackhole.start(), "blackhole starts");

    const std::string configPath = tempPath("config.json");
    const std::string historyPath = tempPath("history.sqlite");
    ::unlink(configPath.c_str());
    ::unlink(historyPath.c_str());

    ApplicationService service(configPath, historyPath);
    service.start();
    expect(service.upsertAdapterConfig(
                       makeOpcUa("opcua-cmd", blackhole.opcUaEndpoint(), kOpcTimeoutMs))
               .ok,
           "upsert opcua");
    expect(service.upsertAdapterConfig(makeMock("mock-cmd", "EQ-MOCK-CMD")).ok,
           "upsert mock");
    expect(service.saveConfiguration().ok, "save");
    expect(service.loadConfiguration().ok, "load");

    expect(waitForRecoveryInFlight(service, "opcua-cmd", kOpcTimeoutMs),
           "OPC UA recovery in-flight");
    expect(waitForAdapterState(service, "mock-cmd", "CONNECTED", 500),
           "mock CONNECTED while OPC UA blocked");

    const int port = 19200 + (::getpid() % 700);
    HttpApiServer api(service, "", "127.0.0.1", port);
    expect(api.start(), "HTTP starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(2, 0);

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

    // Cross-adapter: mock command while OPC UA holds its io_mutex.
    const auto tCmd = std::chrono::steady_clock::now();
    auto cmd = service.executeEquipmentCommand("EQ-MOCK-CMD", "start", 0.0);
    const auto cmdMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - tCmd)
                           .count();
    expect(cmd.ok, "mock command during OPC UA blackhole: " + cmd.message);
    expect(cmdMs < 500,
           "mock command does not wait on OPC UA io_mutex ("
               + std::to_string(cmdMs) + "ms)");

    expect(service.manager().ownerAdapterId("EQ-MOCK-CMD") == "mock-cmd",
           "ownership index routes mock equipment");
    expect(service.manager().ownerAdapterId("EQ-OPC-CMD").empty()
               || service.manager().ownerAdapterId("EQ-OPC-CMD") == "opcua-cmd",
           "opcua ownership not foreign");

    // HTTP command path remains synchronous.
    const auto tHttp = std::chrono::steady_clock::now();
    auto httpCmd = client.Post(
        "/api/v1/equipment/EQ-MOCK-CMD/command",
        R"({"command":"stop"})",
        "application/json");
    const auto httpMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - tHttp)
                            .count();
    expect(httpCmd && httpCmd->status == 200, "HTTP command 200");
    expect(httpMs < 500, "HTTP mock command responsive");

    // Failure on mock must not affect opcua lifecycle path.
    auto bad = service.executeEquipmentCommand("EQ-MOCK-CMD", "not_real", 0.0);
    expect(!bad.ok, "unknown command fails on owner only");
    expect(waitForAdapterState(service, "mock-cmd", "CONNECTED", 500),
           "mock remains CONNECTED after failed command");

    // DELETE race: extract while peers stay up; command on deleted eq fails
    // safely (no UAF).
    expect(waitForRecoveryInFlight(service, "opcua-cmd", kOpcTimeoutMs)
               || service.adapter("opcua-cmd")->connectionState == "FAULTED"
               || service.adapter("opcua-cmd")->connectionState == "DISCONNECTED",
           "opcua still in lifecycle or settled before DELETE");

    // Ensure mock still commandable, then delete mock under load.
    const auto tDel = std::chrono::steady_clock::now();
    auto removed = service.removeAdapterConfig("mock-cmd");
    const auto delMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - tDel)
                           .count();
    expect(removed.ok, "DELETE mock accepted");
    expect(delMs < 500, "DELETE mock prompt");

    auto afterDel =
        service.executeEquipmentCommand("EQ-MOCK-CMD", "start", 0.0);
    expect(!afterDel.ok, "command after DELETE fails cleanly");
    expect(service.manager().ownerAdapterId("EQ-MOCK-CMD").empty(),
           "ownership cleared after DELETE");

    // Sibling OPC UA path must not crash; status stays responsive.
    auto status = client.Get("/api/v1/status");
    expect(status && status->status == 200, "GET /status after DELETE");

    stopProbe.store(true);
    probe.join();
    expect(probeOk.load() > 0, "GET /status responsive during commands");
    expect(probeSlow.load() == 0,
           "no slow GET /status (" + std::to_string(probeSlow.load()) + ")");

    api.stop();
    service.stop();
    blackhole.stop();
    ::unlink(configPath.c_str());
    ::unlink(historyPath.c_str());
    ::unlink((historyPath + "-wal").c_str());
    ::unlink((historyPath + "-shm").c_str());
  }

  // -----------------------------------------------------------------------
  // Rematerialize race: command must not use extracted instance after enroll
  // clear; new runtime is a new enrollment.
  // -----------------------------------------------------------------------
  {
    AdapterManager manager;
    auto mock = std::make_unique<virtual_factory::MockIndustrialAdapter>("mock-rm");
    mock->addDevice("EQ-RM", "motor");
    mock->addCapability("EQ-RM", "start");
    expect(manager.addAdapter(std::move(mock)).ok, "add mock-rm");
    expect(manager.connectAdapter("mock-rm").ok, "connect mock-rm");
    expect(manager.executeEquipmentCommand("EQ-RM", "start", 0.0).command.accepted,
           "command before rematerialize");

    auto extracted = manager.extractAdapter("mock-rm");
    expect(extracted.adapter != nullptr, "extracted old runtime");
    auto cmdStale =
        manager.executeEquipmentCommand("EQ-RM", "start", 0.0);
    expect(!cmdStale.equipmentFound,
           "command fails after extract (ownership cleared)");

    auto replacement =
        std::make_unique<virtual_factory::MockIndustrialAdapter>("mock-rm");
    replacement->addDevice("EQ-RM", "motor");
    replacement->addCapability("EQ-RM", "start");
    expect(manager.addAdapter(std::move(replacement)).ok, "add replacement");
    expect(manager.connectAdapter("mock-rm").ok, "connect replacement");
    expect(manager.executeEquipmentCommand("EQ-RM", "start", 0.0).command.accepted,
           "command targets rematerialized adapter");

    // Tear down extracted under its io_mutex (P0 semantics).
    if (extracted.io_mutex)
    {
      std::lock_guard<std::mutex> io(*extracted.io_mutex);
      extracted.adapter->disconnect();
    }
    manager.removeAdapter("mock-rm");
  }

  if (failures == 0)
  {
    std::cout << "icp_command_isolation_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_command_isolation_test: " << failures << " failure(s)"
            << std::endl;
  return 1;
}
