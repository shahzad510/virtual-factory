#include <virtual_factory/icp/ProbeExecutor.hh>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace virtual_factory
{
namespace icp
{

namespace
{

void collectEquipmentIds(
    IndustrialAdapter &adapter, std::vector<std::string> *out)
{
  if (out == nullptr)
  {
    return;
  }
  for (Equipment *equipment : adapter.equipment())
  {
    if (equipment != nullptr && !equipment->id().empty())
    {
      out->push_back(equipment->id());
    }
  }
}

std::uint8_t terminalCode(ProbeOutcome outcome)
{
  return static_cast<std::uint8_t>(outcome) + 1;
}

bool sessionBetter(
    const std::shared_ptr<ProbeSession> &a,
    const std::shared_ptr<ProbeSession> &b)
{
  if (!a)
  {
    return false;
  }
  if (!b)
  {
    return true;
  }
  if (a->recoverySequence() != b->recoverySequence())
  {
    return a->recoverySequence() < b->recoverySequence();
  }
  return a->adapterId() < b->adapterId();
}

}  // namespace

struct ProbeExecutor::State
{
  std::mutex mutex;
  std::condition_variable cv;
  bool running{false};
  bool stopping{false};
  std::shared_ptr<ProbeCompletionSink> sink;
  /// Current logical occupancy: at most one session per adapterId.
  std::unordered_map<std::string, std::shared_ptr<ProbeSession>> logical;
  /// Waiting for a worker. Size ≤ logical.size() ≤ adapter count.
  std::vector<std::shared_ptr<ProbeSession>> pending;
  /// Physical in-flight (may include cancelled superseded sessions).
  std::vector<std::shared_ptr<ProbeSession>> physical;
  std::size_t inFlightCount{0};
  std::size_t quarantinedCount{0};
  std::unordered_map<std::string, std::size_t> quarantinedPerAdapter;
  std::size_t qReservedCount{0};
  std::unordered_map<std::string, std::size_t> qReservedPerAdapter;
  std::size_t nonQCount{0};
  std::unordered_map<std::string, std::size_t> nonQPerAdapter;
  std::size_t freshCount{0};
  std::size_t maxNonQ{0};
  std::size_t maxFresh{0};
  std::size_t peakOccupancy{0};
  std::size_t liveBusy{0};
};

bool ProbeSession::tryClaim(ProbeOutcome outcome)
{
  std::uint8_t expected = 0;
  const std::uint8_t desired = terminalCode(outcome);
  return this->terminal_.compare_exchange_strong(
      expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
}

ProbeOutcome ProbeSession::terminal() const
{
  const std::uint8_t code = this->terminal_.load(std::memory_order_acquire);
  if (code == 0)
  {
    return ProbeOutcome::Stale;
  }
  return static_cast<ProbeOutcome>(code - 1);
}

ProbeSession::ProbeSession(std::string adapterId,
                           std::uint64_t probeId,
                           std::uint64_t runtimeEpoch,
                           std::uint64_t recoverySequence,
                           std::shared_ptr<IndustrialAdapter> adapter,
                           std::shared_ptr<std::mutex> ioMutex,
                           std::shared_ptr<std::atomic<bool>> enrolled,
                           std::shared_ptr<ProbeCompletionSink> sink,
                           std::chrono::milliseconds timeout)
    : adapterId_(std::move(adapterId))
    , probeId_(probeId)
    , runtimeEpoch_(runtimeEpoch)
    , recoverySequence_(recoverySequence)
    , adapter_(std::move(adapter))
    , ioMutex_(std::move(ioMutex))
    , enrolled_(std::move(enrolled))
    , sink_(std::move(sink))
    , timeout_(timeout.count() > 0 ? timeout : std::chrono::milliseconds{2000})
{
}

ProbeExecutor::ProbeExecutor()
    : state_(std::make_shared<State>())
{
}

ProbeExecutor::~ProbeExecutor()
{
  this->stop();
}

void ProbeExecutor::setSink(std::shared_ptr<ProbeCompletionSink> sink)
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  this->state_->sink = std::move(sink);
}

std::size_t ProbeExecutor::clampWorkerCount(std::size_t workerCount)
{
  if (workerCount < kMinProbeWorkerCount)
  {
    return kMinProbeWorkerCount;
  }
  if (workerCount > kMaxProbeWorkerCount)
  {
    return kMaxProbeWorkerCount;
  }
  return workerCount;
}

ProbeExecutor::WorkerHandle ProbeExecutor::createWorkerHandle()
{
  auto control = std::make_shared<WorkerControl>();
  std::shared_ptr<State> state = this->state_;
  WorkerHandle handle;
  handle.control = control;
  handle.thread = std::thread([state, control]() { workerMain(state, control); });
  return handle;
}

void ProbeExecutor::discardUninstalledWorker(WorkerHandle &handle)
{
  if (!handle.thread.joinable())
  {
    return;
  }
  if (handle.control)
  {
    handle.control->decrementOnExit.store(false, std::memory_order_relaxed);
    handle.control->stayLive.store(false, std::memory_order_release);
  }
  handle.thread.detach();
  if (this->state_)
  {
    this->state_->cv.notify_all();
  }
}

void ProbeExecutor::spawnWorkerLocked()
{
  this->workers_.push_back(this->createWorkerHandle());
}

void ProbeExecutor::start(std::size_t workerCount)
{
  workerCount = clampWorkerCount(workerCount);
  {
    std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
    std::lock_guard<std::mutex> lock(this->state_->mutex);
    if (this->state_->running)
    {
      return;
    }
    this->state_->stopping = false;
    this->state_->running = true;
    this->state_->maxNonQ = workerCount > kReservedProbeWorkerCount
                                ? workerCount - kReservedProbeWorkerCount
                                : 0;
    this->state_->maxFresh = workerCount < kReservedProbeWorkerCount
                                 ? workerCount
                                 : kReservedProbeWorkerCount;
    this->workers_.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i)
    {
      this->spawnWorkerLocked();
    }
  }
  this->watchdog_ = std::thread([this]() { this->watchdogLoop(); });
}

bool ProbeExecutor::submit(ProbeRequest request)
{
  if (request.adapterId.empty() || request.probeId == 0
      || request.adapter == nullptr || request.ioMutex == nullptr)
  {
    return false;
  }
  std::shared_ptr<ProbeSession> superseded;
  {
    std::lock_guard<std::mutex> lock(this->state_->mutex);
    if (this->state_->stopping || !this->state_->running)
    {
      return false;
    }
    auto existing = this->state_->logical.find(request.adapterId);
    if (existing != this->state_->logical.end() && existing->second)
    {
      if (existing->second->probeId() == request.probeId
          && existing->second->runtimeEpoch() == request.runtimeEpoch)
      {
        return true;
      }
      superseded = existing->second;
      superseded->cancel();
      this->state_->pending.erase(
          std::remove(this->state_->pending.begin(), this->state_->pending.end(),
                      superseded),
          this->state_->pending.end());
    }
    auto session = std::make_shared<ProbeSession>(
        request.adapterId,
        request.probeId,
        request.runtimeEpoch,
        request.recoverySequence,
        std::move(request.adapter),
        std::move(request.ioMutex),
        std::move(request.enrolled),
        this->state_->sink,
        request.timeout);
    this->state_->logical[request.adapterId] = session;
    this->state_->pending.push_back(session);
    recordOccupancyLocked(*this->state_);
  }
  this->state_->cv.notify_all();
  (void)superseded;
  return true;
}

void ProbeExecutor::cancel(const std::string &adapterId)
{
  if (adapterId.empty())
  {
    return;
  }
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  auto it = this->state_->logical.find(adapterId);
  if (it != this->state_->logical.end() && it->second)
  {
    it->second->cancel();
    this->state_->pending.erase(
        std::remove(this->state_->pending.begin(), this->state_->pending.end(),
                    it->second),
        this->state_->pending.end());
    this->state_->logical.erase(it);
  }
  for (const auto &session : this->state_->physical)
  {
    if (session && session->adapterId() == adapterId)
    {
      session->cancel();
    }
  }
}

void ProbeExecutor::cancel(const std::string &adapterId, std::uint64_t probeId)
{
  if (adapterId.empty())
  {
    return;
  }
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  auto it = this->state_->logical.find(adapterId);
  if (it != this->state_->logical.end() && it->second
      && it->second->probeId() == probeId)
  {
    it->second->cancel();
    this->state_->pending.erase(
        std::remove(this->state_->pending.begin(), this->state_->pending.end(),
                    it->second),
        this->state_->pending.end());
    this->state_->logical.erase(it);
  }
  for (const auto &session : this->state_->physical)
  {
    if (session && session->adapterId() == adapterId
        && session->probeId() == probeId)
    {
      session->cancel();
    }
  }
}

void ProbeExecutor::cancelAll()
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  for (auto &entry : this->state_->logical)
  {
    if (entry.second)
    {
      entry.second->cancel();
    }
  }
  for (const auto &session : this->state_->physical)
  {
    if (session)
    {
      session->cancel();
    }
  }
  this->state_->pending.clear();
  this->state_->logical.clear();
}

void ProbeExecutor::stop(
    std::chrono::milliseconds grace, std::shared_ptr<void> keepAlive)
{
  std::shared_ptr<State> state;
  {
    std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
    state = this->state_;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (!state->running && this->workers_.empty() && this->quarantined_.empty()
          && !this->watchdog_.joinable())
      {
        return;
      }
      state->stopping = true;
      state->running = false;
      for (auto &entry : state->logical)
      {
        if (entry.second)
        {
          entry.second->cancel();
        }
      }
      for (const auto &session : state->physical)
      {
        if (session)
        {
          session->cancel();
        }
      }
      state->pending.clear();
      state->logical.clear();
    }
  }
  state->cv.notify_all();

  if (this->watchdog_.joinable())
  {
    this->watchdog_.join();
  }

  if (grace.count() < 0)
  {
    grace = std::chrono::milliseconds{0};
  }

  {
    std::unique_lock<std::mutex> lock(state->mutex);
    state->cv.wait_for(lock, grace, [&]() {
      return state->inFlightCount == 0 && state->pending.empty();
    });
  }

  bool busy = false;
  {
    std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
    std::lock_guard<std::mutex> lock(state->mutex);
    busy = state->inFlightCount != 0;

    if (busy)
    {
      auto cohort = std::make_shared<AbandonedCohort>();
      cohort->workers = std::move(this->workers_);
      for (WorkerHandle &handle : this->quarantined_)
      {
        cohort->workers.push_back(std::move(handle));
      }
      this->quarantined_.clear();
      cohort->keepAlive = std::move(keepAlive);
      cohort->state = state;
      this->workers_.clear();
      this->abandoned_ = cohort;
      for (WorkerHandle &worker : cohort->workers)
      {
        if (worker.control)
        {
          worker.control->stayLive.store(false, std::memory_order_release);
        }
        if (worker.thread.joinable())
        {
          worker.thread.detach();
        }
      }
      this->state_ = std::make_shared<State>();
      return;
    }
  }

  std::vector<WorkerHandle> joining;
  {
    std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
    joining = std::move(this->workers_);
    this->workers_.clear();
    for (WorkerHandle &handle : this->quarantined_)
    {
      joining.push_back(std::move(handle));
    }
    this->quarantined_.clear();
  }
  for (WorkerHandle &worker : joining)
  {
    if (worker.control)
    {
      worker.control->stayLive.store(false, std::memory_order_release);
    }
    if (worker.thread.joinable())
    {
      worker.thread.join();
    }
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->stopping = false;
    state->inFlightCount = 0;
    state->liveBusy = 0;
    state->pending.clear();
    state->physical.clear();
    state->logical.clear();
  }
}

bool ProbeExecutor::running() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->running;
}

bool ProbeExecutor::hasAbandonedWorkers() const
{
  std::lock_guard<std::mutex> lock(this->workers_mutex_);
  return this->abandoned_ != nullptr;
}

bool ProbeExecutor::hasLogicalProbe(const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->logical.find(adapterId);
  return it != this->state_->logical.end() && it->second
      && !it->second->cancelled();
}

bool ProbeExecutor::hasLogicalProbe(
    const std::string &adapterId, std::uint64_t probeId) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->logical.find(adapterId);
  return it != this->state_->logical.end() && it->second
      && it->second->probeId() == probeId && !it->second->cancelled();
}

bool ProbeExecutor::hasPhysicalOn(
    const std::string &adapterId, const IndustrialAdapter *expected) const
{
  if (expected == nullptr)
  {
    return false;
  }
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  for (const auto &session : this->state_->physical)
  {
    if (session && session->adapterId() == adapterId
        && session->adapter().get() == expected && session->enteredConnect()
        && !session->finished())
    {
      return true;
    }
  }
  return false;
}

bool ProbeExecutor::hasPhysicalFor(const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  for (const auto &session : this->state_->physical)
  {
    if (session && session->adapterId() == adapterId && !session->finished())
    {
      return true;
    }
  }
  return false;
}

bool ProbeExecutor::hasLivePhysical(const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return hasLivePhysicalLocked(*this->state_, adapterId);
}

std::uint64_t ProbeExecutor::logicalProbeId(const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->logical.find(adapterId);
  if (it == this->state_->logical.end() || !it->second)
  {
    return 0;
  }
  return it->second->probeId();
}

std::size_t ProbeExecutor::logicalProbeCount() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->logical.size();
}

std::size_t ProbeExecutor::pendingCount() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->pending.size();
}

std::size_t ProbeExecutor::inFlightCount() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->inFlightCount;
}

std::size_t ProbeExecutor::liveWorkerCount() const
{
  std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
  return this->workers_.size();
}

std::size_t ProbeExecutor::idleLiveWorkerCount() const
{
  std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const std::size_t live = this->workers_.size();
  const std::size_t busy = this->state_->liveBusy;
  return live > busy ? live - busy : 0;
}

std::size_t ProbeExecutor::quarantinedWorkerCount() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->quarantinedCount;
}

std::size_t ProbeExecutor::quarantinedWorkerCount(
    const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->quarantinedPerAdapter.find(adapterId);
  if (it == this->state_->quarantinedPerAdapter.end())
  {
    return 0;
  }
  return it->second;
}

std::size_t ProbeExecutor::quarantineReservationCount() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->qReservedCount;
}

std::size_t ProbeExecutor::nonQuarantinableLiveCount() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->nonQCount;
}

std::size_t ProbeExecutor::peakOccupancy() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->peakOccupancy;
}

void ProbeExecutor::recordOccupancyLocked(State &state)
{
  const std::size_t occupancy =
      state.logical.size() + state.pending.size() + state.physical.size();
  if (occupancy > state.peakOccupancy)
  {
    state.peakOccupancy = occupancy;
  }
}

bool ProbeExecutor::hasLivePhysicalLocked(
    const State &state, const std::string &adapterId)
{
  for (const auto &session : state.physical)
  {
    if (!session || session->adapterId() != adapterId || session->finished())
    {
      continue;
    }
    const ProbeHangClass hang = session->hangClass();
    if (hang == ProbeHangClass::ReservedQ
        || hang == ProbeHangClass::ReservedNonQ
        || hang == ProbeHangClass::ReservedFresh)
    {
      return true;
    }
  }
  return false;
}

bool ProbeExecutor::canReserveQLocked(
    const State &state, const std::string &adapterId)
{
  if (state.quarantinedCount + state.qReservedCount
      >= kMaxQuarantinedProbeWorkers)
  {
    return false;
  }
  std::size_t per = 0;
  const auto qIt = state.quarantinedPerAdapter.find(adapterId);
  if (qIt != state.quarantinedPerAdapter.end())
  {
    per += qIt->second;
  }
  const auto rIt = state.qReservedPerAdapter.find(adapterId);
  if (rIt != state.qReservedPerAdapter.end())
  {
    per += rIt->second;
  }
  return per < kMaxQuarantinedProbesPerAdapter;
}

bool ProbeExecutor::canReserveNonQLocked(
    const State &state, const std::string &adapterId)
{
  if (state.maxNonQ == 0 || state.nonQCount >= state.maxNonQ)
  {
    return false;
  }
  const auto it = state.nonQPerAdapter.find(adapterId);
  return it == state.nonQPerAdapter.end() || it->second == 0;
}

bool ProbeExecutor::canReserveFreshLocked(
    const State &state, const std::string &adapterId)
{
  if (state.maxFresh == 0 || state.freshCount >= state.maxFresh)
  {
    return false;
  }
  if (hasLivePhysicalLocked(state, adapterId))
  {
    return false;
  }
  const auto qIt = state.quarantinedPerAdapter.find(adapterId);
  if (qIt != state.quarantinedPerAdapter.end() && qIt->second > 0)
  {
    return false;
  }
  const auto rIt = state.qReservedPerAdapter.find(adapterId);
  if (rIt != state.qReservedPerAdapter.end() && rIt->second > 0)
  {
    return false;
  }
  const auto nIt = state.nonQPerAdapter.find(adapterId);
  if (nIt != state.nonQPerAdapter.end() && nIt->second > 0)
  {
    return false;
  }
  return true;
}

bool ProbeExecutor::canAdmitLocked(
    const State &state, const std::string &adapterId)
{
  if (hasLivePhysicalLocked(state, adapterId))
  {
    return false;
  }
  return canReserveQLocked(state, adapterId)
      || canReserveNonQLocked(state, adapterId)
      || canReserveFreshLocked(state, adapterId);
}

bool ProbeExecutor::hasEligiblePendingLocked(const State &state)
{
  for (const auto &session : state.pending)
  {
    if (!session || session->cancelled() || session->finished())
    {
      continue;
    }
    if (canAdmitLocked(state, session->adapterId()))
    {
      return true;
    }
  }
  return false;
}

bool ProbeExecutor::admitLocked(
    State &state, const std::shared_ptr<ProbeSession> &session)
{
  if (!session)
  {
    return false;
  }
  const std::string &id = session->adapterId();
  if (hasLivePhysicalLocked(state, id))
  {
    return false;
  }
  if (canReserveQLocked(state, id))
  {
    ++state.qReservedCount;
    ++state.qReservedPerAdapter[id];
    session->setHangClass(ProbeHangClass::ReservedQ);
    return true;
  }
  if (canReserveNonQLocked(state, id))
  {
    ++state.nonQCount;
    ++state.nonQPerAdapter[id];
    session->setHangClass(ProbeHangClass::ReservedNonQ);
    return true;
  }
  if (canReserveFreshLocked(state, id))
  {
    ++state.freshCount;
    session->setHangClass(ProbeHangClass::ReservedFresh);
    return true;
  }
  return false;
}

void ProbeExecutor::releaseHangBudgetLocked(
    State &state, const std::shared_ptr<ProbeSession> &session)
{
  if (!session)
  {
    return;
  }
  const ProbeHangClass hang = session->hangClass();
  const std::string &id = session->adapterId();
  if (hang == ProbeHangClass::ReservedQ)
  {
    if (state.qReservedCount > 0)
    {
      --state.qReservedCount;
    }
    auto it = state.qReservedPerAdapter.find(id);
    if (it != state.qReservedPerAdapter.end())
    {
      if (it->second > 0)
      {
        --it->second;
      }
      if (it->second == 0)
      {
        state.qReservedPerAdapter.erase(it);
      }
    }
    session->setHangClass(ProbeHangClass::Unreserved);
    state.cv.notify_all();
    return;
  }
  if (hang == ProbeHangClass::ReservedNonQ)
  {
    if (state.nonQCount > 0)
    {
      --state.nonQCount;
    }
    auto it = state.nonQPerAdapter.find(id);
    if (it != state.nonQPerAdapter.end())
    {
      if (it->second > 0)
      {
        --it->second;
      }
      if (it->second == 0)
      {
        state.nonQPerAdapter.erase(it);
      }
    }
    session->setHangClass(ProbeHangClass::Unreserved);
    state.cv.notify_all();
    return;
  }
  if (hang == ProbeHangClass::ReservedFresh)
  {
    if (state.freshCount > 0)
    {
      --state.freshCount;
    }
    session->setHangClass(ProbeHangClass::Unreserved);
    state.cv.notify_all();
  }
}

bool ProbeExecutor::takeNextLocked(
    State &state, std::shared_ptr<ProbeSession> *out)
{
  if (out == nullptr || state.pending.empty())
  {
    return false;
  }
  std::size_t best = state.pending.size();
  for (std::size_t i = 0; i < state.pending.size(); ++i)
  {
    const auto &session = state.pending[i];
    if (!session || session->cancelled() || session->finished())
    {
      continue;
    }
    if (!canAdmitLocked(state, session->adapterId()))
    {
      continue;
    }
    if (best >= state.pending.size()
        || sessionBetter(session, state.pending[best]))
    {
      best = i;
    }
  }
  if (best >= state.pending.size())
  {
    return false;
  }
  *out = std::move(state.pending[best]);
  state.pending.erase(state.pending.begin() + static_cast<std::ptrdiff_t>(best));
  if (!admitLocked(state, *out))
  {
    if (*out)
    {
      state.pending.push_back(*out);
    }
    *out = nullptr;
    return false;
  }
  state.physical.push_back(*out);
  ++state.inFlightCount;
  recordOccupancyLocked(state);
  return true;
}

void ProbeExecutor::completeSessionLocked(
    State &state, const std::shared_ptr<ProbeSession> &session)
{
  if (!session)
  {
    return;
  }
  session->markFinished();
  state.physical.erase(
      std::remove(state.physical.begin(), state.physical.end(), session),
      state.physical.end());
  if (state.inFlightCount > 0)
  {
    --state.inFlightCount;
  }
  auto it = state.logical.find(session->adapterId());
  if (it != state.logical.end() && it->second
      && it->second->probeId() == session->probeId())
  {
    state.logical.erase(it);
  }
  state.cv.notify_all();
}

void ProbeExecutor::accountQuarantineExit(
    State &state, const std::string &adapterId)
{
  if (state.quarantinedCount > 0)
  {
    --state.quarantinedCount;
  }
  auto it = state.quarantinedPerAdapter.find(adapterId);
  if (it != state.quarantinedPerAdapter.end())
  {
    if (it->second > 0)
    {
      --it->second;
    }
    if (it->second == 0)
    {
      state.quarantinedPerAdapter.erase(it);
    }
  }
}

void ProbeExecutor::publish(
    const std::shared_ptr<ProbeSession> &session, ProbeCompletion completion)
{
  if (!session)
  {
    return;
  }
  std::shared_ptr<ProbeCompletionSink> sink = session->sink();
  if (sink)
  {
    sink->onProbeCompleted(completion);
  }
}

void ProbeExecutor::runProbe(
    State &state, const std::shared_ptr<ProbeSession> &session)
{
  if (!session || session->adapter() == nullptr || session->ioMutex() == nullptr)
  {
    ProbeCompletion done;
    if (session)
    {
      done.adapterId = session->adapterId();
      done.probeId = session->probeId();
      done.runtimeEpoch = session->runtimeEpoch();
      done.recoverySequence = session->recoverySequence();
      done.adapter = session->adapter();
      (void)session->tryClaim(ProbeOutcome::Stale);
    }
    done.outcome = ProbeOutcome::Stale;
    done.message = "invalid probe session";
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      releaseHangBudgetLocked(state, session);
      completeSessionLocked(state, session);
    }
    publish(session, std::move(done));
    return;
  }

  session->markStarted(std::chrono::steady_clock::now());

  ProbeCompletion done;
  done.adapterId = session->adapterId();
  done.probeId = session->probeId();
  done.runtimeEpoch = session->runtimeEpoch();
  done.recoverySequence = session->recoverySequence();
  done.adapter = session->adapter();

  auto finishWithoutConnect = [&](ProbeOutcome outcome, const char *message) {
    const bool won = session->tryClaim(outcome);
    done.outcome = won ? outcome : ProbeOutcome::Stale;
    done.message = message;
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      releaseHangBudgetLocked(state, session);
      completeSessionLocked(state, session);
    }
    if (won)
    {
      publish(session, std::move(done));
    }
  };

  if (session->cancelled() || session->hasTerminal())
  {
    finishWithoutConnect(ProbeOutcome::Cancelled, "probe cancelled before connect");
    return;
  }

  std::unique_lock<std::mutex> io(*session->ioMutex(), std::try_to_lock);
  if (!io.owns_lock())
  {
    finishWithoutConnect(ProbeOutcome::IoBusy, "adapter I/O busy");
    return;
  }

  if (session->cancelled() || session->hasTerminal())
  {
    io.unlock();
    finishWithoutConnect(ProbeOutcome::Cancelled, "probe cancelled before connect");
    return;
  }

  session->markEnteredConnect();
  const bool ok = session->adapter()->connect();
  if (ok)
  {
    collectEquipmentIds(*session->adapter(), &done.equipmentIds);
  }
  else
  {
    done.message = session->adapter()->lastError().empty()
                       ? "connect failed"
                       : session->adapter()->lastError();
  }
  io.unlock();

  const ProbeOutcome claimed =
      ok ? ProbeOutcome::Success : ProbeOutcome::Failure;
  const bool won = session->tryClaim(claimed);
  if (won)
  {
    done.outcome = claimed;
    done.ok = ok;
    if (ok)
    {
      done.message = "connected";
    }
  }
  else
  {
    done.outcome = ProbeOutcome::Stale;
    done.ok = false;
    done.message = "stale probe completion ignored";
  }

  {
    std::lock_guard<std::mutex> lock(state.mutex);
    releaseHangBudgetLocked(state, session);
    completeSessionLocked(state, session);
  }
  if (won)
  {
    publish(session, std::move(done));
  }
}

void ProbeExecutor::workerMain(
    const std::shared_ptr<State> &state,
    const std::shared_ptr<WorkerControl> &control)
{
  if (!state || !control)
  {
    return;
  }
  for (;;)
  {
    std::shared_ptr<ProbeSession> session;
    {
      std::unique_lock<std::mutex> lock(state->mutex);
      state->cv.wait(lock, [&]() {
        return state->stopping
            || !control->stayLive.load(std::memory_order_acquire)
            || hasEligiblePendingLocked(*state);
      });
      if (!control->stayLive.load(std::memory_order_acquire))
      {
        break;
      }
      if (state->stopping && !hasEligiblePendingLocked(*state))
      {
        break;
      }
      if (!takeNextLocked(*state, &session))
      {
        if (state->stopping)
        {
          break;
        }
        continue;
      }
      if (session)
      {
        session->setWorkerId(std::this_thread::get_id());
      }
      ++state->liveBusy;
    }
    runProbe(*state, session);
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->liveBusy > 0)
      {
        --state->liveBusy;
      }
      state->cv.notify_all();
    }
  }
  if (control->decrementOnExit.load(std::memory_order_acquire))
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    accountQuarantineExit(*state, control->quarantinedAdapterId);
    state->cv.notify_all();
  }
  control->exited.store(true, std::memory_order_release);
}

void ProbeExecutor::reapFinishedQuarantine()
{
  std::vector<WorkerHandle> joining;
  {
    std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
    for (auto it = this->quarantined_.begin(); it != this->quarantined_.end();)
    {
      if (!it->thread.joinable())
      {
        it = this->quarantined_.erase(it);
        continue;
      }
      if (it->control && it->control->exited.load(std::memory_order_acquire))
      {
        joining.push_back(std::move(*it));
        it = this->quarantined_.erase(it);
        continue;
      }
      ++it;
    }
  }
  for (WorkerHandle &handle : joining)
  {
    if (handle.thread.joinable())
    {
      handle.thread.join();
    }
  }
}

bool ProbeExecutor::tryQuarantine(const std::shared_ptr<ProbeSession> &session)
{
  if (!session)
  {
    return false;
  }
  if (session->hangClass() != ProbeHangClass::ReservedQ)
  {
    return false;
  }
  const std::thread::id tid = session->workerId();
  if (tid == std::thread::id{})
  {
    return false;
  }

  std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  if (!this->state_->running || this->state_->stopping)
  {
    return false;
  }
  if (session->hangClass() != ProbeHangClass::ReservedQ)
  {
    return false;
  }

  auto found = this->workers_.end();
  for (auto it = this->workers_.begin(); it != this->workers_.end(); ++it)
  {
    if (it->thread.joinable() && it->thread.get_id() == tid)
    {
      found = it;
      break;
    }
  }
  if (found == this->workers_.end())
  {
    return false;
  }

  WorkerHandle replacement;
  try
  {
    replacement = this->createWorkerHandle();
  }
  catch (...)
  {
    return false;
  }
  if (!replacement.thread.joinable() || replacement.control == nullptr)
  {
    this->discardUninstalledWorker(replacement);
    return false;
  }

  try
  {
    found->control->quarantinedAdapterId = session->adapterId();
  }
  catch (...)
  {
    this->discardUninstalledWorker(replacement);
    return false;
  }
  found->control->decrementOnExit.store(true, std::memory_order_release);

  WorkerHandle old;
  old.control = std::move(found->control);
  old.thread = std::move(found->thread);
  found->control = std::move(replacement.control);
  found->thread = std::move(replacement.thread);

  if (old.control)
  {
    old.control->stayLive.store(false, std::memory_order_release);
  }
  this->quarantined_.push_back(std::move(old));

  // Convert the Q reservation into an actual quarantine slot. Capacity
  // (reserved + quarantined) is unchanged.
  if (this->state_->qReservedCount > 0)
  {
    --this->state_->qReservedCount;
  }
  auto reservedIt =
      this->state_->qReservedPerAdapter.find(session->adapterId());
  if (reservedIt != this->state_->qReservedPerAdapter.end())
  {
    if (reservedIt->second > 0)
    {
      --reservedIt->second;
    }
    if (reservedIt->second == 0)
    {
      this->state_->qReservedPerAdapter.erase(reservedIt);
    }
  }
  ++this->state_->quarantinedCount;
  ++this->state_->quarantinedPerAdapter[session->adapterId()];
  session->setHangClass(ProbeHangClass::Quarantined);
  this->state_->cv.notify_all();
  return true;
}

void ProbeExecutor::watchdogLoop()
{
  for (;;)
  {
    std::vector<std::shared_ptr<ProbeSession>> timedOut;
    {
      std::unique_lock<std::mutex> lock(this->state_->mutex);
      if (this->state_->stopping)
      {
        return;
      }
      this->state_->cv.wait_for(lock, std::chrono::milliseconds(25), [this]() {
        return this->state_->stopping;
      });
      if (this->state_->stopping)
      {
        return;
      }
      const auto now = std::chrono::steady_clock::now();
      for (const auto &session : this->state_->physical)
      {
        if (!session || session->finished() || session->hasTerminal())
        {
          continue;
        }
        if (session->startedAt().time_since_epoch().count() == 0)
        {
          continue;
        }
        if (now - session->startedAt() < session->timeout())
        {
          continue;
        }
        if (!session->tryClaim(ProbeOutcome::Timeout))
        {
          continue;
        }
        session->cancel();
        timedOut.push_back(session);
        auto it = this->state_->logical.find(session->adapterId());
        if (it != this->state_->logical.end() && it->second
            && it->second->probeId() == session->probeId())
        {
          this->state_->logical.erase(it);
        }
      }
    }
    for (const auto &session : timedOut)
    {
      ProbeCompletion done;
      done.adapterId = session->adapterId();
      done.probeId = session->probeId();
      done.runtimeEpoch = session->runtimeEpoch();
      done.recoverySequence = session->recoverySequence();
      done.adapter = session->adapter();
      done.outcome = ProbeOutcome::Timeout;
      done.message = "logical probe timeout";
      publish(session, done);
      if (session->hangClass() == ProbeHangClass::ReservedQ)
      {
        const bool quarantined = this->tryQuarantine(session);
        if (!quarantined
            && session->hangClass() == ProbeHangClass::ReservedQ)
        {
          // Invariant: a paid Q reservation must convert or remain held.
          // Spawn failure leaves the worker live with the reservation so we
          // do not admit another unpaid Q hang on this adapter.
        }
      }
    }
    this->reapFinishedQuarantine();
  }
}

}  // namespace icp
}  // namespace virtual_factory
