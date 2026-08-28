#ifndef VIRTUAL_FACTORY_EIP_SESSION_HH_
#define VIRTUAL_FACTORY_EIP_SESSION_HH_

#include <cstdint>
#include <string>
#include <unordered_map>

namespace virtual_factory
{
namespace internal
{

enum class EipTagValueType
{
  Bool,
  Dint,
  Real
};

struct EipSessionConfig
{
  std::string host{"127.0.0.1"};
  std::uint16_t port{44818};
  std::string path{"1,0"};
  std::string plcType{"ControlLogix"};
  int timeoutMs{2000};
};

class EipSession
{
public:
  EipSession() = default;
  ~EipSession();

  EipSession(const EipSession &) = delete;
  EipSession &operator=(const EipSession &) = delete;

  bool open(const EipSessionConfig &config);
  void close();
  bool connected() const;

  bool createTag(
      const std::string &key,
      const std::string &tagName,
      EipTagValueType valueType);
  void destroyAllTags();

  bool readBool(const std::string &key, bool *value);
  bool readDint(const std::string &key, std::int32_t *value);
  bool readReal(const std::string &key, float *value);

  bool writeBool(const std::string &key, bool value);
  bool writeDint(const std::string &key, std::int32_t value);
  bool writeReal(const std::string &key, float value);

  void setTimeoutMs(int timeoutMs);
  std::string lastError() const;

private:
  struct TagEntry
  {
    std::int32_t id{-1};
    EipTagValueType valueType{EipTagValueType::Dint};
  };

  bool ensureOpen();
  TagEntry *findTag(const std::string &key);
  std::string buildAttrib(const std::string &tagName) const;
  void captureError(int rc, const char *prefix);
  void captureError(const char *message);
  int timeoutMs() const;

  bool open_{false};
  EipSessionConfig config_;
  std::unordered_map<std::string, TagEntry> tags_;
  std::string last_error_;
};

}  // namespace internal
}  // namespace virtual_factory

#endif
