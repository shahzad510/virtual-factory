#ifndef VIRTUAL_FACTORY_ICP_SQLITE_HISTORY_REPOSITORY_HH_
#define VIRTUAL_FACTORY_ICP_SQLITE_HISTORY_REPOSITORY_HH_

#include <memory>
#include <mutex>
#include <string>

#include <virtual_factory/icp/history/HistoryRepository.hh>

struct sqlite3;

namespace virtual_factory
{
namespace icp
{

/// Embedded SQLite historian. Local file only — never waits on remote services.
class SqliteHistoryRepository final : public HistoryRepository
{
public:
  static constexpr int kSchemaVersion = 2;

  /// Opens (or creates) the database. On failure, status().available == false;
  /// callers should fall back to NullHistoryRepository.
  explicit SqliteHistoryRepository(std::string databasePath);
  ~SqliteHistoryRepository() override;

  SqliteHistoryRepository(const SqliteHistoryRepository &) = delete;
  SqliteHistoryRepository &operator=(const SqliteHistoryRepository &) = delete;

  HistoryStatus status() const override;

  bool appendEvent(const HistoryEventRecord &record) override;
  bool appendHealthTransition(const HealthTransitionRecord &record) override;
  bool appendAlarmEvent(const AlarmEventRecord &record) override;
  bool raiseAlarmOccurrence(const AlarmOccurrenceRecord &occurrence) override;
  bool acknowledgeAlarmOccurrence(
      std::int64_t occurrenceId,
      std::int64_t tsUtcMs,
      const std::string &actorId) override;
  bool clearAlarmOccurrence(
      std::int64_t occurrenceId,
      std::int64_t tsUtcMs,
      const std::string &message) override;
  bool appendCommandAudit(const CommandAuditRecord &record) override;
  bool appendConfigRevision(const ConfigRevisionRecord &record) override;
  bool transitionCommunicationInterval(
      const std::string &adapterId,
      const std::string &protocol,
      const std::string &newState,
      std::int64_t atUtcMs,
      const std::string &reason,
      const std::string &errorCode) override;
  bool transitionEquipmentStateInterval(
      const std::string &equipmentId,
      const std::string &adapterId,
      const std::string &newState,
      std::int64_t atUtcMs,
      const std::string &reason) override;
  HistoryQueryResult query(const HistoryQuery &query) override;

  /// True when the constructor successfully opened and migrated the DB.
  bool openOk() const;

private:
  bool migrateLocked();
  bool execLocked(const char *sql);
  void markWriteFailureLocked(const std::string &message);
  HistoryStatus statusLocked() const;
  bool appendAlarmEventLocked(const AlarmEventRecord &record);

  mutable std::mutex mutex_;
  std::string path_;
  sqlite3 *db_{nullptr};
  bool available_{false};
  bool degraded_{false};
  std::string message_;
  int schema_version_{0};
  std::uint64_t dropped_writes_{0};
};

}  // namespace icp
}  // namespace virtual_factory

#endif
