#ifndef VIRTUAL_FACTORY_HILSCHER_HARDWARE_TEST_HARNESS_HH_
#define VIRTUAL_FACTORY_HILSCHER_HARDWARE_TEST_HARNESS_HH_

#include <string>
#include <vector>

#include "hilscher_hardware_readiness.hh"

namespace virtual_factory
{
namespace internal
{

/// Outcome of one hardware-validation step.
/// SOFTWARE-INTEGRATION may mark SKIPPED_* without claiming plant IO.
enum class HardwareTestStepStatus
{
  NotRun,
  PassedSoftwareBoundary,
  SkippedNoHardware,
  /// Requires NXLFW-PNM / DPM Protocol API packet definitions not present here.
  BlockedProtocolApi,
  ManualVerificationRequired,
  Failed
};

struct HardwareTestStep
{
  std::string id;
  std::string title;
  HardwareTestStepStatus status{HardwareTestStepStatus::NotRun};
  std::string detail;
};

struct HardwareTestPlan
{
  std::string protocol;
  std::string hardwareTarget;
  HilscherReadinessReport readiness;
  std::vector<HardwareTestStep> steps;
};

const char *hardwareTestStepStatusLabel(HardwareTestStepStatus status);

/// Build the PROFINET hardware validation plan.
/// Runs host readiness now. Plant steps stay SKIPPED / BLOCKED until hardware
/// and Protocol API materials are available. Does not invent DCP/AR packets.
HardwareTestPlan buildProfinetHardwareTestPlan(
    const HilscherReadinessRequest &request = {});

/// Build the PROFIBUS hardware validation plan (same honesty rules).
HardwareTestPlan buildProfibusHardwareTestPlan(
    const HilscherReadinessRequest &request = {});

std::string formatHardwareTestPlan(const HardwareTestPlan &plan);

}  // namespace internal
}  // namespace virtual_factory

#endif
