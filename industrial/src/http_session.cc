#include "http_session.hh"

#include <curl/curl.h>

#include <mutex>
#include <sstream>

namespace virtual_factory
{
namespace internal
{

namespace
{

std::once_flag gCurlOnce;

void curlGlobalInit()
{
  (void)curl_global_init(CURL_GLOBAL_DEFAULT);
}

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  auto *out = static_cast<std::string *>(userdata);
  if (out == nullptr || ptr == nullptr)
  {
    return 0;
  }
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

std::string trimSlashes(std::string value, bool keepLeading)
{
  while (value.size() > 1 && value.back() == '/')
  {
    value.pop_back();
  }
  if (value.empty())
  {
    return value;
  }
  if (keepLeading && value.front() != '/')
  {
    value.insert(value.begin(), '/');
  }
  return value;
}

const char *methodName(HttpMethod method)
{
  switch (method)
  {
    case HttpMethod::Get:
      return "GET";
    case HttpMethod::Post:
      return "POST";
    case HttpMethod::Put:
      return "PUT";
    case HttpMethod::Patch:
      return "PATCH";
  }
  return "GET";
}

}  // namespace

HttpSession::~HttpSession()
{
  this->close();
}

void HttpSession::configure(HttpSessionConfig config)
{
  this->config_ = std::move(config);
  (void)this->recreateHandle();
}

void HttpSession::close()
{
  if (this->easy_ != nullptr)
  {
    curl_easy_cleanup(static_cast<CURL *>(this->easy_));
    this->easy_ = nullptr;
  }
}

bool HttpSession::probeConnect()
{
  this->last_error_.clear();
  if (!this->recreateHandle())
  {
    return false;
  }

  CURL *easy = static_cast<CURL *>(this->easy_);
  const std::string url = this->originUrl() + "/";
  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 1L);
  curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L);

  const CURLcode code = curl_easy_perform(easy);
  if (code != CURLE_OK)
  {
    this->captureError("connect failed", static_cast<int>(code));
    this->close();
    return false;
  }

  // Drop the CONNECT_ONLY socket; later mapped requests use a fresh handle.
  return this->recreateHandle();
}

HttpResponse HttpSession::request(
    HttpMethod method, const std::string &path, const std::string &body)
{
  HttpResponse response;
  this->last_error_.clear();

  if (this->easy_ == nullptr && !this->recreateHandle())
  {
    response.error = this->last_error_;
    return response;
  }

  CURL *easy = static_cast<CURL *>(this->easy_);
  curl_easy_reset(easy);
  this->applyCommonOptions();

  const std::string url = this->joinUrl(path);
  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 0L);

  switch (method)
  {
    case HttpMethod::Get:
      curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L);
      break;
    case HttpMethod::Post:
      curl_easy_setopt(easy, CURLOPT_POST, 1L);
      curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body.c_str());
      curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
      break;
    case HttpMethod::Put:
    case HttpMethod::Patch:
      curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, methodName(method));
      curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body.c_str());
      curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
      break;
  }

  std::string received;
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &received);

  curl_slist *headers = static_cast<curl_slist *>(
      this->makeHeaders(body, method != HttpMethod::Get));

  const CURLcode code = curl_easy_perform(easy);
  if (headers != nullptr)
  {
    curl_slist_free_all(headers);
  }

  if (code != CURLE_OK)
  {
    this->captureError("HTTP request failed", static_cast<int>(code));
    response.error = this->last_error_;
    return response;
  }

  response.transportOk = true;
  response.body = std::move(received);
  curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response.status);
  if (!response.success())
  {
    std::ostringstream oss;
    oss << "HTTP " << response.status << " " << methodName(method) << " "
        << path;
    this->last_error_ = oss.str();
    response.error = this->last_error_;
  }
  return response;
}

std::string HttpSession::lastError() const
{
  return this->last_error_;
}

void HttpSession::ensureCurlGlobal()
{
  std::call_once(gCurlOnce, curlGlobalInit);
}

bool HttpSession::recreateHandle()
{
  this->ensureCurlGlobal();
  this->close();
  CURL *easy = curl_easy_init();
  if (easy == nullptr)
  {
    this->last_error_ = "curl_easy_init failed";
    return false;
  }
  this->easy_ = easy;
  this->applyCommonOptions();
  return true;
}

void HttpSession::applyCommonOptions()
{
  CURL *easy = static_cast<CURL *>(this->easy_);
  if (easy == nullptr)
  {
    return;
  }

  curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, static_cast<long>(this->config_.timeoutMs));
  curl_easy_setopt(
      easy, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(this->config_.timeoutMs));
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(easy, CURLOPT_USERAGENT, "virtual-factory-rest-adapter/6E");

  if (this->config_.tlsVerify)
  {
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
  }
  else
  {
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  if (this->config_.useBasicAuth)
  {
    curl_easy_setopt(easy, CURLOPT_HTTPAUTH, static_cast<long>(CURLAUTH_BASIC));
    // USERPWD is not copied into last_error_.
    const std::string userpwd =
        this->config_.username + ":" + this->config_.password;
    curl_easy_setopt(easy, CURLOPT_USERPWD, userpwd.c_str());
  }
}

void HttpSession::captureError(const char *prefix, int curlCode)
{
  const char *curlText = curl_easy_strerror(static_cast<CURLcode>(curlCode));
  this->last_error_ = std::string(prefix) + ": " + curlText;
}

std::string HttpSession::originUrl() const
{
  std::ostringstream oss;
  oss << this->config_.scheme << "://" << this->config_.host << ":"
      << this->config_.port;
  return oss.str();
}

std::string HttpSession::joinUrl(const std::string &path) const
{
  return this->originUrl() + trimSlashes(this->config_.basePath, true) +
         trimSlashes(path, true);
}

void *HttpSession::makeHeaders(const std::string & /*body*/, bool write)
{
  curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "Expect:");
  if (write)
  {
    headers = curl_slist_append(headers, "Content-Type: application/json");
  }
  if (this->config_.useBearerAuth && !this->config_.bearerToken.empty())
  {
    const std::string header =
        std::string("Authorization: Bearer ") + this->config_.bearerToken;
    headers = curl_slist_append(headers, header.c_str());
  }
  curl_easy_setopt(static_cast<CURL *>(this->easy_), CURLOPT_HTTPHEADER, headers);
  return headers;
}

}  // namespace internal
}  // namespace virtual_factory
