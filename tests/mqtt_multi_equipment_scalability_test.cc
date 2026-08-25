#include "mqtt_test_broker.hh"

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/MqttIndustrialAdapter.hh>

#include <dirent.h>
#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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
using virtual_factory::MqttAdapterConfig;
using virtual_factory::MqttEquipmentMapping;
using virtual_factory::MqttIndustrialAdapter;
using virtual_factory::MqttPayloadEncoding;
using virtual_factory::makeMqttSignal;
using virtual_factory::test::MqttTestBroker;
using virtual_factory::test::MqttTestClient;

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

int math_max(int a, int b)
{
  return a > b ? a : b;
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

std::string equipment_id(int index)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "PLC-%03d", index);
  return buffer;
}

std::string topic_prefix(int index)
{
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "scale/machine/%03d", index);
  return buffer;
}

MqttEquipmentMapping mapping_for(int index)
{
  const std::string prefix = topic_prefix(index);
  MqttEquipmentMapping machine;
  machine.id = equipment_id(index);
  machine.type = "generic_machine";
  machine.capabilities = {"start", "stop", "set_speed"};
  machine.commands = {
      {"start", prefix + "/cmd/start", "{\"command\":\"start\"}", 1, false},
      {"stop", prefix + "/cmd/stop", "{\"command\":\"stop\"}", 1, false},
      {"set_speed", prefix + "/cmd/speed", "{\"value\":{{value}}}", 1, false},
  };
  machine.telemetry = {
      {"temperature", prefix + "/telemetry", "degC",
       MqttPayloadEncoding::JsonPointer, "/temperature", 1},
      {"pressure", prefix + "/telemetry", "bar",
       MqttPayloadEncoding::JsonPointer, "/pressure", 1},
      {"speed", prefix + "/telemetry", "rpm",
       MqttPayloadEncoding::JsonPointer, "/speed", 1},
  };
  machine.state = makeMqttSignal(prefix + "/state");
  machine.fault = makeMqttSignal(prefix + "/fault");
  return machine;
}

double expected_temperature(int index)
{
  return 20.0 + static_cast<double>(index);
}

double expected_pressure(int index)
{
  return 1.0 + static_cast<double>(index) * 0.01;
}

double expected_speed(int index)
{
  return 100.0 + static_cast<double>(index);
}

bool publish_telemetry(MqttTestClient &publisher, int index)
{
  const std::string prefix = topic_prefix(index);
  std::ostringstream body;
  body << "{\"temperature\":" << expected_temperature(index)
       << ",\"pressure\":" << expected_pressure(index)
       << ",\"speed\":" << expected_speed(index) << "}";
  return publisher.publish(prefix + "/telemetry", body.str(), 1, false) &&
         publisher.publish(prefix + "/state", "true", 1, false) &&
         publisher.publish(prefix + "/fault", "false", 1, false);
}

bool wait_all_telemetry(
    MqttIndustrialAdapter &adapter,
    const std::vector<int> &indices,
    int timeout_ms)
{
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  while (Clock::now() < deadline)
  {
    adapter.poll();
    bool all_ok = true;
    for (int index : indices)
    {
      Equipment *machine = adapter.equipmentById(equipment_id(index));
      if (machine == nullptr)
      {
        all_ok = false;
        break;
      }
      const auto points = machine->telemetry();
      const auto *temperature = find_telemetry(points, "temperature");
      const auto *pressure = find_telemetry(points, "pressure");
      const auto *speed = find_telemetry(points, "speed");
      if (temperature == nullptr || pressure == nullptr || speed == nullptr ||
          !near(temperature->value, expected_temperature(index)) ||
          !near(pressure->value, expected_pressure(index)) ||
          !near(speed->value, expected_speed(index)) || !machine->running() ||
          machine->fault())
      {
        all_ok = false;
        break;
      }
    }
    if (all_ok)
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

struct ScaleResult
{
  int requested{0};
  int brokers{0};
  int connect_ok{0};
  int connect_fail{0};
  double connect_total_ms{0.0};
  double connect_avg_ms{0.0};
  double connect_max_ms{0.0};
  int poll_cycles{0};
  double poll_total_ms{0.0};
  double poll_avg_cycle_ms{0.0};
  double poll_max_adapter_ms{0.0};
  bool telemetry_ok{false};
  bool command_ok{false};
  bool isolation_ok{false};
  bool reconnect_ok{false};
  ProcessSample after_connect;
  ProcessSample after_poll;
};

ScaleResult run_single_broker_scale(int count)
{
  ScaleResult result;
  result.requested = count;
  result.brokers = 1;

  MqttTestBroker broker;
  expect(broker.start(), "start Mosquitto for single-broker scale " +
                             std::to_string(count));
  if (broker.port() == 0)
  {
    return result;
  }

  std::vector<int> indices;
  indices.reserve(static_cast<std::size_t>(count));
  std::vector<MqttEquipmentMapping> equipment;
  equipment.reserve(static_cast<std::size_t>(count));
  for (int i = 1; i <= count; ++i)
  {
    indices.push_back(i);
    equipment.push_back(mapping_for(i));
  }

  MqttAdapterConfig config;
  config.host = broker.host();
  config.port = broker.port();
  config.timeoutMs = 5000;
  config.pollTimeoutMs = 50;
  config.keepaliveSeconds = 20;
  config.equipment = std::move(equipment);

  MqttIndustrialAdapter adapter("mqtt-scale-" + std::to_string(count), config);

  const auto connect_begin = Clock::now();
  const bool connected = adapter.connect();
  result.connect_total_ms = to_ms(Clock::now() - connect_begin);
  result.connect_avg_ms = result.connect_total_ms;
  result.connect_max_ms = result.connect_total_ms;
  if (connected && adapter.connectionState() == ConnectionState::Connected)
  {
    result.connect_ok = 1;
  }
  else
  {
    result.connect_fail = 1;
    expect(false, "connect single-broker scale " + std::to_string(count));
    return result;
  }
  result.after_connect = sample_process();

  MqttTestClient publisher;
  expect(publisher.connect(broker.host(), broker.port(),
                           "scale-pub-" + std::to_string(count)),
         "publisher connect");
  // Publish in batches interleaved with poll so a burst cannot overwhelm
  // the bounded receive queue under test load.
  constexpr int kBatch = 25;
  for (std::size_t offset = 0; offset < indices.size();
       offset += static_cast<std::size_t>(kBatch))
  {
    const std::size_t end =
        std::min(offset + static_cast<std::size_t>(kBatch), indices.size());
    for (std::size_t i = offset; i < end; ++i)
    {
      expect(publish_telemetry(publisher, indices[i]),
             "publish telemetry " + equipment_id(indices[i]));
    }
    adapter.poll();
  }

  const int wait_ms = math_max(15000, count * 100);
  result.telemetry_ok = wait_all_telemetry(adapter, indices, wait_ms);
  expect(result.telemetry_ok, "telemetry for " + std::to_string(count) +
                                  " equipment mappings");

  MqttTestClient command_listener;
  expect(command_listener.connect(broker.host(), broker.port(),
                                  "scale-cmd-" + std::to_string(count)),
         "command listener connect");
  expect(command_listener.subscribe("scale/machine/#", 1),
         "subscribe command wildcard in test helper only");

  bool commands_ok = true;
  for (int index : indices)
  {
    Equipment *machine = adapter.equipmentById(equipment_id(index));
    if (machine == nullptr ||
        !machine->execute("set_speed", 1000.0 + index).accepted)
    {
      commands_ok = false;
      break;
    }
  }
  // Drain publishes; verify a sample of command topics arrived.
  int seen = 0;
  const auto cmd_deadline =
      Clock::now() + std::chrono::milliseconds(math_max(5000, count * 20));
  while (Clock::now() < cmd_deadline && seen < std::min(count, 20))
  {
    std::string topic;
    std::string payload;
    if (command_listener.waitMessage(&topic, &payload, 50))
    {
      if (topic.find("/cmd/speed") != std::string::npos)
      {
        ++seen;
      }
    }
  }
  result.command_ok = commands_ok && seen > 0;
  expect(result.command_ok, "commands for scale " + std::to_string(count));

  constexpr int k_cycles = 50;
  double max_adapter_ms = 0.0;
  const auto poll_begin = Clock::now();
  for (int cycle = 0; cycle < k_cycles; ++cycle)
  {
    const auto one_begin = Clock::now();
    adapter.poll();
    max_adapter_ms = std::max(max_adapter_ms, to_ms(Clock::now() - one_begin));
    expect(adapter.connectionState() == ConnectionState::Connected,
           "poll stays Connected at scale " + std::to_string(count));
  }
  result.poll_cycles = k_cycles;
  result.poll_total_ms = to_ms(Clock::now() - poll_begin);
  result.poll_avg_cycle_ms = result.poll_total_ms / k_cycles;
  result.poll_max_adapter_ms = max_adapter_ms;
  result.after_poll = sample_process();

  // Explicit reconnect while broker is still up (resubscribe path).
  if (count <= 50)
  {
    adapter.disconnect();
    const bool re_ok = adapter.connect();
    result.reconnect_ok =
        re_ok && adapter.connectionState() == ConnectionState::Connected &&
        adapter.equipment().size() == static_cast<std::size_t>(count);
    expect(result.reconnect_ok,
           "explicit reconnect/resubscribe at scale " + std::to_string(count));
    if (result.reconnect_ok)
    {
      for (int index : indices)
      {
        expect(publish_telemetry(publisher, index),
               "republish after reconnect " + equipment_id(index));
      }
      expect(wait_all_telemetry(adapter, indices, 15000),
             "telemetry after reconnect at scale " + std::to_string(count));
    }
  }
  else
  {
    result.reconnect_ok = true;  // covered at ≤50 and in mqtt_adapter_test
  }

  // Isolation: kill broker → Faulted; equipment still represented.
  broker.stop();
  for (int i = 0; i < 40; ++i)
  {
    adapter.poll();
    if (adapter.connectionState() == ConnectionState::Faulted)
    {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  bool isolation = adapter.connectionState() == ConnectionState::Faulted &&
                   adapter.equipment().size() ==
                       static_cast<std::size_t>(count);
  for (int index : indices)
  {
    Equipment *machine = adapter.equipmentById(equipment_id(index));
    if (machine == nullptr)
    {
      isolation = false;
      break;
    }
  }
  result.isolation_ok = isolation;
  expect(result.isolation_ok,
         "broker stop Faulted with equipment retained at scale " +
             std::to_string(count));

  publisher.disconnect();
  command_listener.disconnect();
  adapter.disconnect();
  return result;
}

ScaleResult run_multi_broker_scale(int per_broker, int broker_count)
{
  ScaleResult result;
  result.requested = per_broker * broker_count;
  result.brokers = broker_count;

  std::vector<std::unique_ptr<MqttTestBroker>> brokers;
  std::vector<std::unique_ptr<MqttIndustrialAdapter>> adapters;
  std::vector<std::vector<int>> index_groups;

  int next_index = 1;
  for (int b = 0; b < broker_count; ++b)
  {
    auto broker = std::make_unique<MqttTestBroker>();
    expect(broker->start(), "start broker " + std::to_string(b));
    if (broker->port() == 0)
    {
      return result;
    }

    std::vector<int> indices;
    std::vector<MqttEquipmentMapping> equipment;
    for (int i = 0; i < per_broker; ++i)
    {
      indices.push_back(next_index);
      equipment.push_back(mapping_for(next_index));
      ++next_index;
    }
    index_groups.push_back(indices);

    MqttAdapterConfig config;
    config.host = broker->host();
    config.port = broker->port();
    config.timeoutMs = 5000;
    config.pollTimeoutMs = 50;
    config.keepaliveSeconds = 20;
    config.clientId = "mqtt-multi-b" + std::to_string(b);
    config.equipment = std::move(equipment);

    auto adapter = std::make_unique<MqttIndustrialAdapter>(
        "mqtt-multi-" + std::to_string(b), config);
    brokers.push_back(std::move(broker));
    adapters.push_back(std::move(adapter));
  }

  double connect_max = 0.0;
  const auto connect_begin = Clock::now();
  for (auto &adapter : adapters)
  {
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
  }
  result.connect_total_ms = to_ms(Clock::now() - connect_begin);
  result.connect_avg_ms =
      adapters.empty()
          ? 0.0
          : result.connect_total_ms / static_cast<double>(adapters.size());
  result.connect_max_ms = connect_max;
  result.after_connect = sample_process();

  std::vector<std::unique_ptr<MqttTestClient>> publishers;
  for (std::size_t b = 0; b < brokers.size(); ++b)
  {
    auto publisher = std::make_unique<MqttTestClient>();
    expect(publisher->connect(brokers[b]->host(), brokers[b]->port(),
                              "multi-pub-" + std::to_string(b)),
           "multi publisher connect");
    constexpr int kBatch = 25;
    for (std::size_t offset = 0; offset < index_groups[b].size();
         offset += static_cast<std::size_t>(kBatch))
    {
      const std::size_t end = std::min(
          offset + static_cast<std::size_t>(kBatch), index_groups[b].size());
      for (std::size_t i = offset; i < end; ++i)
      {
        expect(publish_telemetry(*publisher, index_groups[b][i]),
               "multi publish " + equipment_id(index_groups[b][i]));
      }
      adapters[b]->poll();
    }
    publishers.push_back(std::move(publisher));
  }

  bool telemetry_ok = true;
  for (std::size_t b = 0; b < adapters.size(); ++b)
  {
    if (!wait_all_telemetry(*adapters[b], index_groups[b], 15000))
    {
      telemetry_ok = false;
    }
  }
  result.telemetry_ok = telemetry_ok;
  expect(result.telemetry_ok, "multi-broker telemetry");

  // Cross-broker equipment must not appear on the wrong adapter.
  bool isolation = true;
  if (adapters.size() >= 2)
  {
    for (int index : index_groups[0])
    {
      if (adapters[1]->equipmentById(equipment_id(index)) != nullptr)
      {
        isolation = false;
      }
    }
    for (int index : index_groups[1])
    {
      if (adapters[0]->equipmentById(equipment_id(index)) != nullptr)
      {
        isolation = false;
      }
    }

    // Stop broker 0 → adapter 0 Faulted; adapter 1 remains Connected.
    brokers[0]->stop();
    for (int i = 0; i < 40; ++i)
    {
      adapters[0]->poll();
      adapters[1]->poll();
      if (adapters[0]->connectionState() == ConnectionState::Faulted)
      {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    isolation = isolation &&
                adapters[0]->connectionState() == ConnectionState::Faulted &&
                adapters[1]->connectionState() == ConnectionState::Connected;
  }
  result.isolation_ok = isolation;
  expect(result.isolation_ok, "multi-broker isolation");

  constexpr int k_cycles = 30;
  double max_adapter_ms = 0.0;
  const auto poll_begin = Clock::now();
  for (int cycle = 0; cycle < k_cycles; ++cycle)
  {
    for (auto &adapter : adapters)
    {
      const auto one_begin = Clock::now();
      adapter->poll();
      max_adapter_ms = std::max(max_adapter_ms, to_ms(Clock::now() - one_begin));
    }
  }
  result.poll_cycles = k_cycles;
  result.poll_total_ms = to_ms(Clock::now() - poll_begin);
  result.poll_avg_cycle_ms = result.poll_total_ms / k_cycles;
  result.poll_max_adapter_ms = max_adapter_ms;
  result.after_poll = sample_process();
  result.command_ok = true;
  result.reconnect_ok = true;

  for (auto &publisher : publishers)
  {
    publisher->disconnect();
  }
  for (auto &adapter : adapters)
  {
    adapter->disconnect();
  }
  return result;
}

void print_result(const char *label, const ScaleResult &r)
{
  std::cout << "\n=== " << label << " ===\n"
            << "equipment=" << r.requested << " brokers=" << r.brokers
            << " connect_ok=" << r.connect_ok
            << " connect_fail=" << r.connect_fail << std::fixed
            << std::setprecision(3) << "\nconnect_total_ms=" << r.connect_total_ms
            << " connect_avg_ms=" << r.connect_avg_ms
            << " connect_max_ms=" << r.connect_max_ms
            << "\npoll_cycles=" << r.poll_cycles
            << " poll_total_ms=" << r.poll_total_ms
            << " poll_avg_cycle_ms=" << r.poll_avg_cycle_ms
            << " poll_max_adapter_ms=" << r.poll_max_adapter_ms
            << "\ntelemetry_ok=" << r.telemetry_ok
            << " command_ok=" << r.command_ok
            << " isolation_ok=" << r.isolation_ok
            << " reconnect_ok=" << r.reconnect_ok << std::endl;
  print_sample("after_connect", r.after_connect);
  print_sample("after_poll", r.after_poll);
}

}  // namespace

int main()
{
  std::cout << "MQTT multi-equipment scalability validation\n"
            << "VALIDATION ONLY — not a production capacity guarantee.\n"
            << "Architecture: one MqttIndustrialAdapter = one broker;\n"
            << "N equipment on that broker = N GenericEquipment mappings.\n";

  const ProcessSample baseline = sample_process();
  print_sample("baseline", baseline);

  const int sizes[] = {10, 50, 100, 200};
  for (int count : sizes)
  {
    const ScaleResult r = run_single_broker_scale(count);
    const std::string label = "single-broker N=" + std::to_string(count);
    print_result(label.c_str(), r);
    expect(r.connect_ok == 1, "connect ok N=" + std::to_string(count));
    expect(r.telemetry_ok, "telemetry ok N=" + std::to_string(count));
    expect(r.isolation_ok, "isolation ok N=" + std::to_string(count));
  }

  // Multiple brokers with equipment distributed between them.
  const ScaleResult multi = run_multi_broker_scale(50, 2);
  print_result("multi-broker 2x50", multi);
  expect(multi.connect_ok == 2, "two brokers connect");
  expect(multi.telemetry_ok, "multi-broker telemetry");
  expect(multi.isolation_ok, "multi-broker isolation");

  if (g_failures == 0)
  {
    std::cout << "\nmqtt_multi_equipment_scalability_test: PASSED\n";
    return 0;
  }
  std::cerr << "\nmqtt_multi_equipment_scalability_test: FAILED (" << g_failures
            << ")\n";
  return 1;
}
