#ifndef VIRTUAL_FACTORY_ICP_HTTP_API_SERVER_HH_
#define VIRTUAL_FACTORY_ICP_HTTP_API_SERVER_HH_

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <virtual_factory/icp/app/ApplicationService.hh>

namespace virtual_factory
{
namespace icp
{

/// Minimal HTTP Application API + static GUI host for standalone ICP.
/// Not CIC. Not MES. Versioned under /api/v1/.
class HttpApiServer
{
public:
  HttpApiServer(
      ApplicationService &service,
      std::string staticRoot,
      std::string bindHost = "127.0.0.1",
      int bindPort = 8080);
  ~HttpApiServer();

  HttpApiServer(const HttpApiServer &) = delete;
  HttpApiServer &operator=(const HttpApiServer &) = delete;

  bool start();
  void stop();
  bool running() const;
  int port() const;
  const std::string &host() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
