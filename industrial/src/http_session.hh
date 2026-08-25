#ifndef VIRTUAL_FACTORY_HTTP_SESSION_HH_
#define VIRTUAL_FACTORY_HTTP_SESSION_HH_

// Private industrial-layer helper. Not part of the public adapter API.
// Not included from Equipment or IndustrialAdapter headers.
// Implementation uses libcurl; this header does not include <curl/curl.h>.

#include <cstdint>
#include <string>

namespace virtual_factory
{
namespace internal
{

enum class HttpMethod
{
  Get,
  Post,
  Put,
  Patch
};

struct HttpSessionConfig
{
  std::string scheme{"http"};
  std::string host{"127.0.0.1"};
  std::uint16_t port{80};
  std::string basePath;
  int timeoutMs{2000};
  bool tlsVerify{true};
  bool useBasicAuth{false};
  std::string username;
  std::string password;
  bool useBearerAuth{false};
  std::string bearerToken;
};

struct HttpResponse
{
  bool transportOk{false};
  long status{0};
  std::string body;
  std::string error;

  bool success() const
  {
    return this->transportOk && this->status >= 200 && this->status < 300;
  }
};

/// Blocking HTTP/1.1 client session via libcurl.
class HttpSession
{
public:
  HttpSession() = default;
  ~HttpSession();

  HttpSession(const HttpSession &) = delete;
  HttpSession &operator=(const HttpSession &) = delete;

  void configure(HttpSessionConfig config);
  void close();

  /// TCP (and TLS, for https) connect without requiring a health API.
  bool probeConnect();

  HttpResponse request(
      HttpMethod method, const std::string &path, const std::string &body);

  std::string lastError() const;

private:
  void ensureCurlGlobal();
  bool recreateHandle();
  void applyCommonOptions();
  void captureError(const char *prefix, int curlCode);
  std::string originUrl() const;
  std::string joinUrl(const std::string &path) const;
  void *makeHeaders(const std::string &body, bool write);

  void *easy_{nullptr};
  HttpSessionConfig config_;
  std::string last_error_;
};

}  // namespace internal
}  // namespace virtual_factory

#endif
