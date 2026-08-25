#include "opcua_scale_plc_server.hh"

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/OpcUaIndustrialAdapter.hh>

#include <dirent.h>
#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;
using virtual_factory::ConnectionState;
using virtual_factory::Equipment;
using virtual_factory::OpcUaAdapterConfig;
using virtual_factory::OpcUaEquipmentMapping;
using virtual_factory::OpcUaIndustrialAdapter;
using virtual_factory::OpcUaNodeRef;
using virtual_factory::test::OpcUaScalePlcServer;

int g_failures = 0;

void expect(bool ok, const std::string &message)
{
  if (!ok)
  {
    std::cerr << "FAIL: " << message << std::endl;
    ++g_failures;
  }
}

bool near(double actual, double expected)
{
  return std::fabs(actual - expected) <= 1e-6;
}

double to_ms(Clock::duration duration)
{
  return std::chrono::duration<double, std::milli>(duration).count();
}

const virtual_factory::TelemetryPoint *find_telemetry(
    const std::vector<virtual_factory::TelemetryPoint> &points,
    const std::string &name)
{
  for (const auto &point : points)
  {
    if (point.name == name)
    {
      return &point;
    }
  }
  return nullptr;
}

struct ProcessSample
{
  long rss_kb{0};
  int fd_count{0};
  double cpu_user_s{0.0};
  double cpu_sys_s{0.0};
};

ProcessSample sample_process()
{
  ProcessSample sample;
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line))
  {
    if (line.rfind("VmRSS:", 0) == 0)
    {
      std::istringstream in(line.substr(6));
      in >> sample.rss_kb;
      break;
    }
  }

  DIR *dir = ::opendir("/proc/self/fd");
  if (dir != nullptr)
  {
    while (::readdir(dir) != nullptr)
    {
      ++sample.fd_count;
    }
    ::closedir(dir);
    sample.fd_count -= 2;
  }

  rusage usage{};
  if (::getrusage(RUSAGE_SELF, &usage) == 0)
  {
    sample.cpu_user_s = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6;
    sample.cpu_sys_s = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
  }
  return sample;
}

void print_sample(const char *label, const ProcessSample &sample)
{
  std::cout << label << " RSS_kB=" << sample.rss_kb
            << " fds=" << sample.fd_count << std::fixed << std::setprecision(3)
            << " cpu_user_s=" << sample.cpu_user_s
            << " cpu_sys_s=" << sample.cpu_sys_s << std::endl;
}

std::string make_adapter_id(int index)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "opcua-plc-%03d", index);
  return buffer;
}

OpcUaAdapterConfig mapping_for(const OpcUaScalePlcServer &server)
{
  OpcUaAdapterConfig config;
  config.endpointUrl = server.endpointUrl();

  OpcUaEquipmentMapping machine;
  machine.id = server.equipmentId();
  machine.type = "generic_machine";
  machine.capabilities = {"start", "stop", "set_speed"};
  machine.commands = {
      {"start", OpcUaNodeRef{1, server.startNodeId()}},
      {"stop", OpcUaNodeRef{1, server.stopNodeId()}},
      {"set_speed", OpcUaNodeRef{1, server.speedSetpointNodeId()}},
  };
  machine.telemetry = {
      {"temperature", OpcUaNodeRef{1, server.temperatureNodeId()}, "degC"},
      {"pressure", OpcUaNodeRef{1, server.pressureNodeId()}, "bar"},
      {"speed", OpcUaNodeRef{1, server.speedNodeId()}, "rpm"},
  };
  machine.stateNode = OpcUaNodeRef{1, server.runningNodeId()};
  machine.faultNode = OpcUaNodeRef{1, server.faultNodeId()};
  config.equipment.push_back(std::move(machine));
  return config;
}

bool telemetry_isolated(
    const std::vector<std::unique_ptr<OpcUaScalePlcServer>> &servers,
    std::vector<std::unique_ptr<OpcUaIndustrialAdapter>> &adapters)
{
  for (std::size_t i = 0; i < adapters.size(); ++i)
  {
    Equipment *machine = adapters[i]->equipmentById(servers[i]->equipmentId());
    if (machine == nullptr)
    {
      return false;
    }
    const auto points = machine->telemetry();
    const auto *temperature = find_telemetry(points, "temperature");
    const auto *pressure = find_telemetry(points, "pressure");
    const auto *speed = find_telemetry(points, "speed");
    if (temperature == nullptr || pressure == nullptr || speed == nullptr)
    {
      return false;
    }
    if (!near(temperature->value, servers[i]->expectedTemperature()) ||
        !near(pressure->value, servers[i]->expectedPressure()) ||
        !near(speed->value, servers[i]->expectedSpeed()))
    {
      return false;
    }
    if (i > 0 &&
        adapters[i]->equipmentById(servers[i - 1]->equipmentId()) != nullptr)
    {
      return false;
    }
  }
  return true;
}

bool commands_isolated(
    std::vector<std::unique_ptr<OpcUaScalePlcServer>> &servers,
    std::vector<std::unique_ptr<OpcUaIndustrialAdapter>> &adapters)
{
  for (std::size_t i = 0; i < adapters.size(); ++i)
  {
    Equipment *machine = adapters[i]->equipmentById(servers[i]->equipmentId());
    if (machine == nullptr)
    {
      return false;
    }
    const double setpoint = 1000.0 + static_cast<double>(servers[i]->index());
    if (!machine->execute("start").accepted ||
        !machine->execute("set_speed", setpoint).accepted ||
        !machine->execute("stop").accepted)
    {
      return false;
    }
  }
  for (std::size_t i = 0; i < servers.size(); ++i)
  {
    const double setpoint = 1000.0 + static_cast<double>(servers[i]->index());
    if (!servers[i]->startNode() || !servers[i]->stopNode() ||
        !near(servers[i]->speedSetpoint(), setpoint))
    {
      return false;
    }
    if (i > 0 && near(servers[i]->speedSetpoint(),
                      1000.0 + static_cast<double>(servers[i - 1]->index())))
    {
      return false;
    }
  }
  return true;
}

struct ScaleResult
{
  int requested{0};
  int servers_started{0};
  int connect_ok{0};
  int connect_fail{0};
  double connect_total_ms{0.0};
  double connect_avg_ms{0.0};
  double connect_max_ms{0.0};
  int poll_cycles{0};
  int poll_ok{0};
  int poll_fail{0};
  double poll_total_ms{0.0};
  double poll_avg_cycle_ms{0.0};
  double poll_avg_adapter_ms{0.0};
  double poll_max_adapter_ms{0.0};
  bool telemetry_ok{false};
  bool command_ok{false};
  bool isolation_ran{false};
  bool isolation_ok{false};
  bool reconnect_ok{false};
  double reconnect_total_ms{0.0};
  double reconnect_avg_ms{0.0};
  ProcessSample after_connect;
};

ScaleResult run_scale(int count, bool run_isolation)
{
  ScaleResult result;
  result.requested = count;

  std::vector<std::unique_ptr<OpcUaScalePlcServer>> servers;
  servers.reserve(static_cast<std::size_t>(count));
  for (int i = 1; i <= count; ++i)
  {
    auto server = std::make_unique<OpcUaScalePlcServer>(i);
    if (!server->start())
    {
      expect(false, "start PLC server " + std::to_string(i));
      break;
    }
    servers.push_back(std::move(server));
  }
  result.servers_started = static_cast<int>(servers.size());
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<std::unique_ptr<OpcUaIndustrialAdapter>> adapters;
  adapters.reserve(servers.size());
  double connect_max = 0.0;
  const auto connect_begin = Clock::now();
  for (std::size_t i = 0; i < servers.size(); ++i)
  {
    auto adapter = std::make_unique<OpcUaIndustrialAdapter>(
        make_adapter_id(servers[i]->index()), mapping_for(*servers[i]));
    const auto one_begin = Clock::now();
    const bool ok = adapter->connect();
    connect_max = std::max(connect_max, to_ms(Clock::now() - one_begin));
    if (ok && adapter->connectionState() == ConnectionState::Connected)
    {
      ++result.connect_ok;
    }
    else
    {
      ++result.connect_fail;
    }
    adapters.push_back(std::move(adapter));
  }
  result.connect_total_ms = to_ms(Clock::now() - connect_begin);
  result.connect_avg_ms =
      adapters.empty() ? 0.0
                       : result.connect_total_ms / static_cast<double>(adapters.size());
  result.connect_max_ms = connect_max;
  result.after_connect = sample_process();

  constexpr int k_cycles = 100;
  int poll_ok = 0;
  int poll_fail = 0;
  double max_adapter_ms = 0.0;
  const auto poll_begin = Clock::now();
  for (int cycle = 0; cycle < k_cycles; ++cycle)
  {
    for (auto &adapter : adapters)
    {
      const auto one_begin = Clock::now();
      adapter->poll();
      max_adapter_ms = std::max(max_adapter_ms, to_ms(Clock::now() - one_begin));
      if (adapter->connectionState() == ConnectionState::Connected)
      {
        ++poll_ok;
      }
      else
      {
        ++poll_fail;
      }
    }
  }
  result.poll_cycles = k_cycles;
  result.poll_total_ms = to_ms(Clock::now() - poll_begin);
  result.poll_avg_cycle_ms = result.poll_total_ms / k_cycles;
  const int n_polls = k_cycles * static_cast<int>(adapters.size());
  result.poll_avg_adapter_ms =
      n_polls == 0 ? 0.0 : result.poll_total_ms / n_polls;
  result.poll_max_adapter_ms = max_adapter_ms;
  result.poll_ok = poll_ok;
  result.poll_fail = poll_fail;

  result.telemetry_ok = telemetry_isolated(servers, adapters);
  result.command_ok = commands_isolated(servers, adapters);

  if (run_isolation && adapters.size() >= 38)
  {
    result.isolation_ran = true;
    const std::size_t victim = 36;
    servers[victim]->stop();
    for (auto &adapter : adapters)
    {
      adapter->poll();
    }

    bool others_ok = true;
    for (std::size_t i = 0; i < adapters.size(); ++i)
    {
      if (i != victim &&
          adapters[i]->connectionState() != ConnectionState::Connected)
      {
        others_ok = false;
      }
    }
    const bool victim_faulted =
        adapters[victim]->connectionState() == ConnectionState::Faulted;
    Equipment *listed =
        adapters[victim]->equipmentById(servers[victim]->equipmentId());
    const bool still_listed = listed != nullptr;
    const bool not_machine_fault = listed != nullptr && !listed->fault();

    expect(servers[victim]->start(), "PLC-037 restart");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    expect(adapters[victim]->connect(), "adapter 37 reconnect after restart");
    adapters[victim]->poll();
    Equipment *restored =
        adapters[victim]->equipmentById(servers[victim]->equipmentId());
    const auto *temperature =
        restored ? find_telemetry(restored->telemetry(), "temperature") : nullptr;
    const bool healthy =
        adapters[victim]->connectionState() == ConnectionState::Connected &&
        temperature != nullptr &&
        near(temperature->value, servers[victim]->expectedTemperature());

    result.isolation_ok = victim_faulted && others_ok && still_listed &&
                          not_machine_fault && healthy;
    expect(victim_faulted, "PLC-037 Faulted after stop");
    expect(others_ok, "other adapters remain Connected");
    expect(still_listed, "last-known PLC-037 equipment remains");
    expect(not_machine_fault, "comms fault is not machine fault");
    expect(healthy, "PLC-037 healthy after restart");
  }

  int reconnect_ok = 0;
  const auto reconnect_begin = Clock::now();
  for (auto &adapter : adapters)
  {
    adapter->disconnect();
  }
  for (std::size_t i = 0; i < adapters.size(); ++i)
  {
    const bool ok = adapters[i]->connect();
    adapters[i]->poll();
    Equipment *machine = adapters[i]->equipmentById(servers[i]->equipmentId());
    const auto *temperature =
        machine ? find_telemetry(machine->telemetry(), "temperature") : nullptr;
    if (ok && adapters[i]->connectionState() == ConnectionState::Connected &&
        temperature != nullptr &&
        near(temperature->value, servers[i]->expectedTemperature()))
    {
      ++reconnect_ok;
    }
  }
  result.reconnect_total_ms = to_ms(Clock::now() - reconnect_begin);
  result.reconnect_avg_ms =
      adapters.empty()
          ? 0.0
          : result.reconnect_total_ms / static_cast<double>(adapters.size());
  result.reconnect_ok = reconnect_ok == static_cast<int>(adapters.size());

  expect(result.servers_started == count,
         std::to_string(count) + " servers started");
  expect(result.connect_ok == count, std::to_string(count) + " adapters connected");
  expect(result.connect_fail == 0,
         std::to_string(count) + " zero connect failures");
  expect(result.telemetry_ok, std::to_string(count) + " telemetry isolated");
  expect(result.command_ok, std::to_string(count) + " commands isolated");
  expect(result.reconnect_ok, std::to_string(count) + " reconnect ok");
  expect(result.poll_fail == 0,
         std::to_string(count) + " healthy polls succeeded");

  for (auto &adapter : adapters)
  {
    adapter->disconnect();
  }
  return result;
}

void print_result(const ScaleResult &result)
{
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "SCALE " << result.requested
            << " started=" << result.servers_started
            << " connect_ok=" << result.connect_ok
            << " connect_fail=" << result.connect_fail
            << " connect_total_ms=" << result.connect_total_ms
            << " connect_avg_ms=" << result.connect_avg_ms
            << " connect_max_ms=" << result.connect_max_ms
            << " poll_total_ms=" << result.poll_total_ms
            << " poll_avg_cycle_ms=" << result.poll_avg_cycle_ms
            << " poll_avg_adapter_ms=" << result.poll_avg_adapter_ms
            << " poll_max_adapter_ms=" << result.poll_max_adapter_ms
            << " poll_ok=" << result.poll_ok
            << " poll_fail=" << result.poll_fail
            << " telemetry_ok=" << result.telemetry_ok
            << " command_ok=" << result.command_ok
            << " reconnect_ok=" << result.reconnect_ok
            << " reconnect_total_ms=" << result.reconnect_total_ms
            << " reconnect_avg_ms=" << result.reconnect_avg_ms
            << " rss_kb=" << result.after_connect.rss_kb
            << " fds=" << result.after_connect.fd_count << std::endl;
  if (result.isolation_ran)
  {
    std::cout << "  isolation_ok=" << result.isolation_ok << std::endl;
  }
}

}  // namespace

int main()
{
  std::cout << "opcua_multi_server_scalability_test\n";
  std::cout << "architecture: 1 OpcUaIndustrialAdapter = 1 UA_Client = 1 endpoint\n";
  std::cout << "limitation: simulated servers AND clients share this process\n";

  print_sample("baseline", sample_process());

  const int scales[] = {10, 25, 50, 100, 200};
  std::vector<ScaleResult> results;
  for (int scale : scales)
  {
    std::cout << "---- running scale " << scale << " ----" << std::endl;
    ScaleResult result = run_scale(scale, scale == 100);
    print_sample("after_connect", result.after_connect);
    print_result(result);
    results.push_back(result);
    if (result.connect_ok != scale || result.servers_started != scale)
    {
      std::cout << "stopping further scales after failure at " << scale
                << std::endl;
      break;
    }
  }

  std::cout << "\nServers | Success | Failure | ConnectTotal_ms | Avg_ms | Max_ms | PollCycleAvg_ms | ReconnectTotal_ms | RSS_kB | FDs\n";
  for (const auto &result : results)
  {
    std::cout << std::setw(7) << result.requested << " | " << std::setw(7)
              << result.connect_ok << " | " << std::setw(7)
              << result.connect_fail << " | " << std::setw(15) << std::fixed
              << std::setprecision(1) << result.connect_total_ms << " | "
              << std::setw(6) << result.connect_avg_ms << " | " << std::setw(6)
              << result.connect_max_ms << " | " << std::setw(15)
              << result.poll_avg_cycle_ms << " | " << std::setw(17)
              << result.reconnect_total_ms << " | " << std::setw(6)
              << result.after_connect.rss_kb << " | "
              << result.after_connect.fd_count << std::endl;
  }

  if (g_failures != 0)
  {
    std::cerr << g_failures << " assertion(s) failed" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "opcua_multi_server_scalability_test: all assertions passed\n";
  return EXIT_SUCCESS;
}
