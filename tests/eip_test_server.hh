#ifndef VIRTUAL_FACTORY_EIP_TEST_SERVER_HH_
#define VIRTUAL_FACTORY_EIP_TEST_SERVER_HH_

#include <cstdint>
#include <string>
#include <vector>

namespace virtual_factory
{
namespace test
{

/// Local libplctag ab_server ControlLogix emulator for eip_adapter_test.
///
/// DEVELOPMENT / INTEGRATION VALIDATION ONLY. Not Allen-Bradley / Rockwell
/// hardware certification. Not linked into the production adapter library.
class EipTestServer
{
public:
  EipTestServer() = default;
  ~EipTestServer();

  EipTestServer(const EipTestServer &) = delete;
  EipTestServer &operator=(const EipTestServer &) = delete;

  bool start(const std::vector<std::string> &tags);
  void stop();

  std::string host() const;
  std::uint16_t port() const;
  std::string path() const;
  std::string lastError() const;

private:
  bool waitReady();

  pid_t pid_{-1};
  std::uint16_t port_{0};
  std::string last_error_;
  std::string path_{"1,0"};
};

}  // namespace test
}  // namespace virtual_factory

#endif
