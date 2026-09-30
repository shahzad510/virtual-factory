#include <virtual_factory/icp/history/AsyncHistoryWriter.hh>

#include <cstdio>
#include <type_traits>
#include <utility>

namespace virtual_factory
{
namespace icp
{

std::string defaultHistoryDatabasePath(const std::string &configurationPath)
{
  if (configurationPath.empty())
  {
    return "icp-history.sqlite";
  }
  const std::size_t slash = configurationPath.find_last_of("/\\");
  const std::string dir =
      (slash == std::string::npos) ? std::string{} : configurationPath.substr(0, slash + 1);
  return dir + "icp-history.sqlite";
}

std::string fingerprintContent(const std::string &text)
{
  std::uint64_t hash = 14695981039346656037ull;
  for (unsigned char c : text)
  {
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ull;
  }
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
  return std::string(buf);
}

AsyncHistoryWriter::AsyncHistoryWriter(
    std::shared_ptr<HistoryRepository> repository, std::size_t maxQueue)
    : max_queue_(maxQueue == 0 ? kDefaultMaxQueue : maxQueue)
    , repository_(std::move(repository))
{
  if (!this->repository_)
  {
    this->repository_ = std::make_shared<NullHistoryRepository>(
        "", "History repository was not provided");
  }
}

AsyncHistoryWriter::~AsyncHistoryWriter()
{
  this->stop();
}

void AsyncHistoryWriter::start()
{
  bool expected = false;
  if (!this->running_.compare_exchange_strong(expected, true))
  {
    return;
  }
  this->worker_ = std::thread([this]() { this->workerLoop(); });
}

void AsyncHistoryWriter::stop()
{
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->running_ = false;
  }
  this->cv_.notify_all();
  if (this->worker_.joinable())
  {
    this->worker_.join();
  }
}

bool AsyncHistoryWriter::enqueueItem(Item item)
{
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->queue_.size() >= this->max_queue_)
    {
      ++this->dropped_;
      return false;
    }
    this->queue_.push_back(std::move(item));
  }
  this->cv_.notify_one();
  return true;
}

void AsyncHistoryWriter::enqueueEvent(HistoryEventRecord record)
{
  this->enqueueItem(std::move(record));
}

void AsyncHistoryWriter::enqueueHealthTransition(HealthTransitionRecord record)
{
  this->enqueueItem(std::move(record));
}

void AsyncHistoryWriter::enqueueAlarmEvent(AlarmEventRecord record)
{
  this->enqueueItem(std::move(record));
}

void AsyncHistoryWriter::enqueueAlarmRaise(AlarmOccurrenceRecord occurrence)
{
  this->enqueueItem(std::move(occurrence));
}

void AsyncHistoryWriter::enqueueAlarmClear(
    std::int64_t occurrenceId, std::int64_t tsUtcMs, std::string message)
{
  AlarmClearItem item;
  item.occurrenceId = occurrenceId;
  item.tsUtcMs = tsUtcMs;
  item.message = std::move(message);
  this->enqueueItem(std::move(item));
}

void AsyncHistoryWriter::enqueueCommandAudit(CommandAuditRecord record)
{
  this->enqueueItem(std::move(record));
}

void AsyncHistoryWriter::enqueueConfigRevision(ConfigRevisionRecord record)
{
  this->enqueueItem(std::move(record));
}

void AsyncHistoryWriter::enqueueCommunicationTransition(
    std::string adapterId,
    std::string protocol,
    std::string newState,
    std::int64_t atUtcMs,
    std::string reason,
    std::string errorCode)
{
  CommunicationTransition item;
  item.adapterId = std::move(adapterId);
  item.protocol = std::move(protocol);
  item.newState = std::move(newState);
  item.atUtcMs = atUtcMs;
  item.reason = std::move(reason);
  item.errorCode = std::move(errorCode);
  this->enqueueItem(std::move(item));
}

void AsyncHistoryWriter::enqueueEquipmentStateTransition(
    std::string equipmentId,
    std::string adapterId,
    std::string newState,
    std::int64_t atUtcMs,
    std::string reason)
{
  EquipmentStateTransition item;
  item.equipmentId = std::move(equipmentId);
  item.adapterId = std::move(adapterId);
  item.newState = std::move(newState);
  item.atUtcMs = atUtcMs;
  item.reason = std::move(reason);
  this->enqueueItem(std::move(item));
}

HistoryStatus AsyncHistoryWriter::status() const
{
  HistoryStatus st = this->repository_->status();
  const std::uint64_t dropped = this->dropped_.load();
  st.droppedWrites += dropped;
  if (dropped > 0)
  {
    st.degraded = true;
    if (st.message.empty() || st.message == "ok")
    {
      st.message = "History write queue dropped one or more records";
    }
  }
  return st;
}

HistoryQueryResult AsyncHistoryWriter::query(const HistoryQuery &query) const
{
  HistoryQueryResult result = this->repository_->query(query);
  const std::uint64_t dropped = this->dropped_.load();
  result.status.droppedWrites += dropped;
  if (dropped > 0)
  {
    result.status.degraded = true;
  }
  return result;
}

std::shared_ptr<HistoryRepository> AsyncHistoryWriter::repository() const
{
  return this->repository_;
}

void AsyncHistoryWriter::workerLoop()
{
  auto apply = [this](Item &item) {
    std::visit(
        [this](auto &&record) {
          using T = std::decay_t<decltype(record)>;
          if constexpr (std::is_same_v<T, HistoryEventRecord>)
          {
            (void)this->repository_->appendEvent(record);
          }
          else if constexpr (std::is_same_v<T, HealthTransitionRecord>)
          {
            (void)this->repository_->appendHealthTransition(record);
          }
          else if constexpr (std::is_same_v<T, AlarmEventRecord>)
          {
            (void)this->repository_->appendAlarmEvent(record);
          }
          else if constexpr (std::is_same_v<T, AlarmOccurrenceRecord>)
          {
            (void)this->repository_->raiseAlarmOccurrence(record);
          }
          else if constexpr (std::is_same_v<T, AlarmClearItem>)
          {
            (void)this->repository_->clearAlarmOccurrence(
                record.occurrenceId, record.tsUtcMs, record.message);
          }
          else if constexpr (std::is_same_v<T, CommandAuditRecord>)
          {
            (void)this->repository_->appendCommandAudit(record);
          }
          else if constexpr (std::is_same_v<T, ConfigRevisionRecord>)
          {
            (void)this->repository_->appendConfigRevision(record);
          }
          else if constexpr (std::is_same_v<T, CommunicationTransition>)
          {
            (void)this->repository_->transitionCommunicationInterval(
                record.adapterId,
                record.protocol,
                record.newState,
                record.atUtcMs,
                record.reason,
                record.errorCode);
          }
          else if constexpr (std::is_same_v<T, EquipmentStateTransition>)
          {
            (void)this->repository_->transitionEquipmentStateInterval(
                record.equipmentId,
                record.adapterId,
                record.newState,
                record.atUtcMs,
                record.reason);
          }
        },
        item);
  };

  while (true)
  {
    Item item;
    {
      std::unique_lock<std::mutex> lock(this->mutex_);
      this->cv_.wait(lock, [&]() {
        return !this->queue_.empty() || !this->running_.load();
      });
      if (this->queue_.empty())
      {
        if (!this->running_.load())
        {
          break;
        }
        continue;
      }
      item = std::move(this->queue_.front());
      this->queue_.pop_front();
    }
    apply(item);
  }

  while (true)
  {
    Item item;
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      if (this->queue_.empty())
      {
        break;
      }
      item = std::move(this->queue_.front());
      this->queue_.pop_front();
    }
    apply(item);
  }
}

}  // namespace icp
}  // namespace virtual_factory
