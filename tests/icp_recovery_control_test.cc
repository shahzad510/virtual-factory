/// Recovery V2 Phase 1: RecoveryService / RecoveryControl state + timer.
///
/// No protocol I/O. No OPC UA. Fake clock. Proves one control per adapter,
/// nextAttemptAt (no retry queue), bounded memory over a 24h outage, and
/// disable/remove/rematerialize/shutdown.

#include <virtual_factory/icp/RecoveryService.hh>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

int failures = 0;

void expect(bool condition, const std::string &message)
{
  if (!condition)
  {
    std::cerr << "FAIL: " << message << std::endl;
    ++failures;
  }
}

using virtual_factory::icp::FakeRecoveryClock;
using virtual_factory::icp::RecoveryDueEvent;
using virtual_factory::icp::RecoveryService;
using virtual_factory::icp::RecoveryState;
using virtual_factory::icp::kRecoveryInitialBackoff;
using virtual_factory::icp::kRecoveryMaxBackoff;
using virtual_factory::icp::recoveryStateName;

std::shared_ptr<FakeRecoveryClock> makeClock()
{
  return std::make_shared<FakeRecoveryClock>(
      std::chrono::steady_clock::time_point(std::chrono::seconds(1000)));
}

}  // namespace

int main()
{
  using ms = std::chrono::milliseconds;

  // -------------------------------------------------------------------
  // A. Initial desired adapter
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    recovery.armDesired("a1", true);
    const auto snap = recovery.snapshot("a1");
    expect(snap.desired, "A desired=true");
    expect(snap.state == RecoveryState::RecoveryWait, "A state=RECOVERY_WAIT");
    expect(snap.nextAttemptAt.time_since_epoch().count() != 0,
           "A nextAttemptAt is set");
    expect(snap.nextAttemptAt == clock->now(), "A immediate arm is due at now");
    expect(recovery.controlCount() == 1, "A one RecoveryControl");
    expect(recovery.dueEventCount() == 0, "A no due event before tick");
  }

  // -------------------------------------------------------------------
  // B. Timer not due
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    recovery.armDesired("b1", false);
    const auto before = recovery.snapshot("b1").nextAttemptAt;
    expect(before == clock->now() + kRecoveryInitialBackoff,
           "B nextAttemptAt is now+1s");
    const auto due = recovery.tick();
    expect(due.empty(), "B no recovery due event");
    expect(recovery.snapshot("b1").state == RecoveryState::RecoveryWait,
           "B remains RECOVERY_WAIT");
  }

  // -------------------------------------------------------------------
  // C. Timer due → exactly one recovery-due event
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    recovery.armDesired("c1", true);
    const auto due = recovery.tick();
    expect(due.size() == 1, "C exactly one recovery-due event");
    expect(due[0].adapterId == "c1", "C due adapter id");
    expect(due[0].probeId != 0, "C logical probe id assigned");
    expect(recovery.snapshot("c1").state == RecoveryState::Probing,
           "C state=PROBING");
    expect(recovery.dueEventCount("c1") == 1, "C per-adapter due count 1");
  }

  // -------------------------------------------------------------------
  // D. 100 ticks while due → exactly ONE logical attempt
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    recovery.armDesired("d1", true);
    (void)recovery.tick();
    for (int i = 0; i < 100; ++i)
    {
      const auto due = recovery.tick();
      expect(due.empty(), "D repeated tick emits no extra due event");
    }
    expect(recovery.dueEventCount("d1") == 1, "D exactly one logical attempt");
    expect(recovery.dueProbes().size() == 1, "D still one probing adapter");
    expect(recovery.controlCount() == 1, "D still one control");
  }

  // -------------------------------------------------------------------
  // E. Failure increments consecutiveFailures and advances nextAttemptAt
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    recovery.armDesired("e1", true);
    (void)recovery.tick();
    const auto before = clock->now();
    recovery.noteFailure("e1");
    const auto snap = recovery.snapshot("e1");
    expect(snap.consecutiveFailures == 1, "E consecutiveFailures=1");
    expect(snap.state == RecoveryState::RecoveryWait, "E back to RECOVERY_WAIT");
    expect(snap.backoffMs == ms(2000), "E backoff doubled to 2s");
    expect(snap.nextAttemptAt == before + ms(2000),
           "E nextAttemptAt = now + backoff");
    expect(snap.activeProbeId == 0, "E logical probe cleared");
  }

  // -------------------------------------------------------------------
  // F. Repeated failure: 1s → 2s → 4s → 8s → 10s cap
  //     (backoff field starts at 1s; each failure doubles then waits)
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    recovery.armDesired("f1", false);
    expect(recovery.snapshot("f1").backoffMs == kRecoveryInitialBackoff,
           "F initial backoff 1s");
    const std::vector<int> expectedMs = {2000, 4000, 8000, 10000, 10000};
    for (std::size_t i = 0; i < expectedMs.size(); ++i)
    {
      clock->advance(std::chrono::duration_cast<ms>(
          recovery.snapshot("f1").nextAttemptAt - clock->now()));
      (void)recovery.tick();
      const auto now = clock->now();
      recovery.noteFailure("f1");
      const auto snap = recovery.snapshot("f1");
      expect(snap.backoffMs == ms(expectedMs[i]),
             "F backoff step " + std::to_string(i));
      expect(snap.nextAttemptAt == now + ms(expectedMs[i]),
             "F wait uses doubled backoff at step " + std::to_string(i));
      expect(snap.consecutiveFailures == i + 1, "F consecutiveFailures");
    }
  }

  // -------------------------------------------------------------------
  // G. Success → CONNECTED, consecutiveFailures=0, backoff reset
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    recovery.armDesired("g1", true);
    (void)recovery.tick();
    recovery.noteFailure("g1");
    recovery.noteFailure("g1");
    expect(recovery.snapshot("g1").consecutiveFailures == 2, "G failures before success");
    recovery.noteSuccess("g1");
    const auto snap = recovery.snapshot("g1");
    expect(snap.state == RecoveryState::Connected, "G state CONNECTED");
    expect(snap.consecutiveFailures == 0, "G consecutiveFailures=0");
    expect(snap.backoffMs == kRecoveryInitialBackoff, "G backoff reset");
    expect(snap.nextAttemptAt.time_since_epoch().count() == 0,
           "G timer cleared");
    expect(snap.desired, "G desired remains true");
    clock->advance(ms(60000));
    expect(recovery.tick().empty(), "G no due event after success");
  }

  // -------------------------------------------------------------------
  // H. Disable → no future recovery event
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    recovery.armDesired("h1", true);
    (void)recovery.tick();
    recovery.disable("h1");
    const auto snap = recovery.snapshot("h1");
    expect(!snap.desired, "H desired=false");
    expect(snap.state == RecoveryState::Disabled, "H DISABLED");
    clock->advance(ms(60000));
    expect(recovery.tick().empty(), "H no future recovery event");
    expect(recovery.dueEventCount("h1") == 1, "H no extra due after disable");
  }

  // -------------------------------------------------------------------
  // I. Remove → no future recovery event
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    recovery.armDesired("i1", true);
    (void)recovery.tick();
    recovery.remove("i1");
    expect(recovery.controlCount() == 0, "I control removed");
    expect(recovery.snapshot("i1").state == RecoveryState::Disabled,
           "I missing snapshot is DISABLED");
    clock->advance(ms(60000));
    expect(recovery.tick().empty(), "I no future recovery event");
    expect(recovery.dueEventCount("i1") == 0, "I per-adapter count gone");
  }

  // -------------------------------------------------------------------
  // J. Rematerialization: old runtime cannot schedule the new runtime
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    recovery.armDesired("j1", true);
    const auto first = recovery.tick();
    expect(first.size() == 1, "J first due");
    const auto epoch0 = first[0].runtimeEpoch;
    const auto probe0 = first[0].probeId;
    recovery.rematerialize("j1");
    const auto after = recovery.snapshot("j1");
    expect(after.runtimeEpoch == epoch0 + 1, "J runtimeEpoch bumped");
    expect(after.activeProbeId == 0, "J old probe identity cleared");
    expect(after.state == RecoveryState::RecoveryWait,
           "J replacement waits, does not keep old PROBING");
    const auto second = recovery.tick();
    expect(second.size() == 1, "J new runtime gets its own due event");
    expect(second[0].runtimeEpoch == epoch0 + 1, "J new due uses new epoch");
    expect(second[0].probeId != probe0, "J new probe id");
    expect(recovery.dueProbes().size() == 1, "J one probing adapter");
    expect(recovery.dueProbes()[0].runtimeEpoch == epoch0 + 1,
           "J dueProbes is the new runtime");
  }

  // -------------------------------------------------------------------
  // K. Shutdown: control loop stops promptly
  // -------------------------------------------------------------------
  {
    RecoveryService recovery;
    recovery.armDesired("k1", true);
    recovery.start();
    expect(recovery.running(), "K running after start");
    const auto t0 = std::chrono::steady_clock::now();
    recovery.stop();
    const auto elapsed = std::chrono::duration_cast<ms>(
        std::chrono::steady_clock::now() - t0);
    expect(!recovery.running(), "K not running after stop");
    expect(elapsed < ms(1000), "K stop returns promptly");
    const auto after = recovery.tick();
    (void)after;
    recovery.armDesired("k1", true);
    // stop() completed; a subsequent start is allowed.
    recovery.start();
    recovery.stop();
    expect(!recovery.running(), "K second stop");
  }

  // -------------------------------------------------------------------
  // L. 100 adapters, independent timers, no retry queue
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    for (int i = 0; i < 100; ++i)
    {
      recovery.armDesired("l-" + std::to_string(i), false);
    }
    expect(recovery.controlCount() == 100, "L 100 RecoveryControl entries");
    expect(recovery.tick().empty(), "L none due before backoff");
    clock->advance(kRecoveryInitialBackoff);
    const auto due = recovery.tick();
    expect(due.size() == 100, "L all 100 become due together");
    expect(recovery.dueProbes().size() == 100, "L 100 probing, not a job queue");
    const auto again = recovery.tick();
    expect(again.empty(), "L second tick does not queue more jobs");
    expect(recovery.dueEventCount() == 100, "L 100 logical attempts total");
  }

  // -------------------------------------------------------------------
  // Long outage: 100 adapters, 24 hours, fake clock
  // -------------------------------------------------------------------
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    constexpr int kAdapters = 100;
    for (int i = 0; i < kAdapters; ++i)
    {
      recovery.armDesired("out-" + std::to_string(i), true);
    }
    expect(recovery.controlCount() == static_cast<std::size_t>(kAdapters),
           "outage one RecoveryControl per adapter");

    const auto horizon = clock->now() + std::chrono::hours(24);
    std::uint64_t driverDueSeen = 0;
    std::size_t maxDueBatch = 0;
    std::size_t maxProbing = 0;

    while (clock->now() < horizon)
    {
      const auto due = recovery.tick();
      driverDueSeen += due.size();
      if (due.size() > maxDueBatch)
      {
        maxDueBatch = due.size();
      }
      const auto probing = recovery.dueProbes().size();
      if (probing > maxProbing)
      {
        maxProbing = probing;
      }
      expect(recovery.controlCount() == static_cast<std::size_t>(kAdapters),
             "outage controlCount stays 100");
      expect(probing <= static_cast<std::size_t>(kAdapters),
             "outage probing set bounded by adapter count");

      for (const RecoveryDueEvent &event : due)
      {
        recovery.noteFailure(event.adapterId);
      }

      std::chrono::steady_clock::time_point next =
          std::chrono::steady_clock::time_point::max();
      for (const auto &snap : recovery.snapshots())
      {
        expect(snap.backoffMs <= kRecoveryMaxBackoff, "outage backoff capped");
        expect(snap.desired, "outage remains eligible");
        expect(snap.state == RecoveryState::RecoveryWait
                   || snap.state == RecoveryState::Probing,
               "outage still recovering");
        if (snap.nextAttemptAt.time_since_epoch().count() != 0
            && snap.nextAttemptAt < next)
        {
          next = snap.nextAttemptAt;
        }
      }
      if (next == std::chrono::steady_clock::time_point::max()
          || next <= clock->now())
      {
        clock->advance(ms(1000));
      }
      else
      {
        clock->advance(
            std::chrono::duration_cast<ms>(next - clock->now()));
      }
    }

    expect(recovery.controlCount() == static_cast<std::size_t>(kAdapters),
           "outage still 100 controls after 24h");
    expect(maxDueBatch <= static_cast<std::size_t>(kAdapters),
           "outage no accumulating retry jobs (batch ≤ 100)");
    expect(maxProbing <= static_cast<std::size_t>(kAdapters),
           "outage dueProbes bounded");
    expect(recovery.dueProbes().size() <= static_cast<std::size_t>(kAdapters),
           "outage no growing queue");
    expect(driverDueSeen > static_cast<std::uint64_t>(kAdapters),
           "outage each adapter recovered multiple times");
    for (int i = 0; i < kAdapters; ++i)
    {
      const auto snap = recovery.snapshot("out-" + std::to_string(i));
      expect(snap.desired, "outage adapter still desired");
      expect(snap.backoffMs == kRecoveryMaxBackoff, "outage backoff at 10s cap");
      expect(snap.consecutiveFailures > 0, "outage failures recorded");
      expect(snap.nextAttemptAt.time_since_epoch().count() != 0
                 || snap.state == RecoveryState::Probing,
             "outage nextAttemptAt remains bounded per adapter");
    }
  }

  // notifyUnavailable does not duplicate a wait already in progress
  {
    auto clock = makeClock();
    RecoveryService recovery(clock);
    recovery.armDesired("dup", false);
    const auto firstDeadline = recovery.snapshot("dup").nextAttemptAt;
    recovery.notifyUnavailable("dup");
    expect(recovery.snapshot("dup").nextAttemptAt == firstDeadline,
           "notifyUnavailable does not reset an existing wait");
  }

  if (failures == 0)
  {
    std::cout << "icp_recovery_control_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_recovery_control_test: " << failures << " failure(s)"
            << std::endl;
  return 1;
}
