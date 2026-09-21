#ifndef VIRTUAL_FACTORY_ICP_APPLICATION_SERVICE_HH_
#define VIRTUAL_FACTORY_ICP_APPLICATION_SERVICE_HH_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <atomic>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/icp/LifecycleExecutor.hh>
#include <virtual_factory/icp/LiveStateCache.hh>
#include <virtual_factory/icp/PollScheduler.hh>
#include <virtual_factory/icp/config/ConfigurationCatalog.hh>
#include <virtual_factory/icp/config/ConfigurationModel.hh>
#include <virtual_factory/icp/history/AsyncHistoryWriter.hh>
#include <virtual_factory/icp/history/HistoryTypes.hh>

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

/// Protocol-agnostic command diagnostic snapshot (config + last execution).
/// Availability/execution use only states the runtime can establish.
struct CommandDiagnostic
{
  std::string command;
  std::string target;
  std::string availability{"CONFIGURED"};
  std::string execution{"NOT_EXECUTED"};
  std::string reason;
  std::string lastError;
  std::string errorCode;
  std::string errorMessage;
  std::chrono::system_clock::time_point lastExecutionAtUtc{};
  bool hasLastExecution{false};
};

struct ApplicationEvent
{
  std::chrono::system_clock::time_point atUtc{};
  std::string level;
  std::string category;
  std::string message;
  std::string adapterId;
  std::string equipmentId;
  /// Optional structured fields — empty when unknown (do not fabricate).
  std::string eventType;
  std::string protocol;
  std::string reason;
  std::string errorCode;
  std::string errorDetails;
  std::string nodeId;
  std::string previousState;
  std::string newState;
  std::string previousHealth;
  std::string newHealth;
  std::string command;
  std::string recovery;
  std::string correlationId;
  std::int64_t durationMs{-1};
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
  /// Operator health from session diagnostics (HEALTHY|DEGRADED|FAULTED|UNKNOWN).
  std::string health{"UNKNOWN"};
  std::string description;
  std::size_t equipmentCount{0};
  /// Active stack for this adapter: gateway | hilscher_native | softing_native | simulated
  std::string implementation;
  /// Transport for protocols that distinguish it (e.g. Modbus tcp|rtu).
  std::string transport;
  /// Non-secret connection summary for diagnostics (endpoint, host:port, boardId).
  std::string connectionSummary;
};

/// Common, protocol-agnostic session diagnostics for one adapter (process lifetime).
struct AdapterSessionDiagnostics
{
  std::size_t connectionAttempts{0};
  std::size_t successfulConnections{0};
  std::size_t failedConnections{0};
  std::size_t reconnectCount{0};
  std::size_t faultCount{0};
  std::size_t warningCount{0};
  std::size_t communicationFailureCount{0};
  bool activeFault{false};
  bool everConnected{false};
  std::chrono::system_clock::time_point sessionStartedAt{};
  std::chrono::system_clock::time_point connectedAt{};
  std::chrono::system_clock::time_point disconnectedAt{};
  std::chrono::system_clock::time_point lastStateChangeAt{};
  std::chrono::system_clock::time_point lastSuccessfulCommunicationAt{};
  bool hasLastSuccessfulCommunication{false};
  std::chrono::milliseconds cumulativeConnectedMs{0};
  std::chrono::milliseconds cumulativeDisconnectedMs{0};
  std::string lastObservedState{"DISCONNECTED"};
  /// Operator health: HEALTHY | DEGRADED | FAULTED | UNKNOWN (not the same as connection state).
  std::string health{"UNKNOWN"};
  std::string healthReason;
  /// Lifecycle/comms view: CONNECTED | DISCONNECTED | FAULTED | UNKNOWN.
  std::string communicationLifecycleState{"UNKNOWN"};
  std::string lastWarning;
  std::string earlyWarning;
  /// Last health value for which a health transition event was emitted.
  std::string lastEmittedHealth;
  bool hasEmittedHealth{false};

  /// Wall-clock time of the most recent transition into FAULTED (session).
  std::chrono::system_clock::time_point faultedAt{};
  bool hasFaultedAt{false};
  std::string lastFaultError;
  std::string lastFaultNodeId;

  /// ICP-owned auto-reconnect scheduling (steady clock; not persisted).
  std::chrono::steady_clock::time_point nextAutoReconnectAt{};
  std::chrono::milliseconds autoReconnectBackoffMs{1000};
  std::size_t autoReconnectAttempts{0};
  bool autoReconnectInFlight{false};
  /// When true, ICP background recovery may connect a DISCONNECTED/FAULTED
  /// adapter. Armed on configuration materialize and explicit Connect; cleared
  /// on explicit Disconnect so operator disconnect stays sticky for this process.
  bool autoConnectDesired{false};
};

struct AdapterDiagnosticsView
{
  RuntimeAdapterView adapter;
  AdapterSessionDiagnostics session;
  std::size_t healthyEquipmentCount{0};
  std::size_t degradedEquipmentCount{0};
  std::size_t faultedEquipmentCount{0};
  std::int64_t currentStateDurationMs{0};
  std::int64_t currentUptimeMs{0};
  std::int64_t currentDowntimeMs{0};
  std::string sessionMtbfStatus{"insufficient_data"};
  std::int64_t sessionMtbfMs{0};
};

struct ActiveAlarmView
{
  std::string severity;
  std::string sourceType;
  std::string sourceId;
  std::string protocol;
  std::string category;
  std::string message;
  std::chrono::system_clock::time_point sinceUtc{};
};

struct DiagnosticsSystemSummary
{
  std::string overallHealth{"UNKNOWN"};
  std::size_t configuredAdapters{0};
  std::size_t connectedAdapters{0};
  std::size_t disconnectedAdapters{0};
  std::size_t faultedAdapters{0};
  std::size_t warningCount{0};
  std::size_t activeAlarmCount{0};
  std::size_t historicalFaultCount{0};
  std::size_t healthyAdapters{0};
  std::size_t degradedAdapters{0};
  std::size_t faultedHealthAdapters{0};
  std::size_t unknownHealthAdapters{0};
  std::size_t healthyEquipment{0};
  std::size_t degradedEquipment{0};
  std::size_t faultedEquipment{0};
  std::string mtbfStatus{"insufficient_data"};
  std::string mtbfNote{
      "Long-term MTBF requires persistent history across sessions. "
      "Only session-scoped estimates are available in this process."};
  bool schedulerRunning{false};
};

/// ICP software self-diagnostics (not industrial adapter health).
struct IcpSelfDiagnostics
{
  std::string overallHealth{"UNKNOWN"};
  bool serviceRunning{false};
  bool schedulerRunning{false};
  bool apiReachable{true};
  bool configurationValid{false};
  std::string configurationMessage;
  std::size_t eventBufferSize{0};
  std::size_t eventBufferCapacity{0};
  std::size_t configuredAdapters{0};
  std::size_t runtimeAdapters{0};
  std::size_t liveEquipmentCount{0};
  std::int64_t applicationUptimeMs{0};
  std::vector<std::string> checks;
};

struct DiagnosticsReport
{
  DiagnosticsSystemSummary system;
  IcpSelfDiagnostics icp;
  std::vector<AdapterDiagnosticsView> adapters;
  std::vector<EquipmentSnapshot> equipment;
  std::vector<ActiveAlarmView> activeAlarms;
  std::vector<ApplicationEvent> recentEvents;
  std::chrono::system_clock::time_point generatedAtUtc{};
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
  /// historyDatabasePath empty → derive icp-history.sqlite next to configuration.
  explicit ApplicationService(
      std::string configurationPath = "icp-config.json",
      std::string historyDatabasePath = {});
  ~ApplicationService();

  ApplicationService(const ApplicationService &) = delete;
  ApplicationService &operator=(const ApplicationService &) = delete;

  void start();
  void stop();
  bool running() const;

  /// Historian availability (independent of protocol adapter health).
  HistoryStatus historyStatus() const;
  HistoryQueryResult queryHistory(const HistoryQuery &query) const;
  const std::string &historyDatabasePath() const;

  /// Acknowledge an alarm occurrence (historical action; never deletes rows).
  ConfigResult acknowledgeAlarmOccurrence(
      std::int64_t occurrenceId, const std::string &actorId = {});

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
  /// Explicit disconnect then connect; increments reconnectCount (GUI/API reconnect).
  AdapterManagerResult reconnectAdapter(const std::string &adapterId);

  std::vector<EquipmentSnapshot> equipment() const;
  std::optional<EquipmentSnapshot> equipmentById(const std::string &id) const;

  /// Execute a named command on connected equipment (updates LiveStateCache).
  EquipmentCommandResult executeEquipmentCommand(
      const std::string &equipmentId,
      const std::string &command,
      double parameter = 0.0);

  /// Generic command diagnostics for one equipment (configured + last execution).
  std::vector<CommandDiagnostic> commandDiagnosticsForEquipment(
      const std::string &adapterId, const std::string &equipmentId) const;

  HilscherDiagnosticsView hilscherDiagnostics() const;
  std::vector<ApplicationEvent> events(std::size_t limit = 100) const;

  /// Adapter-agnostic diagnostics snapshot for GUI / Application API.
  DiagnosticsReport diagnosticsReport() const;

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

  /// Record a structured event (optional fields may be left empty).
  void recordEvent(ApplicationEvent event);

private:
  /// Rematerialize runtime for config changes: create replacement, extract old
  /// without blocking on protocol I/O, enqueue Teardown for the old instance.
  AdapterManagerResult ensureRuntimeAdapter(const AdapterConfigRecord &record);
  /// Connect/Reconnect path: create only if missing; never remove/recreate.
  /// Must not block on industrial I/O or an in-flight lifecycle connect.
  AdapterManagerResult ensureRuntimeAdapterPresent(
      const AdapterConfigRecord &record);
  /// Enqueue LifecycleOp::Teardown for an extracted runtime (or sync if stopped).
  void scheduleAdapterTeardown(
      const std::string &adapterId, AdapterManager::ExtractedAdapter extracted);
  std::unique_ptr<IndustrialAdapter> createRuntimeAdapter(
      const AdapterConfigRecord &record, std::string *error) const;
  static std::string connectionStateName(ConnectionState state);
  static std::string connectionStateDisplay(
      const std::string &protocol, const std::string &connectionState);
  static std::string connectionSummary(const AdapterConfigRecord &record);
  static std::string commandRuntimeKey(
      const std::string &equipmentId, const std::string &command);
  static std::string commandTargetSummary(
      const AdapterConfigRecord &record, const CommandMappingRecord &cmd);

  AdapterSessionDiagnostics &diagnosticsFor(const std::string &adapterId) const;
  void observeAdapterStateLocked(
      const std::string &adapterId, const std::string &connectionState) const;
  void refreshAllAdapterObservationsLocked() const;
  void recordEventLocked(ApplicationEvent event);
  void emitLifecycleTransitionLocked(
      const std::string &adapterId,
      const std::string &previousState,
      const std::string &newState,
      std::int64_t durationMs) const;
  void onPollCycle();
  void executeLifecycleJob(const LifecycleJob &job);
  void applyConnectOutcome(
      const std::string &adapterId,
      const AdapterManagerResult &connected,
      bool emitFailureEvent,
      bool reconnectStyleFailure);
  void scheduleAutoReconnectLocked(const std::string &adapterId) const;
  /// Create missing enabled runtime adapters without connecting. Arms ICP-owned
  /// background connect so peer-down at startup cannot block HTTP/control plane.
  void materializeEnabledAdaptersForRecovery();
  static void enrichCommunicationErrorFields(ApplicationEvent *event);

  void ensureHistoryWriter();
  void persistEventToHistory(const ApplicationEvent &event) const;
  void persistConfigRevision(const std::string &action, const std::string &summary) const;
  void persistCommandAudit(
      const std::string &equipmentId,
      const std::string &adapterId,
      const std::string &command,
      const std::string &result,
      const std::string &errorCode,
      std::int64_t durationMs,
      const std::string &correlationId) const;
  void syncAlarmAndEquipmentHistoryLocked() const;
  static std::int64_t toEpochMs(std::chrono::system_clock::time_point tp);
  /// Stable alarm identity: sourceType:sourceId:category (message excluded).
  static std::string alarmKeyFor(const ActiveAlarmView &alarm);
  static std::string equipmentHistoryState(const EquipmentSnapshot &snap);
  std::int64_t nextAlarmOccurrenceIdLocked() const;

  struct OpenAlarmTracking
  {
    ActiveAlarmView view;
    std::int64_t occurrenceId{0};
  };

  mutable std::mutex mutex_;
  std::string configuration_path_;
  std::string history_database_path_;
  mutable std::unique_ptr<AsyncHistoryWriter> history_writer_;
  ConfigurationCatalog catalog_;
  /// Shared so abandoned lifecycle/disconnect threads can outlive stop() without
  /// use-after-free on protocol I/O (P2 shutdown isolation).
  std::shared_ptr<AdapterManager> manager_{std::make_shared<AdapterManager>()};
  std::shared_ptr<LiveStateCache> cache_{std::make_shared<LiveStateCache>()};
  std::shared_ptr<LifecycleExecutor> lifecycle_{
      std::make_shared<LifecycleExecutor>()};
  /// Shared with in-flight lifecycle jobs so abandoned workers can skip
  /// ApplicationService diagnostics after shutdown without touching `this`.
  std::shared_ptr<std::atomic<bool>> shutdown_flag_{
      std::make_shared<std::atomic<bool>>(false)};
  std::unique_ptr<PollScheduler> scheduler_;
  bool running_{false};
  /// Intentional process-lifetime pins for abandoned shutdown I/O.
  std::vector<std::shared_ptr<void>> shutdown_keep_alives_;
  bool configuration_loaded_{false};
  std::string configuration_load_state_;
  std::deque<ApplicationEvent> events_;
  static constexpr std::size_t kMaxEvents = 500;
  mutable std::unordered_map<std::string, AdapterSessionDiagnostics> adapter_diagnostics_;
  /// Last observed execution outcome per equipment+command (process lifetime).
  mutable std::unordered_map<std::string, CommandDiagnostic> command_runtime_;
  /// Adapters currently inside reconnectAdapter() (for recovery event category).
  mutable std::unordered_map<std::string, bool> reconnect_in_progress_;
  /// Open alarm identities → current occurrence (process-lifetime; no poll dupes).
  mutable std::unordered_map<std::string, OpenAlarmTracking> open_alarms_;
  /// Last persisted equipment operational history state per equipment id.
  mutable std::unordered_map<std::string, std::string> equipment_history_state_;
  mutable std::int64_t next_alarm_occurrence_id_{0};
  std::chrono::system_clock::time_point service_started_at_{};
};

}  // namespace icp
}  // namespace virtual_factory

#endif
