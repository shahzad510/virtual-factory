/// P2 regression: shutdown isolation (protocol-neutral).
/// ApplicationService::stop must return within kLifecycleShutdownGrace even when
/// adapters hang in protocol I/O. One hung adapter must not block peers.
#include <virtual_factory/equipment/GenericEquipment.hh>
#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/icp/LifecycleExecutor.hh>
#include <virtual_factory/icp/LiveStateCache.hh>
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
  stream << "/tmp/icp-shutdown-" << ::getpid() << "-" << suffix;
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
    const std::string &id,
    const std::string &endpoint,
    int timeoutMs,
    const std::string &equipmentId)
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

/// Adapter whose connect/disconnect block until HangGate is released.
class HungAdapter : public virtual_factory::IndustrialAdapter
{
public:
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

  class Eq : public virtual_factory::Equipment
  {
  public:
    explicit Eq(std::string id) : inner_(std::move(id), "hung")
    {
      this->inner_.addCapability("start");
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
      return {"start", "block"};
    }
    virtual_factory::CommandResult execute(
        const std::string &command, double) override
    {
      if (command == "block" && this->gate_)
      {
        this->gate_->wait();
        return {true, "blocked"};
      }
      return this->inner_.execute(command, 0.0);
    }
    std::vector<virtual_factory::TelemetryPoint> telemetry() const override
    {
      return this->inner_.telemetry();
    }
    virtual_factory::EquipmentStatus status() const override
    {
      return this->inner_.status();
    }

    void setGate(std::shared_ptr<HangGate> gate)
    {
      this->gate_ = std::move(gate);
    }

  private:
    virtual_factory::GenericEquipment inner_;
    std::shared_ptr<HangGate> gate_;
  };

  HungAdapter(
      std::string id,
      std::string equipmentId,
      std::shared_ptr<HangGate> gate,
      bool hangConnect,
      bool hangDisconnect)
      : id_(std::move(id))
      , gate_(std::move(gate))
      , hangConnect_(hangConnect)
      , hangDisconnect_(hangDisconnect)
      , equipment_(std::move(equipmentId))
  {
    this->equipment_.setGate(this->gate_);
  }

  std::string id() const override
  {
    return this->id_;
  }
  std::string protocol() const override
  {
    return "hung";
  }
  virtual_factory::ConnectionState connectionState() const override
  {
    return this->state_;
  }
  std::string lastError() const override
  {
    return {};
  }

  bool connect() override
  {
    this->connectEntered_.store(true);
    if (this->hangConnect_ && this->gate_)
    {
      this->gate_->wait();
    }
    this->state_ = virtual_factory::ConnectionState::Connected;
    return true;
  }

  void disconnect() override
  {
    this->disconnectEntered_.store(true);
    if (this->hangDisconnect_ && this->gate_)
    {
      this->gate_->wait();
    }
    this->state_ = virtual_factory::ConnectionState::Disconnected;
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
  }

  bool connectEntered() const
  {
    return this->connectEntered_.load();
  }
  bool disconnectEntered() const
  {
    return this->disconnectEntered_.load();
  }

private:
  std::string id_;
  std::shared_ptr<HangGate> gate_;
  bool hangConnect_{true};
  bool hangDisconnect_{true};
  Eq equipment_;
  virtual_factory::ConnectionState state_{
      virtual_factory::ConnectionState::Disconnected};
  std::atomic<bool> connectEntered_{false};
  std::atomic<bool> disconnectEntered_{false};
};

}  // namespace

int main()
{
  using virtual_factory::icp::AdapterManager;
  using virtual_factory::icp::ApplicationService;
  using virtual_factory::icp::HttpApiServer;
  using virtual_factory::icp::kLifecycleShutdownGrace;

  constexpr int kOpcTimeoutMs = 2500;
  const auto kStopBound =
      kLifecycleShutdownGrace + std::chrono::milliseconds(1000);

  // -----------------------------------------------------------------------
  // 1) Healthy adapters — normal shutdown
  // -----------------------------------------------------------------------
  {
    const std::string configPath = tempPath("healthy-config.json");
    const std::string historyPath = tempPath("healthy-history.sqlite");
    ::unlink(configPath.c_str());
    ::unlink(historyPath.c_str());
    ApplicationService service(configPath, historyPath);
    service.start();
    expect(service.upsertAdapterConfig(makeMock("mock-ok", "EQ-OK")).ok, "upsert mock");
    expect(service.saveConfiguration().ok, "save");
    expect(service.loadConfiguration().ok, "load");
    expect(waitForAdapterState(service, "mock-ok", "CONNECTED", 2000),
           "mock CONNECTED");

    const auto t0 = std::chrono::steady_clock::now();
    service.stop();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    expect(ms < kStopBound.count(),
           "healthy shutdown within bound (" + std::to_string(ms) + "ms)");

    // Idempotent second stop.
    const auto t1 = std::chrono::steady_clock::now();
    service.stop();
    const auto ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - t1)
                         .count();
    expect(ms2 < 1000, "second stop is cheap (" + std::to_string(ms2) + "ms)");
    ::unlink(configPath.c_str());
    ::unlink(historyPath.c_str());
  }

  // -----------------------------------------------------------------------
  // 2) AdapterManager disconnectAllBounded — hung peer does not block sibling
  // -----------------------------------------------------------------------
  {
    auto gate = std::make_shared<HungAdapter::HangGate>();
    AdapterManager manager;
    auto hung = std::make_unique<HungAdapter>(
        "hung-a", "EQ-HUNG-A", gate, /*hangConnect=*/false, /*hangDisconnect=*/true);
    HungAdapter *rawHung = hung.get();
    auto mock = std::make_unique<virtual_factory::MockIndustrialAdapter>("mock-a");
    mock->addDevice("EQ-MOCK-A", "motor");
    mock->addCapability("EQ-MOCK-A", "start");
    expect(manager.addAdapter(std::move(hung)).ok, "add hung");
    expect(manager.addAdapter(std::move(mock)).ok, "add mock");
    expect(manager.connectAdapter("hung-a").ok, "connect hung");
    expect(manager.connectAdapter("mock-a").ok, "connect mock");

    std::vector<std::shared_ptr<virtual_factory::IndustrialAdapter>> keep;
    const auto t0 = std::chrono::steady_clock::now();
    const std::size_t unfinished = manager.disconnectAllBounded(
        std::chrono::milliseconds(500), &keep);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    expect(ms < 1500, "bounded disconnect returns promptly ("
                          + std::to_string(ms) + "ms)");
    expect(unfinished >= 1, "hung disconnect counted unfinished");
    expect(rawHung->disconnectEntered(), "hung disconnect was attempted");
    expect(manager.adapterCount() == 0, "manager map cleared after bounded disconnect");
    expect(!keep.empty(), "keepAlive retained hung adapter shared_ptr");
    gate->open();  // release abandoned disconnect thread before process exit
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // -----------------------------------------------------------------------
  // 3) Hung connect abandoned by LifecycleExecutor stop
  // -----------------------------------------------------------------------
  {
    auto gate = std::make_shared<HungAdapter::HangGate>();
    AdapterManager manager;
    virtual_factory::icp::LiveStateCache cache;
    auto lifecycle = std::make_shared<virtual_factory::icp::LifecycleExecutor>();
    lifecycle->setHandler([&](const virtual_factory::icp::LifecycleJob &job) {
      if (job.op == virtual_factory::icp::LifecycleOp::Connect)
      {
        (void)manager.connectAdapter(job.adapterId);
      }
    });
    lifecycle->start(2);

    auto hung = std::make_unique<HungAdapter>(
        "hung-c", "EQ-HUNG-C", gate, /*hangConnect=*/true, /*hangDisconnect=*/false);
    HungAdapter *raw = hung.get();
    expect(manager.addAdapter(std::move(hung)).ok, "add hung-c");

    virtual_factory::icp::LifecycleJob job;
    job.adapterId = "hung-c";
    job.op = virtual_factory::icp::LifecycleOp::Connect;
    job.generation = lifecycle->generation("hung-c");
    expect(lifecycle->enqueue(job), "enqueue hung connect");

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (!raw->connectEntered()
           && std::chrono::steady_clock::now() < deadline)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(raw->connectEntered(), "hung connect entered protocol I/O");

    auto pin = std::make_shared<int>(1);
    const auto t0 = std::chrono::steady_clock::now();
    lifecycle->stop(std::chrono::milliseconds(400), pin);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    expect(ms < 1000, "lifecycle stop abandons hung connect ("
                          + std::to_string(ms) + "ms)");
    expect(lifecycle->hasAbandonedWorkers(), "workers marked abandoned");
    gate->open();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // -----------------------------------------------------------------------
  // 4) OPC UA blackhole + mock — ApplicationService stop bound
  // -----------------------------------------------------------------------
  {
    BlackholeTcpListener blackhole;
    expect(blackhole.start(), "blackhole starts");
    const std::string configPath = tempPath("bh-config.json");
    const std::string historyPath = tempPath("bh-history.sqlite");
    ::unlink(configPath.c_str());
    ::unlink(historyPath.c_str());

    ApplicationService service(configPath, historyPath);
    service.start();
    expect(service
               .upsertAdapterConfig(makeOpcUa(
                   "opcua-bh",
                   blackhole.opcUaEndpoint(),
                   kOpcTimeoutMs,
                   "EQ-OPC-BH"))
               .ok,
           "upsert opcua");
    expect(service.upsertAdapterConfig(makeMock("mock-bh", "EQ-MOCK-BH")).ok,
           "upsert mock");
    expect(service.saveConfiguration().ok, "save");
    expect(service.loadConfiguration().ok, "load");
    expect(waitForRecoveryInFlight(service, "opcua-bh", kOpcTimeoutMs),
           "opcua recovery in-flight");
    expect(waitForAdapterState(service, "mock-bh", "CONNECTED", 1000),
           "mock CONNECTED");

    const int port = 19300 + (::getpid() % 600);
    HttpApiServer api(service, "", "127.0.0.1", port);
    expect(api.start(), "HTTP starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(1, 0);
    auto status = client.Get("/api/v1/status");
    expect(status && status->status == 200, "GET status before shutdown");

    // Pending connect + in-flight command on mock, then shutdown.
    (void)service.connectAdapter("opcua-bh");
    std::atomic<bool> cmdDone{false};
    std::thread cmdThread([&]() {
      (void)service.executeEquipmentCommand("EQ-MOCK-BH", "start", 0.0);
      cmdDone.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Config DELETE while stopping path is about to run.
    (void)service.removeAdapterConfig("opcua-bh");

    const auto t0 = std::chrono::steady_clock::now();
    api.stop();
    service.stop();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    cmdThread.join();
    expect(cmdDone.load(), "command finished or was interrupted safely");
    expect(ms < kStopBound.count(),
           "AS+HTTP shutdown within bound with blackhole ("
               + std::to_string(ms) + "ms)");

    blackhole.stop();
    ::unlink(configPath.c_str());
    ::unlink(historyPath.c_str());
    ::unlink((historyPath + "-wal").c_str());
    ::unlink((historyPath + "-shm").c_str());
  }

  // -----------------------------------------------------------------------
  // 5) Multiple blackhole adapters — parallel shutdown bound
  // -----------------------------------------------------------------------
  {
    BlackholeTcpListener bh1;
    BlackholeTcpListener bh2;
    expect(bh1.start() && bh2.start(), "two blackholes");
    const std::string configPath = tempPath("multi-config.json");
    const std::string historyPath = tempPath("multi-history.sqlite");
    ::unlink(configPath.c_str());
    ::unlink(historyPath.c_str());

    ApplicationService service(configPath, historyPath);
    service.start();
    expect(service
               .upsertAdapterConfig(makeOpcUa(
                   "opcua-1", bh1.opcUaEndpoint(), kOpcTimeoutMs, "EQ-1"))
               .ok,
           "upsert opcua-1");
    expect(service
               .upsertAdapterConfig(makeOpcUa(
                   "opcua-2", bh2.opcUaEndpoint(), kOpcTimeoutMs, "EQ-2"))
               .ok,
           "upsert opcua-2");
    expect(service.upsertAdapterConfig(makeMock("mock-m", "EQ-M")).ok,
           "upsert mock-m");
    expect(service.saveConfiguration().ok && service.loadConfiguration().ok,
           "save/load multi");
    expect(waitForRecoveryInFlight(service, "opcua-1", kOpcTimeoutMs),
           "opcua-1 recovery");
    expect(waitForAdapterState(service, "mock-m", "CONNECTED", 1000),
           "mock-m CONNECTED");

    const auto t0 = std::chrono::steady_clock::now();
    service.stop();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    expect(ms < kStopBound.count(),
           "multi-blackhole shutdown within bound ("
               + std::to_string(ms) + "ms)");

    bh1.stop();
    bh2.stop();
    ::unlink(configPath.c_str());
    ::unlink(historyPath.c_str());
  }

  if (failures == 0)
  {
    std::cout << "icp_shutdown_isolation_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_shutdown_isolation_test: " << failures << " failure(s)"
            << std::endl;
  return 1;
}
