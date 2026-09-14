#include <virtual_factory/icp/history/HistoryRepository.hh>

namespace virtual_factory
{
namespace icp
{

NullHistoryRepository::NullHistoryRepository(std::string path, std::string message)
{
  status_.available = false;
  status_.degraded = true;
  status_.path = std::move(path);
  status_.message = std::move(message);
  status_.schemaVersion = 0;
  status_.droppedWrites = 0;
}

HistoryStatus NullHistoryRepository::status() const
{
  return status_;
}

bool NullHistoryRepository::appendEvent(const HistoryEventRecord &)
{
  return false;
}

bool NullHistoryRepository::appendHealthTransition(const HealthTransitionRecord &)
{
  return false;
}

bool NullHistoryRepository::appendAlarmEvent(const AlarmEventRecord &)
{
  return false;
}

bool NullHistoryRepository::appendCommandAudit(const CommandAuditRecord &)
{
  return false;
}

bool NullHistoryRepository::appendConfigRevision(const ConfigRevisionRecord &)
{
  return false;
}

bool NullHistoryRepository::transitionCommunicationInterval(
    const std::string &,
    const std::string &,
    const std::string &,
    std::int64_t,
    const std::string &,
    const std::string &)
{
  return false;
}

bool NullHistoryRepository::transitionEquipmentStateInterval(
    const std::string &,
    const std::string &,
    const std::string &,
    std::int64_t,
    const std::string &)
{
  return false;
}

HistoryQueryResult NullHistoryRepository::query(const HistoryQuery &query)
{
  HistoryQueryResult out;
  out.status = status_;
  out.kind = query.kind.empty() ? "events" : query.kind;
  return out;
}

}  // namespace icp
}  // namespace virtual_factory
