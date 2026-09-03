#ifndef VIRTUAL_FACTORY_ICP_APPLICATION_SERVICE_HH_
#define VIRTUAL_FACTORY_ICP_APPLICATION_SERVICE_HH_

#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/icp/LiveStateCache.hh>
#include <virtual_factory/icp/PollScheduler.hh>
#include <virtual_factory/icp/config/ConfigurationCatalog.hh>
#include <virtual_factory/icp/config/ConfigurationModel.hh>

namespace virtual_factory
{
namespace icp
{

struct ProtocolCapability
{
  std::string id;
  std::string label;
  bool configurableWithoutHardware{true};
  bool requiresHilscherHardware{false};
};

struct EquipmentCommandResult
{
  bool ok{false};
  std::string message;
  std::string equipmentId;
  std::string command;
};

struct ApplicationEvent
{
  std::chrono::system_clock::time_point atUtc{};
  std::string level;
  std::string category;
  std::string message;
  std::string adapterId;
  std::string equipmentId;
};

struct ApplicationStatus
{
  std::string product{"ICP"};
  std::string version{"0.1.0-gui"};
  std::string apiVersion{"v1"};
  bool mesDependency{false};
  bool cicDependency{false};
  bool schedulerRunning{false};
  std::size_t configuredAdapterCount{0};
  std::size_t runtimeAdapterCount{0};
  std::size_t connectedAdapters{0};
  std::size_t disconnectedAdapters{0};
  std::size_t faultedAdapters{0};
  std::size_t equipmentCount{0};
  std::size_t staleEquipment{0};
  std::size_t machineFaultEquipment{0};
  bool configurationLoaded{false};
  std::string configurationLoadState;
  std::string configurationPath;
  std::string configurationName;
};

struct RuntimeAdapterView
{
  std::string adapterId;
  std::string protocol;
  bool configured{false};
  bool enabled{true};
  bool runtimePresent{false};
  std::string connectionState;
  /// GUI-friendly label; mock Connected → SIMULATED_ACTIVE (canonical state unchanged).
  std::string connectionStateDisplay;
  std::string lastError;
  std::string description;
  std::size_t equipmentCount{0};
  /// Active stack for this adapter: gateway | hilscher_native | softing_native | simulated
  std::string implementation;
  /// Transport for protocols that distinguish it (e.g. Modbus tcp|rtu).
  std::string transport;
  /// Non-secret connection summary for diagnostics (endpoint, host:port, boardId).
  std::string connectionSummary;
};

struct HilscherDiagnosticsView
{
  bool compiledIn{false};
  std::string readinessState;
  std::string summary;
  std::string driverVersion;
  std::size_t boardCount{0};
  std::string selectedBoard;
  std::string selectedFirmware;
  std::uint32_t serialNumber{0};
  std::vector<std::string> notes;
  std::vector<std::string> manualChecks;
};

/// Standalone ICP application facade for the ICP GUI / Application API.
/// Not CIC. Not MES. Owns catalog + runtime (manager/cache/scheduler).
class ApplicationService
{
public:
  explicit ApplicationService(std::string configurationPath = "icp-config.json");
  ~ApplicationService();

  ApplicationService(const ApplicationService &) = delete;
  ApplicationService &operator=(const ApplicationService &) = delete;

  void start();
  void stop();
  bool running() const;

  ApplicationStatus status() const;
  std::vector<ProtocolCapability> protocols() const;

  const IcpConfigurationDocument &configuration() const;
  ConfigResult setConfiguration(IcpConfigurationDocument document);
  ConfigResult validateConfiguration() const;
  ConfigResult saveConfiguration();
  ConfigResult loadConfiguration();
  ConfigResult importConfigurationJson(const std::string &jsonText);
  std::string exportConfigurationJson() const;

  ConfigResult upsertAdapterConfig(AdapterConfigRecord adapter);
  ConfigResult removeAdapterConfig(const std::string &adapterId);

  std::vector<RuntimeAdapterView> adapters() const;
  std::optional<RuntimeAdapterView> adapter(const std::string &adapterId) const;

  AdapterManagerResult connectAdapter(const std::string &adapterId);
  AdapterManagerResult disconnectAdapter(const std::string &adapterId);

  std::vector<EquipmentSnapshot> equipment() const;
  std::optional<EquipmentSnapshot> equipmentById(const std::string &id) const;

  /// Execute a named command on connected equipment (updates LiveStateCache).
  EquipmentCommandResult executeEquipmentCommand(
      const std::string &equipmentId,
      const std::string &command,
      double parameter = 0.0);

  HilscherDiagnosticsView hilscherDiagnostics() const;
  std::vector<ApplicationEvent> events(std::size_t limit = 100) const;

  /// Classifies configured adapter stack for contextual diagnostics (GUI/API).
  static std::string adapterImplementation(const AdapterConfigRecord &record);

  ConfigurationCatalog &catalog();
  const ConfigurationCatalog &catalog() const;
  AdapterManager &manager();
  LiveStateCache &cache();

  void recordEvent(
      const std::string &level,
      const std::string &category,
      const std::string &message,
      const std::string &adapterId = {},
      const std::string &equipmentId = {});

private:
  AdapterManagerResult ensureRuntimeAdapter(const AdapterConfigRecord &record);
  std::unique_ptr<IndustrialAdapter> createRuntimeAdapter(
      const AdapterConfigRecord &record, std::string *error) const;
  static std::string connectionStateName(ConnectionState state);
  static std::string connectionStateDisplay(
      const std::string &protocol, const std::string &connectionState);
  static std::string connectionSummary(const AdapterConfigRecord &record);

  mutable std::mutex mutex_;
  std::string configuration_path_;
  ConfigurationCatalog catalog_;
  AdapterManager manager_;
  LiveStateCache cache_;
  std::unique_ptr<PollScheduler> scheduler_;
  bool running_{false};
  bool configuration_loaded_{false};
  std::string configuration_load_state_;
  std::deque<ApplicationEvent> events_;
  static constexpr std::size_t kMaxEvents = 500;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
