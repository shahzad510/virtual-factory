#include <virtual_factory/icp/history/SqliteHistoryRepository.hh>

#include <sqlite3.h>

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace virtual_factory
{
namespace icp
{

namespace
{

std::string sqliteErr(sqlite3 *db)
{
  if (db == nullptr)
  {
    return "sqlite handle is null";
  }
  const char *msg = sqlite3_errmsg(db);
  return msg != nullptr ? std::string(msg) : std::string("unknown sqlite error");
}

std::string columnText(sqlite3_stmt *stmt, int col)
{
  const unsigned char *text = sqlite3_column_text(stmt, col);
  return text != nullptr ? std::string(reinterpret_cast<const char *>(text)) : std::string{};
}

}  // namespace

SqliteHistoryRepository::SqliteHistoryRepository(std::string databasePath)
    : path_(std::move(databasePath))
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->path_.empty())
  {
    this->available_ = false;
    this->degraded_ = true;
    this->message_ = "History database path is empty";
    return;
  }

  sqlite3 *db = nullptr;
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  const int rc = sqlite3_open_v2(this->path_.c_str(), &db, flags, nullptr);
  if (rc != SQLITE_OK || db == nullptr)
  {
    this->available_ = false;
    this->degraded_ = true;
    this->message_ = "Failed to open history database: "
                     + (db != nullptr ? sqliteErr(db) : std::string("open returned null"));
    if (db != nullptr)
    {
      sqlite3_close(db);
    }
    return;
  }

  this->db_ = db;
  (void)this->execLocked("PRAGMA journal_mode=WAL;");
  (void)this->execLocked("PRAGMA synchronous=NORMAL;");
  (void)this->execLocked("PRAGMA busy_timeout=100;");
  (void)this->execLocked("PRAGMA temp_store=MEMORY;");

  if (!this->migrateLocked())
  {
    this->available_ = false;
    this->degraded_ = true;
    if (this->message_.empty())
    {
      this->message_ = "History schema migration failed: " + sqliteErr(this->db_);
    }
    sqlite3_close(this->db_);
    this->db_ = nullptr;
    return;
  }

  this->available_ = true;
  this->degraded_ = false;
  this->message_ = "ok";
}

SqliteHistoryRepository::~SqliteHistoryRepository()
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->db_ != nullptr)
  {
    sqlite3_close(this->db_);
    this->db_ = nullptr;
  }
}

bool SqliteHistoryRepository::openOk() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->available_ && this->db_ != nullptr;
}

HistoryStatus SqliteHistoryRepository::status() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->statusLocked();
}

HistoryStatus SqliteHistoryRepository::statusLocked() const
{
  HistoryStatus out;
  out.available = this->available_;
  out.degraded = this->degraded_ || !this->available_;
  out.path = this->path_;
  out.message = this->message_;
  out.schemaVersion = this->schema_version_;
  out.droppedWrites = this->dropped_writes_;
  return out;
}

bool SqliteHistoryRepository::execLocked(const char *sql)
{
  char *err = nullptr;
  const int rc = sqlite3_exec(this->db_, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK)
  {
    if (err != nullptr)
    {
      this->message_ = err;
      sqlite3_free(err);
    }
    else
    {
      this->message_ = sqliteErr(this->db_);
    }
    return false;
  }
  return true;
}

void SqliteHistoryRepository::markWriteFailureLocked(const std::string &message)
{
  this->degraded_ = true;
  this->message_ = message;
  ++this->dropped_writes_;
}

bool SqliteHistoryRepository::migrateLocked()
{
  if (!this->execLocked(
          "CREATE TABLE IF NOT EXISTS schema_meta ("
          "  key TEXT PRIMARY KEY NOT NULL,"
          "  value TEXT NOT NULL"
          ");"))
  {
    return false;
  }

  int version = 0;
  {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(
            this->db_,
            "SELECT value FROM schema_meta WHERE key='version';",
            -1,
            &stmt,
            nullptr)
        == SQLITE_OK)
    {
      if (sqlite3_step(stmt) == SQLITE_ROW)
      {
        version = std::atoi(columnText(stmt, 0).c_str());
      }
      sqlite3_finalize(stmt);
    }
  }

  if (version < 1)
  {
    const char *ddl =
        "BEGIN IMMEDIATE;"
        "CREATE TABLE IF NOT EXISTS history_event ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts_utc_ms INTEGER NOT NULL,"
        "  level TEXT, category TEXT, event_type TEXT, message TEXT,"
        "  adapter_id TEXT, equipment_id TEXT, protocol TEXT,"
        "  previous_state TEXT, new_state TEXT,"
        "  previous_health TEXT, new_health TEXT,"
        "  command TEXT, reason TEXT, error_code TEXT, error_details TEXT,"
        "  node_id TEXT, recovery TEXT, correlation_id TEXT,"
        "  duration_ms INTEGER, actor_id TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_history_event_ts ON history_event(ts_utc_ms);"
        "CREATE INDEX IF NOT EXISTS idx_history_event_adapter_ts "
        "  ON history_event(adapter_id, ts_utc_ms);"
        "CREATE INDEX IF NOT EXISTS idx_history_event_equipment_ts "
        "  ON history_event(equipment_id, ts_utc_ms);"
        "CREATE INDEX IF NOT EXISTS idx_history_event_type_ts "
        "  ON history_event(event_type, ts_utc_ms);"
        "CREATE TABLE IF NOT EXISTS communication_interval ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  adapter_id TEXT NOT NULL, protocol TEXT, state TEXT NOT NULL,"
        "  started_at_utc_ms INTEGER NOT NULL, ended_at_utc_ms INTEGER,"
        "  reason TEXT, error_code TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_comm_adapter_start "
        "  ON communication_interval(adapter_id, started_at_utc_ms);"
        "CREATE INDEX IF NOT EXISTS idx_comm_start "
        "  ON communication_interval(started_at_utc_ms);"
        "CREATE TABLE IF NOT EXISTS health_transition ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  adapter_id TEXT NOT NULL, protocol TEXT,"
        "  previous_health TEXT, new_health TEXT NOT NULL,"
        "  ts_utc_ms INTEGER NOT NULL, reason TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_health_adapter_ts "
        "  ON health_transition(adapter_id, ts_utc_ms);"
        "CREATE INDEX IF NOT EXISTS idx_health_ts ON health_transition(ts_utc_ms);"
        "CREATE TABLE IF NOT EXISTS alarm_event ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  alarm_key TEXT NOT NULL, action TEXT NOT NULL, severity TEXT,"
        "  source_type TEXT, source_id TEXT, equipment_id TEXT, adapter_id TEXT,"
        "  protocol TEXT, category TEXT, message TEXT,"
        "  ts_utc_ms INTEGER NOT NULL, correlation_id TEXT, actor_id TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_alarm_ts ON alarm_event(ts_utc_ms);"
        "CREATE INDEX IF NOT EXISTS idx_alarm_adapter_ts "
        "  ON alarm_event(adapter_id, ts_utc_ms);"
        "CREATE INDEX IF NOT EXISTS idx_alarm_equipment_ts "
        "  ON alarm_event(equipment_id, ts_utc_ms);"
        "CREATE TABLE IF NOT EXISTS equipment_state_interval ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  equipment_id TEXT NOT NULL, adapter_id TEXT, state TEXT NOT NULL,"
        "  started_at_utc_ms INTEGER NOT NULL, ended_at_utc_ms INTEGER,"
        "  reason TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_eq_equipment_start "
        "  ON equipment_state_interval(equipment_id, started_at_utc_ms);"
        "CREATE INDEX IF NOT EXISTS idx_eq_start "
        "  ON equipment_state_interval(started_at_utc_ms);"
        "CREATE TABLE IF NOT EXISTS command_audit ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts_utc_ms INTEGER NOT NULL, equipment_id TEXT, adapter_id TEXT,"
        "  command TEXT, result TEXT, error_code TEXT, duration_ms INTEGER,"
        "  correlation_id TEXT, actor_id TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_command_ts ON command_audit(ts_utc_ms);"
        "CREATE INDEX IF NOT EXISTS idx_command_equipment_ts "
        "  ON command_audit(equipment_id, ts_utc_ms);"
        "CREATE TABLE IF NOT EXISTS config_revision ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts_utc_ms INTEGER NOT NULL, action TEXT NOT NULL,"
        "  configuration_name TEXT, content_hash TEXT, summary TEXT, actor_id TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_config_ts ON config_revision(ts_utc_ms);"
        "INSERT OR REPLACE INTO schema_meta(key, value) VALUES('version', '1');"
        "COMMIT;";
    if (!this->execLocked(ddl))
    {
      (void)this->execLocked("ROLLBACK;");
      return false;
    }
    version = 1;
  }

  this->schema_version_ = version;
  return version >= 1;
}

bool SqliteHistoryRepository::appendEvent(const HistoryEventRecord &record)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (!this->available_ || this->db_ == nullptr)
  {
    ++this->dropped_writes_;
    return false;
  }
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO history_event("
      "ts_utc_ms, level, category, event_type, message, adapter_id, equipment_id, "
      "protocol, previous_state, new_state, previous_health, new_health, command, "
      "reason, error_code, error_details, node_id, recovery, correlation_id, "
      "duration_ms, actor_id) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
  if (sqlite3_prepare_v2(this->db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    this->markWriteFailureLocked(sqliteErr(this->db_));
    return false;
  }
  int i = 1;
  sqlite3_bind_int64(stmt, i++, record.tsUtcMs);
  sqlite3_bind_text(stmt, i++, record.level.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.category.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.eventType.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.message.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.adapterId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.equipmentId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.protocol.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.previousState.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.newState.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.previousHealth.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.newHealth.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.command.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.reason.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.errorCode.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.errorDetails.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.nodeId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.recovery.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.correlationId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, i++, record.durationMs);
  sqlite3_bind_text(stmt, i++, record.actorId.c_str(), -1, SQLITE_TRANSIENT);
  const int step = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (step != SQLITE_DONE)
  {
    this->markWriteFailureLocked(sqliteErr(this->db_));
    return false;
  }
  return true;
}

bool SqliteHistoryRepository::appendHealthTransition(const HealthTransitionRecord &record)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (!this->available_ || this->db_ == nullptr)
  {
    ++this->dropped_writes_;
    return false;
  }
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO health_transition("
      "adapter_id, protocol, previous_health, new_health, ts_utc_ms, reason) "
      "VALUES(?,?,?,?,?,?);";
  if (sqlite3_prepare_v2(this->db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    this->markWriteFailureLocked(sqliteErr(this->db_));
    return false;
  }
  sqlite3_bind_text(stmt, 1, record.adapterId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, record.protocol.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, record.previousHealth.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, record.newHealth.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 5, record.tsUtcMs);
  sqlite3_bind_text(stmt, 6, record.reason.c_str(), -1, SQLITE_TRANSIENT);
  const int step = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (step != SQLITE_DONE)
  {
    this->markWriteFailureLocked(sqliteErr(this->db_));
    return false;
  }
  return true;
}

bool SqliteHistoryRepository::appendAlarmEvent(const AlarmEventRecord &record)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (!this->available_ || this->db_ == nullptr)
  {
    ++this->dropped_writes_;
    return false;
  }
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO alarm_event("
      "alarm_key, action, severity, source_type, source_id, equipment_id, "
      "adapter_id, protocol, category, message, ts_utc_ms, correlation_id, actor_id) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?);";
  if (sqlite3_prepare_v2(this->db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    this->markWriteFailureLocked(sqliteErr(this->db_));
    return false;
  }
  int i = 1;
  sqlite3_bind_text(stmt, i++, record.alarmKey.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.action.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.severity.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.sourceType.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.sourceId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.equipmentId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.adapterId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.protocol.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.category.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.message.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, i++, record.tsUtcMs);
  sqlite3_bind_text(stmt, i++, record.correlationId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.actorId.c_str(), -1, SQLITE_TRANSIENT);
  const int step = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (step != SQLITE_DONE)
  {
    this->markWriteFailureLocked(sqliteErr(this->db_));
    return false;
  }
  return true;
}

bool SqliteHistoryRepository::appendCommandAudit(const CommandAuditRecord &record)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (!this->available_ || this->db_ == nullptr)
  {
    ++this->dropped_writes_;
    return false;
  }
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO command_audit("
      "ts_utc_ms, equipment_id, adapter_id, command, result, error_code, "
      "duration_ms, correlation_id, actor_id) VALUES(?,?,?,?,?,?,?,?,?);";
  if (sqlite3_prepare_v2(this->db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    this->markWriteFailureLocked(sqliteErr(this->db_));
    return false;
  }
  int i = 1;
  sqlite3_bind_int64(stmt, i++, record.tsUtcMs);
  sqlite3_bind_text(stmt, i++, record.equipmentId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.adapterId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.command.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.result.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.errorCode.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, i++, record.durationMs);
  sqlite3_bind_text(stmt, i++, record.correlationId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, i++, record.actorId.c_str(), -1, SQLITE_TRANSIENT);
  const int step = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (step != SQLITE_DONE)
  {
    this->markWriteFailureLocked(sqliteErr(this->db_));
    return false;
  }
  return true;
}

bool SqliteHistoryRepository::appendConfigRevision(const ConfigRevisionRecord &record)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (!this->available_ || this->db_ == nullptr)
  {
    ++this->dropped_writes_;
    return false;
  }
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO config_revision("
      "ts_utc_ms, action, configuration_name, content_hash, summary, actor_id) "
      "VALUES(?,?,?,?,?,?);";
  if (sqlite3_prepare_v2(this->db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    this->markWriteFailureLocked(sqliteErr(this->db_));
    return false;
  }
  sqlite3_bind_int64(stmt, 1, record.tsUtcMs);
  sqlite3_bind_text(stmt, 2, record.action.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, record.configurationName.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, record.contentHash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, record.summary.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, record.actorId.c_str(), -1, SQLITE_TRANSIENT);
  const int step = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (step != SQLITE_DONE)
  {
    this->markWriteFailureLocked(sqliteErr(this->db_));
    return false;
  }
  return true;
}

bool SqliteHistoryRepository::transitionCommunicationInterval(
    const std::string &adapterId,
    const std::string &protocol,
    const std::string &newState,
    std::int64_t atUtcMs,
    const std::string &reason,
    const std::string &errorCode)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (!this->available_ || this->db_ == nullptr)
  {
    ++this->dropped_writes_;
    return false;
  }
  if (!this->execLocked("BEGIN IMMEDIATE;"))
  {
    this->markWriteFailureLocked(this->message_);
    return false;
  }
  {
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "UPDATE communication_interval SET ended_at_utc_ms=? "
        "WHERE adapter_id=? AND ended_at_utc_ms IS NULL;";
    if (sqlite3_prepare_v2(this->db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
      (void)this->execLocked("ROLLBACK;");
      this->markWriteFailureLocked(sqliteErr(this->db_));
      return false;
    }
    sqlite3_bind_int64(stmt, 1, atUtcMs);
    sqlite3_bind_text(stmt, 2, adapterId.c_str(), -1, SQLITE_TRANSIENT);
    const int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step != SQLITE_DONE)
    {
      (void)this->execLocked("ROLLBACK;");
      this->markWriteFailureLocked(sqliteErr(this->db_));
      return false;
    }
  }
  {
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO communication_interval("
        "adapter_id, protocol, state, started_at_utc_ms, ended_at_utc_ms, "
        "reason, error_code) VALUES(?,?,?,?,NULL,?,?);";
    if (sqlite3_prepare_v2(this->db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
      (void)this->execLocked("ROLLBACK;");
      this->markWriteFailureLocked(sqliteErr(this->db_));
      return false;
    }
    sqlite3_bind_text(stmt, 1, adapterId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, protocol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, newState.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, atUtcMs);
    sqlite3_bind_text(stmt, 5, reason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, errorCode.c_str(), -1, SQLITE_TRANSIENT);
    const int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step != SQLITE_DONE)
    {
      (void)this->execLocked("ROLLBACK;");
      this->markWriteFailureLocked(sqliteErr(this->db_));
      return false;
    }
  }
  if (!this->execLocked("COMMIT;"))
  {
    (void)this->execLocked("ROLLBACK;");
    this->markWriteFailureLocked(this->message_);
    return false;
  }
  return true;
}

bool SqliteHistoryRepository::transitionEquipmentStateInterval(
    const std::string &equipmentId,
    const std::string &adapterId,
    const std::string &newState,
    std::int64_t atUtcMs,
    const std::string &reason)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (!this->available_ || this->db_ == nullptr)
  {
    ++this->dropped_writes_;
    return false;
  }
  if (!this->execLocked("BEGIN IMMEDIATE;"))
  {
    this->markWriteFailureLocked(this->message_);
    return false;
  }
  {
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "UPDATE equipment_state_interval SET ended_at_utc_ms=? "
        "WHERE equipment_id=? AND ended_at_utc_ms IS NULL;";
    if (sqlite3_prepare_v2(this->db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
      (void)this->execLocked("ROLLBACK;");
      this->markWriteFailureLocked(sqliteErr(this->db_));
      return false;
    }
    sqlite3_bind_int64(stmt, 1, atUtcMs);
    sqlite3_bind_text(stmt, 2, equipmentId.c_str(), -1, SQLITE_TRANSIENT);
    const int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step != SQLITE_DONE)
    {
      (void)this->execLocked("ROLLBACK;");
      this->markWriteFailureLocked(sqliteErr(this->db_));
      return false;
    }
  }
  {
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO equipment_state_interval("
        "equipment_id, adapter_id, state, started_at_utc_ms, ended_at_utc_ms, reason) "
        "VALUES(?,?,?,?,NULL,?);";
    if (sqlite3_prepare_v2(this->db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
      (void)this->execLocked("ROLLBACK;");
      this->markWriteFailureLocked(sqliteErr(this->db_));
      return false;
    }
    sqlite3_bind_text(stmt, 1, equipmentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, adapterId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, newState.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, atUtcMs);
    sqlite3_bind_text(stmt, 5, reason.c_str(), -1, SQLITE_TRANSIENT);
    const int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step != SQLITE_DONE)
    {
      (void)this->execLocked("ROLLBACK;");
      this->markWriteFailureLocked(sqliteErr(this->db_));
      return false;
    }
  }
  if (!this->execLocked("COMMIT;"))
  {
    (void)this->execLocked("ROLLBACK;");
    this->markWriteFailureLocked(this->message_);
    return false;
  }
  return true;
}

HistoryQueryResult SqliteHistoryRepository::query(const HistoryQuery &query)
{
  HistoryQueryResult out;
  out.kind = query.kind.empty() ? "events" : query.kind;
  std::lock_guard<std::mutex> lock(this->mutex_);
  out.status = this->statusLocked();
  if (!this->available_ || this->db_ == nullptr)
  {
    return out;
  }

  const std::size_t limit =
      query.limit == 0 ? 100 : std::min(query.limit, static_cast<std::size_t>(1000));

  auto appendTimeAdapterFilters = [&](std::ostringstream &sql,
                                      const char *tsCol,
                                      bool withAdapter,
                                      bool withEquipment,
                                      bool withProtocol) {
    sql << " WHERE 1=1";
    if (query.startUtcMs.has_value())
    {
      sql << " AND " << tsCol << " >= ?";
    }
    if (query.endUtcMs.has_value())
    {
      sql << " AND " << tsCol << " <= ?";
    }
    if (withAdapter && !query.adapterId.empty())
    {
      sql << " AND adapter_id = ?";
    }
    if (withEquipment && !query.equipmentId.empty())
    {
      sql << " AND equipment_id = ?";
    }
    if (withProtocol && !query.protocol.empty())
    {
      sql << " AND protocol = ?";
    }
  };

  auto bindTimeAdapterFilters = [&](sqlite3_stmt *stmt,
                                    int &idx,
                                    bool withAdapter,
                                    bool withEquipment,
                                    bool withProtocol) {
    if (query.startUtcMs.has_value())
    {
      sqlite3_bind_int64(stmt, idx++, *query.startUtcMs);
    }
    if (query.endUtcMs.has_value())
    {
      sqlite3_bind_int64(stmt, idx++, *query.endUtcMs);
    }
    if (withAdapter && !query.adapterId.empty())
    {
      sqlite3_bind_text(stmt, idx++, query.adapterId.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (withEquipment && !query.equipmentId.empty())
    {
      sqlite3_bind_text(stmt, idx++, query.equipmentId.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (withProtocol && !query.protocol.empty())
    {
      sqlite3_bind_text(stmt, idx++, query.protocol.c_str(), -1, SQLITE_TRANSIENT);
    }
  };

  if (out.kind == "events")
  {
    std::ostringstream sql;
    sql << "SELECT id, ts_utc_ms, level, category, event_type, message, adapter_id, "
           "equipment_id, protocol, previous_state, new_state, previous_health, "
           "new_health, command, reason, error_code, error_details, node_id, recovery, "
           "correlation_id, duration_ms, actor_id FROM history_event";
    appendTimeAdapterFilters(sql, "ts_utc_ms", true, true, true);
    if (!query.category.empty())
    {
      sql << " AND category = ?";
    }
    if (!query.eventType.empty())
    {
      sql << " AND event_type = ?";
    }
    sql << " ORDER BY ts_utc_ms ASC, id ASC LIMIT " << limit;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(this->db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
      out.status.degraded = true;
      out.status.message = sqliteErr(this->db_);
      return out;
    }
    int idx = 1;
    bindTimeAdapterFilters(stmt, idx, true, true, true);
    if (!query.category.empty())
    {
      sqlite3_bind_text(stmt, idx++, query.category.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!query.eventType.empty())
    {
      sqlite3_bind_text(stmt, idx++, query.eventType.c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      HistoryEventRecord row;
      row.id = sqlite3_column_int64(stmt, 0);
      row.tsUtcMs = sqlite3_column_int64(stmt, 1);
      row.level = columnText(stmt, 2);
      row.category = columnText(stmt, 3);
      row.eventType = columnText(stmt, 4);
      row.message = columnText(stmt, 5);
      row.adapterId = columnText(stmt, 6);
      row.equipmentId = columnText(stmt, 7);
      row.protocol = columnText(stmt, 8);
      row.previousState = columnText(stmt, 9);
      row.newState = columnText(stmt, 10);
      row.previousHealth = columnText(stmt, 11);
      row.newHealth = columnText(stmt, 12);
      row.command = columnText(stmt, 13);
      row.reason = columnText(stmt, 14);
      row.errorCode = columnText(stmt, 15);
      row.errorDetails = columnText(stmt, 16);
      row.nodeId = columnText(stmt, 17);
      row.recovery = columnText(stmt, 18);
      row.correlationId = columnText(stmt, 19);
      row.durationMs = sqlite3_column_int64(stmt, 20);
      row.actorId = columnText(stmt, 21);
      out.events.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
  }
  else if (out.kind == "communication_intervals")
  {
    std::ostringstream sql;
    sql << "SELECT id, adapter_id, protocol, state, started_at_utc_ms, ended_at_utc_ms, "
           "reason, error_code FROM communication_interval";
    appendTimeAdapterFilters(sql, "started_at_utc_ms", true, false, true);
    sql << " ORDER BY started_at_utc_ms ASC, id ASC LIMIT " << limit;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(this->db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
      out.status.degraded = true;
      out.status.message = sqliteErr(this->db_);
      return out;
    }
    int idx = 1;
    bindTimeAdapterFilters(stmt, idx, true, false, true);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      CommunicationIntervalRecord row;
      row.id = sqlite3_column_int64(stmt, 0);
      row.adapterId = columnText(stmt, 1);
      row.protocol = columnText(stmt, 2);
      row.state = columnText(stmt, 3);
      row.startedAtUtcMs = sqlite3_column_int64(stmt, 4);
      if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
      {
        row.endedAtUtcMs = sqlite3_column_int64(stmt, 5);
      }
      row.reason = columnText(stmt, 6);
      row.errorCode = columnText(stmt, 7);
      out.communicationIntervals.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
  }
  else if (out.kind == "health_transitions")
  {
    std::ostringstream sql;
    sql << "SELECT id, adapter_id, protocol, previous_health, new_health, ts_utc_ms, reason "
           "FROM health_transition";
    appendTimeAdapterFilters(sql, "ts_utc_ms", true, false, true);
    sql << " ORDER BY ts_utc_ms ASC, id ASC LIMIT " << limit;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(this->db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
      out.status.degraded = true;
      out.status.message = sqliteErr(this->db_);
      return out;
    }
    int idx = 1;
    bindTimeAdapterFilters(stmt, idx, true, false, true);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      HealthTransitionRecord row;
      row.id = sqlite3_column_int64(stmt, 0);
      row.adapterId = columnText(stmt, 1);
      row.protocol = columnText(stmt, 2);
      row.previousHealth = columnText(stmt, 3);
      row.newHealth = columnText(stmt, 4);
      row.tsUtcMs = sqlite3_column_int64(stmt, 5);
      row.reason = columnText(stmt, 6);
      out.healthTransitions.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
  }
  else if (out.kind == "alarm_events")
  {
    std::ostringstream sql;
    sql << "SELECT id, alarm_key, action, severity, source_type, source_id, equipment_id, "
           "adapter_id, protocol, category, message, ts_utc_ms, correlation_id, actor_id "
           "FROM alarm_event";
    appendTimeAdapterFilters(sql, "ts_utc_ms", true, true, true);
    if (!query.severity.empty())
    {
      sql << " AND severity = ?";
    }
    if (!query.category.empty())
    {
      sql << " AND category = ?";
    }
    sql << " ORDER BY ts_utc_ms ASC, id ASC LIMIT " << limit;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(this->db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
      out.status.degraded = true;
      out.status.message = sqliteErr(this->db_);
      return out;
    }
    int idx = 1;
    bindTimeAdapterFilters(stmt, idx, true, true, true);
    if (!query.severity.empty())
    {
      sqlite3_bind_text(stmt, idx++, query.severity.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!query.category.empty())
    {
      sqlite3_bind_text(stmt, idx++, query.category.c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      AlarmEventRecord row;
      row.id = sqlite3_column_int64(stmt, 0);
      row.alarmKey = columnText(stmt, 1);
      row.action = columnText(stmt, 2);
      row.severity = columnText(stmt, 3);
      row.sourceType = columnText(stmt, 4);
      row.sourceId = columnText(stmt, 5);
      row.equipmentId = columnText(stmt, 6);
      row.adapterId = columnText(stmt, 7);
      row.protocol = columnText(stmt, 8);
      row.category = columnText(stmt, 9);
      row.message = columnText(stmt, 10);
      row.tsUtcMs = sqlite3_column_int64(stmt, 11);
      row.correlationId = columnText(stmt, 12);
      row.actorId = columnText(stmt, 13);
      out.alarmEvents.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
  }
  else if (out.kind == "equipment_state_intervals")
  {
    std::ostringstream sql;
    sql << "SELECT id, equipment_id, adapter_id, state, started_at_utc_ms, ended_at_utc_ms, "
           "reason FROM equipment_state_interval";
    appendTimeAdapterFilters(sql, "started_at_utc_ms", true, true, false);
    sql << " ORDER BY started_at_utc_ms ASC, id ASC LIMIT " << limit;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(this->db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
      out.status.degraded = true;
      out.status.message = sqliteErr(this->db_);
      return out;
    }
    int idx = 1;
    bindTimeAdapterFilters(stmt, idx, true, true, false);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      EquipmentStateIntervalRecord row;
      row.id = sqlite3_column_int64(stmt, 0);
      row.equipmentId = columnText(stmt, 1);
      row.adapterId = columnText(stmt, 2);
      row.state = columnText(stmt, 3);
      row.startedAtUtcMs = sqlite3_column_int64(stmt, 4);
      if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
      {
        row.endedAtUtcMs = sqlite3_column_int64(stmt, 5);
      }
      row.reason = columnText(stmt, 6);
      out.equipmentStateIntervals.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
  }
  else if (out.kind == "command_audit")
  {
    std::ostringstream sql;
    sql << "SELECT id, ts_utc_ms, equipment_id, adapter_id, command, result, error_code, "
           "duration_ms, correlation_id, actor_id FROM command_audit";
    appendTimeAdapterFilters(sql, "ts_utc_ms", true, true, false);
    sql << " ORDER BY ts_utc_ms ASC, id ASC LIMIT " << limit;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(this->db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
      out.status.degraded = true;
      out.status.message = sqliteErr(this->db_);
      return out;
    }
    int idx = 1;
    bindTimeAdapterFilters(stmt, idx, true, true, false);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      CommandAuditRecord row;
      row.id = sqlite3_column_int64(stmt, 0);
      row.tsUtcMs = sqlite3_column_int64(stmt, 1);
      row.equipmentId = columnText(stmt, 2);
      row.adapterId = columnText(stmt, 3);
      row.command = columnText(stmt, 4);
      row.result = columnText(stmt, 5);
      row.errorCode = columnText(stmt, 6);
      row.durationMs = sqlite3_column_int64(stmt, 7);
      row.correlationId = columnText(stmt, 8);
      row.actorId = columnText(stmt, 9);
      out.commandAudits.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
  }
  else if (out.kind == "config_revisions")
  {
    std::ostringstream sql;
    sql << "SELECT id, ts_utc_ms, action, configuration_name, content_hash, summary, actor_id "
           "FROM config_revision WHERE 1=1";
    if (query.startUtcMs.has_value())
    {
      sql << " AND ts_utc_ms >= ?";
    }
    if (query.endUtcMs.has_value())
    {
      sql << " AND ts_utc_ms <= ?";
    }
    sql << " ORDER BY ts_utc_ms ASC, id ASC LIMIT " << limit;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(this->db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
      out.status.degraded = true;
      out.status.message = sqliteErr(this->db_);
      return out;
    }
    int idx = 1;
    if (query.startUtcMs.has_value())
    {
      sqlite3_bind_int64(stmt, idx++, *query.startUtcMs);
    }
    if (query.endUtcMs.has_value())
    {
      sqlite3_bind_int64(stmt, idx++, *query.endUtcMs);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      ConfigRevisionRecord row;
      row.id = sqlite3_column_int64(stmt, 0);
      row.tsUtcMs = sqlite3_column_int64(stmt, 1);
      row.action = columnText(stmt, 2);
      row.configurationName = columnText(stmt, 3);
      row.contentHash = columnText(stmt, 4);
      row.summary = columnText(stmt, 5);
      row.actorId = columnText(stmt, 6);
      out.configRevisions.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
  }
  else
  {
    out.status.degraded = true;
    out.status.message = "Unknown history kind: " + out.kind;
  }

  return out;
}

}  // namespace icp
}  // namespace virtual_factory
