// SOFTWARE-INTEGRATION / HARDWARE-READINESS TEST
// Enumerates cifX readiness and prints PN/PB hardware test plans.
// This is NOT a REAL PROFINET TEST, REAL PROFIBUS TEST, or HARDWARE VALIDATION.

#include "hilscher/hilscher_hardware_readiness.hh"
#include "hilscher/hilscher_hardware_test_harness.hh"

#include <virtual_factory/icp/AdapterFactory.hh>
#include <virtual_factory/icp/config/ConfigurationCatalog.hh>
#include <virtual_factory/icp/config/ConfigurationValidator.hh>
#include <virtual_factory/icp/config/JsonFileConfigurationRepository.hh>
#include <virtual_factory/icp/config/NativeFieldbusConfigMapper.hh>

#include <iostream>
#include <string>

namespace
{

int failures = 0;

void expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << "FAIL: " << message << std::endl;
    ++failures;
  }
}

bool contains(const std::string &haystack, const char *needle)
{
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main()
{
  using virtual_factory::internal::HardwareTestPlan;
  using virtual_factory::internal::HardwareTestStepStatus;
  using virtual_factory::internal::HilscherProtocolTarget;
  using virtual_factory::internal::HilscherReadinessRequest;
  using virtual_factory::internal::HilscherReadinessState;
  using virtual_factory::internal::assessHilscherHardwareReadiness;
  using virtual_factory::internal::buildProfinetHardwareTestPlan;
  using virtual_factory::internal::buildProfibusHardwareTestPlan;
  using virtual_factory::internal::formatHardwareTestPlan;
  using virtual_factory::internal::formatHilscherReadinessReport;
  using virtual_factory::internal::hardwareTestStepStatusLabel;
  using virtual_factory::internal::hilscherReadinessStateLabel;

  HilscherReadinessRequest any;
  const auto report = assessHilscherHardwareReadiness(any);
  expect(!report.stateLabel.empty(), "readiness state label set");
  expect(
      report.stateLabel == hilscherReadinessStateLabel(report.state),
      "state label matches enum");
  expect(!report.manualChecks.empty(), "manual verification checklist present");
  expect(
      contains(formatHilscherReadinessReport(report), report.stateLabel.c_str()),
      "formatted report includes state");

  if (!report.sdkCompiledIn)
  {
    expect(
        report.state == HilscherReadinessState::SdkMissing,
        "default build without backend reports SDK_MISSING");
  }
  else
  {
    expect(
        report.state == HilscherReadinessState::DriverInitFailed
            || report.state == HilscherReadinessState::NoBoard
            || report.state == HilscherReadinessState::NoChannel
            || report.state == HilscherReadinessState::WrongFirmware
            || report.state == HilscherReadinessState::FirmwareNotReady
            || report.state == HilscherReadinessState::ReadyForHostIoTest
            || report.state
                   == HilscherReadinessState::LicenseManualVerificationRequired
            || report.state
                   == HilscherReadinessState::ManualVerificationRequired,
        "SDK-present readiness is a known host state (no invented success)");
    // This cloud host has no CIFX card — expect no false READY_FOR_TEST with boards.
    if (report.inventory.boards.empty()
        && report.state != HilscherReadinessState::DriverInitFailed)
    {
      expect(
          report.state == HilscherReadinessState::NoBoard,
          "empty board list → NO_BOARD");
    }
  }

  HilscherReadinessRequest pnReq;
  pnReq.protocol = HilscherProtocolTarget::Profinet;
  const HardwareTestPlan pn = buildProfinetHardwareTestPlan(pnReq);
  expect(pn.protocol == "profinet", "profinet plan protocol");
  expect(pn.steps.size() >= 18U, "profinet plan has full checklist");
  bool sawBlockedDcp = false;
  bool sawMappingPass = false;
  for (const auto &entry : pn.steps)
  {
    if (entry.id == "pn-03")
    {
      expect(
          entry.status == HardwareTestStepStatus::BlockedProtocolApi,
          "DCP remains BLOCKED_PROTOCOL_API");
      sawBlockedDcp = true;
    }
    if (entry.id == "pn-17")
    {
      expect(
          entry.status == HardwareTestStepStatus::PassedSoftwareBoundary,
          "GenericEquipment mapping software-boundary covered");
      sawMappingPass = true;
    }
  }
  expect(sawBlockedDcp, "DCP step present");
  expect(sawMappingPass, "mapping step present");
  expect(
      contains(formatHardwareTestPlan(pn), "BLOCKED_PROTOCOL_API"),
      "formatted PN plan shows protocol API boundary");

  HilscherReadinessRequest pbReq;
  pbReq.protocol = HilscherProtocolTarget::Profibus;
  const HardwareTestPlan pb = buildProfibusHardwareTestPlan(pbReq);
  expect(pb.protocol == "profibus", "profibus plan protocol");
  expect(pb.steps.size() >= 15U, "profibus plan has full checklist");
  bool sawBlockedDp = false;
  for (const auto &entry : pb.steps)
  {
    if (entry.id == "pb-08")
    {
      expect(
          entry.status == HardwareTestStepStatus::BlockedProtocolApi,
          "cyclic DP remains BLOCKED_PROTOCOL_API");
      sawBlockedDp = true;
      expect(
          contains(entry.detail, "Do not invent"),
          "DP step forbids invented packets");
    }
  }
  expect(sawBlockedDp, "cyclic DP step present");

#ifndef VF_ICP_NATIVE_EXAMPLE_DIR
#define VF_ICP_NATIVE_EXAMPLE_DIR "."
#endif
  {
    using virtual_factory::icp::ConfigurationCatalog;
    using virtual_factory::icp::ConfigurationValidator;
    using virtual_factory::icp::JsonFileConfigurationRepository;
    using virtual_factory::icp::NativeFieldbusConfigMapper;
    using virtual_factory::icp::AdapterFactory;

    const std::string examples[] = {
        "profinet-single-iodevice.json",
        "profinet-multi-iodevice.json",
        "profibus-single-slave.json",
        "profibus-multi-slave.json",
    };
    for (const std::string &name : examples)
    {
      const std::string path =
          std::string(VF_ICP_NATIVE_EXAMPLE_DIR) + "/" + name;
      JsonFileConfigurationRepository repo(path);
      ConfigurationCatalog catalog;
      const auto loaded = catalog.load(repo);
      expect(loaded.ok, ("load example " + name).c_str());
      const auto validated = ConfigurationValidator::validate(catalog.document());
      expect(validated.ok, ("validate example " + name).c_str());
      expect(catalog.adapterCount() == 1U, ("one adapter in " + name).c_str());
      const auto *adapter = catalog.document().adapters.empty()
                                ? nullptr
                                : &catalog.document().adapters.front();
      expect(adapter != nullptr, ("adapter present in " + name).c_str());
      if (adapter == nullptr)
      {
        continue;
      }
      if (adapter->protocol == "profinet")
      {
        virtual_factory::ProfinetIndustrialAdapter::AdapterConfig mapped;
        const auto mapResult =
            NativeFieldbusConfigMapper::toProfinet(*adapter, &mapped);
        expect(mapResult.ok, ("map PN example " + name).c_str());
        auto built = AdapterFactory::createProfinetFromRecord(*adapter);
        expect(built != nullptr, ("factory PN example " + name).c_str());
        expect(!built->connect(), ("PN example still blocked without HW " + name).c_str());
        expect(
            built->connectionState()
                == virtual_factory::ConnectionState::Faulted,
            ("PN example Faulted " + name).c_str());
        built->disconnect();
        expect(
            built->connectionState()
                == virtual_factory::ConnectionState::Disconnected,
            ("PN example disconnect " + name).c_str());
      }
      else if (adapter->protocol == "profibus")
      {
        virtual_factory::ProfibusIndustrialAdapter::AdapterConfig mapped;
        const auto mapResult =
            NativeFieldbusConfigMapper::toProfibus(*adapter, &mapped);
        expect(mapResult.ok, ("map PB example " + name).c_str());
        auto built = AdapterFactory::createProfibusFromRecord(*adapter);
        expect(built != nullptr, ("factory PB example " + name).c_str());
        expect(!built->connect(), ("PB example still blocked without HW " + name).c_str());
      }
    }
  }

  // Ensure we never claim plant success without boards.
  for (const auto &entry : pn.steps)
  {
    expect(
        entry.status != HardwareTestStepStatus::Failed
            || !report.inventory.boards.empty(),
        "no unexpected FAILED without hardware attempt");
    (void)hardwareTestStepStatusLabel(entry.status);
  }

  std::cout << formatHilscherReadinessReport(report);
  std::cout << formatHardwareTestPlan(pn);
  std::cout << formatHardwareTestPlan(pb);

  if (failures != 0)
  {
    std::cerr << failures
              << " HARDWARE-READINESS SOFTWARE-INTEGRATION failure(s)\n";
    return 1;
  }

  std::cout
      << "hilscher_hardware_readiness_test: OK "
         "(SOFTWARE-INTEGRATION / HARDWARE-READINESS — not hardware validation)\n";
  return 0;
}
