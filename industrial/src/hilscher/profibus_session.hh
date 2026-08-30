#ifndef VIRTUAL_FACTORY_PROFIBUS_SESSION_HH_
#define VIRTUAL_FACTORY_PROFIBUS_SESSION_HH_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cifx_runtime.hh"

namespace virtual_factory
{
namespace internal
{

struct ProfibusSessionConfig
{
  std::string boardId;
  unsigned channel{0};
  unsigned masterAddress{1};
  unsigned baudRateKbps{19200};
  std::string configArtifactPath;
  std::string expectedFirmwareName;
  unsigned ioTimeoutMs{1000};
};

/// Private Hilscher PROFIBUS DP Master session.
/// Uses the official cifX API only. DP-V1 / GSD protocol packet IDs from
/// NXLFW-DPM Protocol API headers are NOT invented here.
class ProfibusSession
{
public:
  ProfibusSession() = default;
  ~ProfibusSession();

  ProfibusSession(const ProfibusSession &) = delete;
  ProfibusSession &operator=(const ProfibusSession &) = delete;

  bool sdkAvailable() const;

  bool open(const ProfibusSessionConfig &config);
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
  ProfibusSessionConfig config_;
  std::string last_error_;
  std::string firmware_name_;
  CifxChannel channel_;
};

}  // namespace internal
}  // namespace virtual_factory

#endif
