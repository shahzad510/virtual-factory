#ifndef VIRTUAL_FACTORY_REST_TEST_SERVER_HH_
#define VIRTUAL_FACTORY_REST_TEST_SERVER_HH_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace virtual_factory
{
namespace test
{

/// In-process HTTP/1.1 fixture for RestIndustrialAdapter tests.
///
/// DEVELOPMENT/INTEGRATION VALIDATION ONLY: localhost, optional test auth,
/// no TLS. Not vendor API certification and not a production gateway.
/// Machine names (mixer, pump, unknown) are JSON resource labels only.
class RestTestServer
{
public:
  RestTestServer();
  ~RestTestServer();

  RestTestServer(const RestTestServer &) = delete;
  RestTestServer &operator=(const RestTestServer &) = delete;

  bool start();
  void stop();

  std::string host() const;
  std::uint16_t port() const;

  /// If non-empty, requests must send this exact Authorization header value
  /// (e.g. "Bearer test-token" or "Basic …"). Wrong/missing → HTTP 401.
  void requireAuthorization(std::string headerValue);

  void setSlowDelayMs(int delayMs);

  bool mixerRunning() const;
  bool mixerFault() const;
  double mixerSpeed() const;
  double mixerTemperature() const;
  void setMixerRunning(bool running);
  void setMixerFault(bool fault);
  void setMixerSpeed(double speed);
  void setMixerTemperature(double temperature);

  bool pumpRunning() const;
  bool pumpFault() const;
  double pumpFlow() const;
  double pumpPressure() const;
  void setPumpRunning(bool running);
  void setPumpFault(bool fault);
  void setPumpFlow(double flow);
  void setPumpPressure(double pressure);

  bool unknownRunning() const;
  bool unknownFault() const;
  double unknownTemperature() const;
  void setUnknownRunning(bool running);
  void setUnknownFault(bool fault);
  void setUnknownTemperature(double temperature);

  std::string lastMethod() const;
  std::string lastPath() const;
  std::string lastBody() const;
  int mixerStartCount() const;
  int pumpStartCount() const;

private:
  struct Machine
  {
    bool running{false};
    bool fault{false};
    double a{0.0};
    double b{0.0};
  };

  void run();
  void handleClient(int clientFd);
  void closeListen();
  void resetStateLocked();
  std::string handleRequestLocked(
      const std::string &method,
      const std::string &path,
      const std::string &authorization,
      const std::string &body,
      int *status);

  int listen_fd_{-1};
  std::atomic<int> client_fd_{-1};
  std::uint16_t port_{0};
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex mutex_;

  std::string required_authorization_;
  int slow_delay_ms_{1500};

  Machine mixer_;
  Machine pump_;
  Machine unknown_;

  std::string last_method_;
  std::string last_path_;
  std::string last_body_;
  int mixer_start_count_{0};
  int pump_start_count_{0};
};

}  // namespace test
}  // namespace virtual_factory

#endif
