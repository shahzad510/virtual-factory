#ifndef VIRTUAL_FACTORY_ICP_JSON_FILE_CONFIGURATION_REPOSITORY_HH_
#define VIRTUAL_FACTORY_ICP_JSON_FILE_CONFIGURATION_REPOSITORY_HH_

#include <string>

#include <virtual_factory/icp/config/ConfigurationRepository.hh>

namespace virtual_factory
{
namespace icp
{

/// Human-readable JSON file. Atomic replace via temp file + rename.
/// Does not require Hilscher SDK. Secrets must be references, not values.
class JsonFileConfigurationRepository : public ConfigurationRepository
{
public:
  explicit JsonFileConfigurationRepository(std::string path);

  ConfigResult load(IcpConfigurationDocument *out) override;
  ConfigResult save(const IcpConfigurationDocument &document) override;

  const std::string &path() const;

  /// Parse JSON text (for tests and migration). Does not touch disk.
  static ConfigResult parseText(
      const std::string &jsonText, IcpConfigurationDocument *out);

  /// Deterministic JSON (fixed key order via model serialization).
  static std::string toJsonText(const IcpConfigurationDocument &document);

private:
  std::string path_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
