#ifndef VIRTUAL_FACTORY_ICP_HISTORY_TYPES_HH_
#define VIRTUAL_FACTORY_ICP_HISTORY_TYPES_HH_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace virtual_factory
{
namespace icp
{

/// Historian availability — independent of protocol adapter health.
struct HistoryStatus
{
  bool available{false};
  bool degraded{false};
  std::string path;
  std::string message;
  int schemaVersion{0};
  std::uint64_t droppedWrites{0};
};

struct HistoryEventRecord
{
  std::int64_t id{0};
  std::int64_t tsUtcMs{0};
  std::string level;
  std::string category;
  std::string eventType;
  std::string message;
  std::string adapterId;
  std::string equipmentId;
  std::string protocol;
  std::string previousState;
  std::string newState;
  std::string previousHealth;
  std::string newHealth;
  std::string command;
  std::string reason;
  std::string errorCode;
  std::string errorDetails;
  std::string nodeId;
  std::string recovery;
  std::string correlationId;
  std::int64_t durationMs{-1};
  std::string actorId;  // nullable until Milestone 2
};

struct CommunicationIntervalRecord
{
  std::int64_t id{0};
  std::string adapterId;
  std::string protocol;
  std::string state;
  std::int64_t startedAtUtcMs{0};
  std::optional<std::int64_t> endedAtUtcMs;
  std::string reason;
  std::string errorCode;
};

struct HealthTransitionRecord
{
  std::int64_t id{0};
  std::string adapterId;
  std::string protocol;
  std::string previousHealth;
  std::string newHealth;
  std::int64_t tsUtcMs{0};
  std::string reason;
};

struct AlarmEventRecord
{
  std::int64_t id{0};
  std::string alarmKey;
  std::string action;  // raised | cleared | acknowledged
  std::string severity;
  std::string sourceType;
  std::string sourceId;
  std::string equipmentId;
  std::string adapterId;
  std::string protocol;
  std::string category;
  std::string message;
  std::int64_t tsUtcMs{0};
  std::string correlationId;
  std::string actorId;  // nullable until Milestone 2
};

struct EquipmentStateIntervalRecord
{
  std::int64_t id{0};
  std::string equipmentId;
  std::string adapterId;
  std::string state;  // Running | Stopped | Faulted
  std::int64_t startedAtUtcMs{0};
  std::optional<std::int64_t> endedAtUtcMs;
  std::string reason;
};

struct CommandAuditRecord
{
  std::int64_t id{0};
  std::int64_t tsUtcMs{0};
  std::string equipmentId;
  std::string adapterId;
  std::string command;
  std::string result;
  std::string errorCode;
  std::int64_t durationMs{-1};
  std::string correlationId;
  std::string actorId;  // nullable until Milestone 2
};

struct ConfigRevisionRecord
{
  std::int64_t id{0};
  std::int64_t tsUtcMs{0};
  std::string action;
  std::string configurationName;
  std::string contentHash;
  std::string summary;
  std::string actorId;  // nullable until Milestone 2
};

struct HistoryQuery
{
  std::string kind{"events"};  // events|communication_intervals|health_transitions|
                               // alarm_events|equipment_state_intervals|command_audit|
                               // config_revisions
  std::optional<std::int64_t> startUtcMs;
  std::optional<std::int64_t> endUtcMs;
  std::string adapterId;
  std::string equipmentId;
  std::string protocol;
  std::string category;
  std::string eventType;
  std::string severity;
  std::size_t limit{100};
};

struct HistoryQueryResult
{
  HistoryStatus status;
  std::string kind;
  std::vector<HistoryEventRecord> events;
  std::vector<CommunicationIntervalRecord> communicationIntervals;
  std::vector<HealthTransitionRecord> healthTransitions;
  std::vector<AlarmEventRecord> alarmEvents;
  std::vector<EquipmentStateIntervalRecord> equipmentStateIntervals;
  std::vector<CommandAuditRecord> commandAudits;
  std::vector<ConfigRevisionRecord> configRevisions;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
