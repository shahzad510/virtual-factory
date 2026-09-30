#include <virtual_factory/icp/RecoveryService.hh>

#include <algorithm>
#include <utility>

namespace virtual_factory
{
namespace icp
{

const char *recoveryStateName(RecoveryState state)
{
  switch (state)
  {
  case RecoveryState::Disabled:
    return "DISABLED";
  case RecoveryState::Connected:
    return "CONNECTED";
  case RecoveryState::RecoveryWait:
    return "RECOVERY_WAIT";
  case RecoveryState::Probing:
    return "PROBING";
  }
  return "DISABLED";
}

std::chrono::steady_clock::time_point SteadyRecoveryClock::now() const
{
  return std::chrono::steady_clock::now();
}

FakeRecoveryClock::FakeRecoveryClock()
    : current_(std::chrono::steady_clock::time_point(
          std::chrono::milliseconds(1)))
{
}

FakeRecoveryClock::FakeRecoveryClock(std::chrono::steady_clock::time_point start)
    : current_(start)
{
}

void FakeRecoveryClock::set(std::chrono::steady_clock::time_point tp)
{
  this->current_ = tp;
}

void FakeRecoveryClock::advance(std::chrono::milliseconds delta)
{
  this->current_ += delta;
}

std::chrono::steady_clock::time_point FakeRecoveryClock::now() const
{
  return this->current_;
}

RecoveryService::RecoveryService()
    : RecoveryService(std::make_shared<SteadyRecoveryClock>())
{
}

RecoveryService::RecoveryService(std::shared_ptr<RecoveryClock> clock)
    : clock_(std::move(clock))
{
  if (!this->clock_)
  {
    this->clock_ = std::make_shared<SteadyRecoveryClock>();
  }
}

RecoveryService::~RecoveryService()
{
  this->stop();
}

void RecoveryService::start()
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->running_)
  {
    return;
  }
  this->stopping_ = false;
  this->running_ = true;
  this->thread_ = std::thread([this]() { this->threadMain(); });
}

void RecoveryService::stop()
{
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (!this->running_ && !this->thread_.joinable())
    {
      return;
    }
    this->stopping_ = true;
    this->running_ = false;
  }
  this->cv_.notify_all();
  if (this->thread_.joinable())
  {
    this->thread_.join();
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->stopping_ = false;
}

bool RecoveryService::running() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->running_;
}

void RecoveryService::armDesired(const std::string &adapterId, bool immediate)
{
  if (adapterId.empty())
  {
    return;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->stopping_)
  {
    return;
  }
  ControlEntry &entry = this->ensureLocked(adapterId);
  entry.control.desired = true;
  if (entry.control.state == RecoveryState::Connected)
  {
    return;
  }
  if (entry.control.state == RecoveryState::Probing)
  {
    return;
  }
  const auto now = this->nowLocked();
  const auto when = immediate ? now : now + entry.control.backoffMs;
  this->enterWaitLocked(entry, when);
  this->notifyThreadLocked();
}

void RecoveryService::setDesired(const std::string &adapterId, bool desired)
{
  if (adapterId.empty())
  {
    return;
  }
  if (!desired)
  {
    this->disable(adapterId);
    return;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->stopping_)
  {
    return;
  }
  ControlEntry &entry = this->ensureLocked(adapterId);
  entry.control.desired = true;
  // Intent only. Automatic retry is armed by armDesired (startup/config)
  // or notifyUnavailable (runtime left CONNECTED). Operator Connect must
  // not start a second timer-driven recovery schedule.
}

void RecoveryService::disable(const std::string &adapterId)
{
  if (adapterId.empty())
  {
    return;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  ControlEntry &entry = this->ensureLocked(adapterId);
  entry.control.desired = false;
  entry.control.state = RecoveryState::Disabled;
  this->clearScheduleLocked(entry);
  this->resetBackoffLocked(entry);
  entry.control.consecutiveFailures = 0;
  this->notifyThreadLocked();
}

void RecoveryService::remove(const std::string &adapterId)
{
  if (adapterId.empty())
  {
    return;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->controls_.erase(adapterId);
  this->notifyThreadLocked();
}

void RecoveryService::rematerialize(const std::string &adapterId)
{
  if (adapterId.empty())
  {
    return;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  auto it = this->controls_.find(adapterId);
  if (it == this->controls_.end())
  {
    return;
  }
  ControlEntry &entry = it->second;
  ++entry.control.runtimeEpoch;
  entry.control.activeProbeId = 0;
  if (!entry.control.desired)
  {
    entry.control.state = RecoveryState::Disabled;
    this->clearScheduleLocked(entry);
    this->notifyThreadLocked();
    return;
  }
  if (entry.control.state == RecoveryState::Connected)
  {
    this->notifyThreadLocked();
    return;
  }
  // New runtime: previous logical probe identity is invalid. Immediate wait
  // so the replacement can be recovered without inheriting the old probeId.
  this->enterWaitLocked(entry, this->nowLocked());
  this->notifyThreadLocked();
}

void RecoveryService::noteSuccess(const std::string &adapterId)
{
  this->notifyConnected(adapterId);
}

bool RecoveryService::noteSuccess(
    const std::string &adapterId,
    std::uint64_t probeId,
    std::uint64_t runtimeEpoch)
{
  if (adapterId.empty() || probeId == 0)
  {
    return false;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  auto it = this->controls_.find(adapterId);
  if (it == this->controls_.end() || !it->second.control.desired)
  {
    return false;
  }
  ControlEntry &entry = it->second;
  if (entry.control.runtimeEpoch != runtimeEpoch
      || entry.control.activeProbeId != probeId)
  {
    return false;
  }
  entry.control.state = RecoveryState::Connected;
  this->clearScheduleLocked(entry);
  this->resetBackoffLocked(entry);
  entry.control.consecutiveFailures = 0;
  this->notifyThreadLocked();
  return true;
}

void RecoveryService::noteFailure(const std::string &adapterId)
{
  if (adapterId.empty())
  {
    return;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  auto it = this->controls_.find(adapterId);
  if (it == this->controls_.end() || !it->second.control.desired)
  {
    return;
  }
  ControlEntry &entry = it->second;
  ++entry.control.consecutiveFailures;
  entry.control.backoffMs = doubledBackoff(entry.control.backoffMs);
  this->enterWaitLocked(entry, this->nowLocked() + entry.control.backoffMs);
  this->notifyThreadLocked();
}

bool RecoveryService::noteFailure(
    const std::string &adapterId,
    std::uint64_t probeId,
    std::uint64_t runtimeEpoch)
{
  if (adapterId.empty() || probeId == 0)
  {
    return false;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  auto it = this->controls_.find(adapterId);
  if (it == this->controls_.end() || !it->second.control.desired)
  {
    return false;
  }
  ControlEntry &entry = it->second;
  if (entry.control.runtimeEpoch != runtimeEpoch
      || entry.control.activeProbeId != probeId)
  {
    return false;
  }
  ++entry.control.consecutiveFailures;
  entry.control.backoffMs = doubledBackoff(entry.control.backoffMs);
  this->enterWaitLocked(entry, this->nowLocked() + entry.control.backoffMs);
  this->notifyThreadLocked();
  return true;
}

bool RecoveryService::noteTimeout(
    const std::string &adapterId,
    std::uint64_t probeId,
    std::uint64_t runtimeEpoch)
{
  return this->noteFailure(adapterId, probeId, runtimeEpoch);
}

void RecoveryService::notifyConnected(const std::string &adapterId)
{
  if (adapterId.empty())
  {
    return;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  ControlEntry &entry = this->ensureLocked(adapterId);
  if (!entry.control.desired)
  {
    entry.control.state = RecoveryState::Disabled;
    this->clearScheduleLocked(entry);
    return;
  }
  entry.control.state = RecoveryState::Connected;
  this->clearScheduleLocked(entry);
  this->resetBackoffLocked(entry);
  entry.control.consecutiveFailures = 0;
  this->notifyThreadLocked();
}

void RecoveryService::notifyUnavailable(const std::string &adapterId)
{
  if (adapterId.empty())
  {
    return;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  auto it = this->controls_.find(adapterId);
  if (it == this->controls_.end() || !it->second.control.desired)
  {
    return;
  }
  ControlEntry &entry = it->second;
  if (entry.control.state == RecoveryState::RecoveryWait
      || entry.control.state == RecoveryState::Probing)
  {
    return;
  }
  // CONNECTED → unavailable, or desired-but-DISABLED (operator Connect
  // armed intent; runtime not yet connected). Wait current backoff without
  // treating this as a failed logical probe.
  this->enterWaitLocked(entry, this->nowLocked() + entry.control.backoffMs);
  this->notifyThreadLocked();
}

void RecoveryService::deferRetry(const std::string &adapterId)
{
  if (adapterId.empty())
  {
    return;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  auto it = this->controls_.find(adapterId);
  if (it == this->controls_.end() || !it->second.control.desired)
  {
    return;
  }
  ControlEntry &entry = it->second;
  auto backoff = entry.control.backoffMs;
  if (backoff.count() <= 0)
  {
    backoff = kRecoveryInitialBackoff;
    entry.control.backoffMs = backoff;
  }
  this->enterWaitLocked(entry, this->nowLocked() + backoff);
  this->notifyThreadLocked();
}

void RecoveryService::releaseLogicalProbe(const std::string &adapterId)
{
  if (adapterId.empty())
  {
    return;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  auto it = this->controls_.find(adapterId);
  if (it == this->controls_.end() || !it->second.control.desired)
  {
    return;
  }
  ControlEntry &entry = it->second;
  if (entry.control.state != RecoveryState::Probing)
  {
    return;
  }
  this->enterWaitLocked(entry, this->nowLocked());
  this->notifyThreadLocked();
}

std::vector<RecoveryDueEvent> RecoveryService::tick()
{
  std::vector<RecoveryDueEvent> due;
  std::function<void(const RecoveryDueEvent &)> observer;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->stopping_)
    {
      return due;
    }
    due = this->collectDueLocked(this->nowLocked());
    observer = this->dueObserver_;
  }
  if (observer)
  {
    for (const RecoveryDueEvent &event : due)
    {
      observer(event);
    }
  }
  return due;
}

RecoveryControlSnapshot RecoveryService::snapshot(const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const auto it = this->controls_.find(adapterId);
  if (it == this->controls_.end())
  {
    RecoveryControlSnapshot empty;
    empty.adapterId = adapterId;
    return empty;
  }
  return this->snapshotOf(adapterId, it->second.control);
}

std::vector<RecoveryControlSnapshot> RecoveryService::snapshots() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  std::vector<RecoveryControlSnapshot> out;
  out.reserve(this->controls_.size());
  for (const auto &entry : this->controls_)
  {
    out.push_back(this->snapshotOf(entry.first, entry.second.control));
  }
  return out;
}

std::size_t RecoveryService::controlCount() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->controls_.size();
}

std::vector<RecoveryDueEvent> RecoveryService::dueProbes() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  std::vector<RecoveryDueEvent> out;
  for (const auto &entry : this->controls_)
  {
    const RecoveryControl &control = entry.second.control;
    if (control.state != RecoveryState::Probing)
    {
      continue;
    }
    RecoveryDueEvent event;
    event.adapterId = entry.first;
    event.probeId = control.activeProbeId;
    event.runtimeEpoch = control.runtimeEpoch;
    event.recoverySequence = control.recoverySequence;
    out.push_back(event);
  }
  return out;
}

std::uint64_t RecoveryService::dueEventCount() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->dueEventCount_;
}

std::uint64_t RecoveryService::dueEventCount(const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const auto it = this->controls_.find(adapterId);
  if (it == this->controls_.end())
  {
    return 0;
  }
  return it->second.dueEvents;
}

void RecoveryService::setDueObserver(
    std::function<void(const RecoveryDueEvent &)> observer)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->dueObserver_ = std::move(observer);
}

RecoveryControlSnapshot RecoveryService::snapshotOf(
    const std::string &adapterId, const RecoveryControl &control) const
{
  RecoveryControlSnapshot snap;
  snap.adapterId = adapterId;
  snap.desired = control.desired;
  snap.state = control.state;
  snap.nextAttemptAt = control.nextAttemptAt;
  snap.backoffMs = control.backoffMs;
  snap.consecutiveFailures = control.consecutiveFailures;
  snap.recoverySequence = control.recoverySequence;
  snap.runtimeEpoch = control.runtimeEpoch;
  snap.activeProbeId = control.activeProbeId;
  return snap;
}

RecoveryService::ControlEntry &RecoveryService::ensureLocked(
    const std::string &adapterId)
{
  return this->controls_[adapterId];
}

std::vector<RecoveryDueEvent> RecoveryService::collectDueLocked(
    std::chrono::steady_clock::time_point now)
{
  std::vector<RecoveryDueEvent> due;
  for (auto &entry : this->controls_)
  {
    RecoveryControl &control = entry.second.control;
    if (!control.desired)
    {
      continue;
    }
    if (control.state != RecoveryState::RecoveryWait)
    {
      continue;
    }
    if (control.nextAttemptAt.time_since_epoch().count() == 0
        || now < control.nextAttemptAt)
    {
      continue;
    }
    control.state = RecoveryState::Probing;
    control.activeProbeId = this->nextProbeId_++;
    ++control.recoverySequence;
    ++entry.second.dueEvents;
    ++this->dueEventCount_;
    RecoveryDueEvent event;
    event.adapterId = entry.first;
    event.probeId = control.activeProbeId;
    event.runtimeEpoch = control.runtimeEpoch;
    event.recoverySequence = control.recoverySequence;
    due.push_back(event);
  }
  std::sort(
      due.begin(), due.end(),
      [](const RecoveryDueEvent &a, const RecoveryDueEvent &b) {
        if (a.recoverySequence != b.recoverySequence)
        {
          return a.recoverySequence < b.recoverySequence;
        }
        return a.adapterId < b.adapterId;
      });
  return due;
}

void RecoveryService::enterWaitLocked(
    ControlEntry &entry, std::chrono::steady_clock::time_point when)
{
  entry.control.state = RecoveryState::RecoveryWait;
  entry.control.nextAttemptAt = when;
  entry.control.activeProbeId = 0;
}

void RecoveryService::clearScheduleLocked(ControlEntry &entry)
{
  entry.control.nextAttemptAt = {};
  entry.control.activeProbeId = 0;
}

void RecoveryService::resetBackoffLocked(ControlEntry &entry)
{
  entry.control.backoffMs = kRecoveryInitialBackoff;
}

std::chrono::milliseconds RecoveryService::doubledBackoff(
    std::chrono::milliseconds current)
{
  if (current.count() <= 0)
  {
    current = kRecoveryInitialBackoff;
  }
  auto next = current * 2;
  if (next > kRecoveryMaxBackoff)
  {
    next = kRecoveryMaxBackoff;
  }
  if (next < kRecoveryInitialBackoff)
  {
    next = kRecoveryInitialBackoff;
  }
  return next;
}

std::chrono::steady_clock::time_point RecoveryService::earliestDueLocked() const
{
  std::chrono::steady_clock::time_point earliest =
      std::chrono::steady_clock::time_point::max();
  bool any = false;
  for (const auto &entry : this->controls_)
  {
    const RecoveryControl &control = entry.second.control;
    if (!control.desired || control.state != RecoveryState::RecoveryWait)
    {
      continue;
    }
    if (control.nextAttemptAt.time_since_epoch().count() == 0)
    {
      continue;
    }
    if (!any || control.nextAttemptAt < earliest)
    {
      earliest = control.nextAttemptAt;
      any = true;
    }
  }
  return any ? earliest : std::chrono::steady_clock::time_point::max();
}

void RecoveryService::notifyThreadLocked()
{
  this->cv_.notify_all();
}

std::chrono::steady_clock::time_point RecoveryService::nowLocked() const
{
  return this->clock_->now();
}

void RecoveryService::threadMain()
{
  for (;;)
  {
    std::vector<RecoveryDueEvent> due;
    std::function<void(const RecoveryDueEvent &)> observer;
    {
      std::unique_lock<std::mutex> lock(this->mutex_);
      if (this->stopping_ || !this->running_)
      {
        return;
      }
      const auto deadline = this->earliestDueLocked();
      if (deadline == std::chrono::steady_clock::time_point::max())
      {
        this->cv_.wait(lock, [this]() {
          return this->stopping_ || !this->running_
              || this->earliestDueLocked()
              != std::chrono::steady_clock::time_point::max();
        });
      }
      else
      {
        // No predicate: notify (new/sooner timer) must interrupt wait_until.
        // A stopping-only predicate would ignore that notify and wait out
        // the previous deadline.
        this->cv_.wait_until(lock, deadline);
      }
      if (this->stopping_ || !this->running_)
      {
        return;
      }
      due = this->collectDueLocked(this->nowLocked());
      observer = this->dueObserver_;
    }
    if (observer)
    {
      for (const RecoveryDueEvent &event : due)
      {
        observer(event);
      }
    }
  }
}

}  // namespace icp
}  // namespace virtual_factory
