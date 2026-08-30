#ifndef VIRTUAL_FACTORY_PROFINET_SESSION_HH_
#define VIRTUAL_FACTORY_PROFINET_SESSION_HH_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cifx_runtime.hh"

namespace virtual_factory
{
namespace internal
{

struct ProfinetSessionConfig
{
  std::string boardId;
  unsigned channel{0};
  /// Exported SYCON / Communication Studio artifact (e.g. config.nxd).
  std::string configArtifactPath;
  /// Optional firmware name substring (e.g. "PROFINET"); empty = skip check.
  std::string expectedFirmwareName;
  unsigned ioTimeoutMs{1000};
};

/// Private Hilscher PROFINET IO-Controller session.
/// Uses the official cifX API only. Protocol mailbox (DCP/AR) packet IDs
/// from NXLFW-PNM Protocol API headers are NOT invented here.
class ProfinetSession
{
public:
  ProfinetSession() = default;
  ~ProfinetSession();

  ProfinetSession(const ProfinetSession &) = delete;
  ProfinetSession &operator=(const ProfinetSession &) = delete;

  bool sdkAvailable() const;

  bool open(const ProfinetSessionConfig &config);
  void close();
  bool connected() const;

  bool readInputArea(
      std::size_t byteOffset, std::size_t length, std::vector<std::uint8_t> *out);
  bool writeOutputArea(
      std::size_t byteOffset, const std::vector<std::uint8_t> &data);

  std::string firmwareName() const;
  std::string lastError() const;

private:
  bool matchesFirmware(const std::string &name) const;

  bool open_{false};
  ProfinetSessionConfig config_;
  std::string last_error_;
  std::string firmware_name_;
  CifxChannel channel_;
};

}  // namespace internal
}  // namespace virtual_factory

#endif
