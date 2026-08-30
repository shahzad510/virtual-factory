#ifndef VIRTUAL_FACTORY_PROFIBUS_SESSION_HH_
#define VIRTUAL_FACTORY_PROFIBUS_SESSION_HH_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
  /// Exported SYCON configuration artifact for the DP master.
  std::string configArtifactPath;
};

/// Private Hilscher PROFIBUS DP Master session (cifX API wrapper).
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

  bool readInputArea(std::size_t byteOffset, std::size_t length, std::vector<std::uint8_t> *out);
  bool writeOutputArea(
      std::size_t byteOffset,
      const std::vector<std::uint8_t> &data);

  std::string lastError() const;

private:
  bool open_{false};
  ProfibusSessionConfig config_;
  std::string last_error_;
#if defined(VF_HILSCHER_CIFX_AVAILABLE) && VF_HILSCHER_CIFX_AVAILABLE
  struct Impl;
  Impl *impl_{nullptr};
#endif
};

}  // namespace internal
}  // namespace virtual_factory

#endif
