#ifndef VIRTUAL_FACTORY_ICP_ASYNC_HISTORY_WRITER_HH_
#define VIRTUAL_FACTORY_ICP_ASYNC_HISTORY_WRITER_HH_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>

#include <virtual_factory/icp/history/HistoryRepository.hh>
#include <virtual_factory/icp/history/HistoryTypes.hh>

namespace virtual_factory
{
namespace icp
{

/// Bounded async queue in front of HistoryRepository.
/// enqueue*() never performs SQLite I/O and must stay off protocol I/O locks
/// for long durations (push under a short queue mutex only).
class AsyncHistoryWriter
{
public:
  static constexpr std::size_t kDefaultMaxQueue = 10000;

  explicit AsyncHistoryWriter(
      std::shared_ptr<HistoryRepository> repository,
      std::size_t maxQueue = kDefaultMaxQueue);
  ~AsyncHistoryWriter();

  AsyncHistoryWriter(const AsyncHistoryWriter &) = delete;
  AsyncHistoryWriter &operator=(const AsyncHistoryWriter &) = delete;

  void start();
  void stop();

  void enqueueEvent(HistoryEventRecord record);
  void enqueueHealthTransition(HealthTransitionRecord record);
  void enqueueAlarmEvent(AlarmEventRecord record);
  void enqueueCommandAudit(CommandAuditRecord record);
  void enqueueConfigRevision(ConfigRevisionRecord record);
  void enqueueCommunicationTransition(
      std::string adapterId,
      std::string protocol,
      std::string newState,
      std::int64_t atUtcMs,
      std::string reason,
      std::string errorCode);
  void enqueueEquipmentStateTransition(
      std::string equipmentId,
      std::string adapterId,
      std::string newState,
      std::int64_t atUtcMs,
      std::string reason);

  HistoryStatus status() const;
  HistoryQueryResult query(const HistoryQuery &query) const;
  std::shared_ptr<HistoryRepository> repository() const;

private:
  struct CommunicationTransition
  {
    std::string adapterId;
    std::string protocol;
    std::string newState;
    std::int64_t atUtcMs{0};
    std::string reason;
    std::string errorCode;
  };

  struct EquipmentStateTransition
  {
    std::string equipmentId;
    std::string adapterId;
    std::string newState;
    std::int64_t atUtcMs{0};
    std::string reason;
  };

  using Item = std::variant<
      HistoryEventRecord,
      HealthTransitionRecord,
      AlarmEventRecord,
      CommandAuditRecord,
      ConfigRevisionRecord,
      CommunicationTransition,
      EquipmentStateTransition>;

  void workerLoop();
  bool enqueueItem(Item item);

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Item> queue_;
  std::size_t max_queue_;
  std::shared_ptr<HistoryRepository> repository_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> dropped_{0};
};

/// Derive a local SQLite path next to the ICP configuration file.
std::string defaultHistoryDatabasePath(const std::string &configurationPath);

/// Non-cryptographic content fingerprint (FNV-1a 64-bit hex). Never store secrets.
std::string fingerprintContent(const std::string &text);

}  // namespace icp
}  // namespace virtual_factory

#endif
