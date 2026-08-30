#ifndef VIRTUAL_FACTORY_CIFX_RUNTIME_HH_
#define VIRTUAL_FACTORY_CIFX_RUNTIME_HH_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace virtual_factory
{
namespace internal
{

struct CifxChannelInfo
{
  std::string boardName;
  std::string firmwareName;
  unsigned firmwareMajor{0};
  unsigned firmwareMinor{0};
  unsigned firmwareBuild{0};
  std::size_t mailboxSize{0};
  std::size_t inputAreaBytes{0};
  std::size_t outputAreaBytes{0};
};

/// Process-wide cifX driver (Linux: cifXDriverInit / cifXDriverDeinit).
/// Windows: kernel NXDRV-WIN is assumed already loaded; xDriverOpen is used.
class CifxDriver
{
public:
  static bool acquire(std::string *error);
  static void release();
  static bool acquired();
};

/// One cifX communication channel. No protocol packet IDs invented here.
class CifxChannel
{
public:
  CifxChannel() = default;
  ~CifxChannel();

  CifxChannel(const CifxChannel &) = delete;
  CifxChannel &operator=(const CifxChannel &) = delete;

  bool open(const std::string &boardId, unsigned channel, std::string *error);
  void close();
  bool isOpen() const;

  bool queryInfo(CifxChannelInfo *info, std::string *error);
  bool setHostReady(bool ready, std::string *error);
  bool setBusOn(bool on, unsigned timeoutMs, std::string *error);
  bool downloadConfigFile(
      const std::string &path, std::string *error);

  bool readInput(
      std::size_t offset,
      std::size_t length,
      std::vector<std::uint8_t> *out,
      unsigned timeoutMs,
      std::string *error);
  bool writeOutput(
      std::size_t offset,
      const std::vector<std::uint8_t> &data,
      unsigned timeoutMs,
      std::string *error);

  static std::string formatError(int32_t code);

private:
  bool open_{false};
  void *driver_{nullptr};
  void *channel_{nullptr};
};

}  // namespace internal
}  // namespace virtual_factory

#endif
