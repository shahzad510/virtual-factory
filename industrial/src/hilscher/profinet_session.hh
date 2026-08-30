#ifndef VIRTUAL_FACTORY_PROFINET_SESSION_HH_
#define VIRTUAL_FACTORY_PROFINET_SESSION_HH_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace virtual_factory
{
namespace internal
{

struct ProfinetSessionConfig
{
  /// cifX board identifier (driver enumeration / slot id).
  std::string boardId;
  unsigned channel{0};
  /// Exported SYCON / Communication Studio artifact (e.g. config.nxd).
  std::string configArtifactPath;
};

/// Private Hilscher PROFINET IO-Controller session (cifX API wrapper).
/// No Hilscher types in this header.
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

  /// Read bytes from the controller process input image.
  bool readInputArea(std::size_t byteOffset, std::size_t length, std::vector<std::uint8_t> *out);

  /// Write bytes to the controller process output image.
  bool writeOutputArea(
      std::size_t byteOffset,
      const std::vector<std::uint8_t> &data);

  std::string lastError() const;

private:
  bool open_{false};
  ProfinetSessionConfig config_;
  std::string last_error_;
#if defined(VF_HILSCHER_CIFX_AVAILABLE) && VF_HILSCHER_CIFX_AVAILABLE
  struct Impl;
  Impl *impl_{nullptr};
#endif
};

}  // namespace internal
}  // namespace virtual_factory

#endif
