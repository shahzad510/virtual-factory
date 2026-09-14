#ifndef VIRTUAL_FACTORY_ICP_HISTORY_REPOSITORY_HH_
#define VIRTUAL_FACTORY_ICP_HISTORY_REPOSITORY_HH_

#include <virtual_factory/icp/history/HistoryTypes.hh>

namespace virtual_factory
{
namespace icp
{

/// Application-level historical persistence. Protocol adapters must not depend
/// on this interface. Opening/using history must never gate ICP startup or
/// protocol I/O; failures surface only via HistoryStatus.
class HistoryRepository
{
public:
  virtual ~HistoryRepository() = default;

  virtual HistoryStatus status() const = 0;

  virtual bool appendEvent(const HistoryEventRecord &record) = 0;
  virtual bool appendHealthTransition(const HealthTransitionRecord &record) = 0;
  virtual bool appendAlarmEvent(const AlarmEventRecord &record) = 0;
  virtual bool appendCommandAudit(const CommandAuditRecord &record) = 0;
  virtual bool appendConfigRevision(const ConfigRevisionRecord &record) = 0;

  /// Close any open communication interval for adapterId, then open a new one.
  virtual bool transitionCommunicationInterval(
      const std::string &adapterId,
      const std::string &protocol,
      const std::string &newState,
      std::int64_t atUtcMs,
      const std::string &reason,
      const std::string &errorCode) = 0;

  /// Close any open equipment state interval, then open a new one when state changes.
  virtual bool transitionEquipmentStateInterval(
      const std::string &equipmentId,
      const std::string &adapterId,
      const std::string &newState,
      std::int64_t atUtcMs,
      const std::string &reason) = 0;

  virtual HistoryQueryResult query(const HistoryQuery &query) = 0;
};

/// No-op historian used when the database cannot be opened. ICP continues.
class NullHistoryRepository final : public HistoryRepository
{
public:
  explicit NullHistoryRepository(std::string path, std::string message);

  HistoryStatus status() const override;
  bool appendEvent(const HistoryEventRecord &) override;
  bool appendHealthTransition(const HealthTransitionRecord &) override;
  bool appendAlarmEvent(const AlarmEventRecord &) override;
  bool appendCommandAudit(const CommandAuditRecord &) override;
  bool appendConfigRevision(const ConfigRevisionRecord &) override;
  bool transitionCommunicationInterval(
      const std::string &,
      const std::string &,
      const std::string &,
      std::int64_t,
      const std::string &,
      const std::string &) override;
  bool transitionEquipmentStateInterval(
      const std::string &,
      const std::string &,
      const std::string &,
      std::int64_t,
      const std::string &) override;
  HistoryQueryResult query(const HistoryQuery &query) override;

private:
  HistoryStatus status_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
