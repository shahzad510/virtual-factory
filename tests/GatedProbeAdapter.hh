#ifndef VIRTUAL_FACTORY_TEST_GATED_PROBE_ADAPTER_HH_
#define VIRTUAL_FACTORY_TEST_GATED_PROBE_ADAPTER_HH_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <virtual_factory/equipment/GenericEquipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{
namespace test
{

struct ProbeHangGate
{
  std::mutex mu;
  std::condition_variable cv;
  bool release{false};

  void wait()
  {
    std::unique_lock<std::mutex> lock(this->mu);
    this->cv.wait(lock, [this]() { return this->release; });
  }

  bool waitFor(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(this->mu);
    return this->cv.wait_for(lock, timeout, [this]() { return this->release; });
  }

  void open()
  {
    std::lock_guard<std::mutex> lock(this->mu);
    this->release = true;
    this->cv.notify_all();
  }
};

/// Test-only adapter for ProbeExecutor. Production protocol adapters are not
/// modified. Supports gate-before-return, delay, never-return, late success
/// / failure, and a configurable protocol label for cross-protocol tests.
class GatedProbeAdapter : public IndustrialAdapter
{
public:
  explicit GatedProbeAdapter(std::string id, std::string protocol = "mock")
      : id_(std::move(id)), protocol_(std::move(protocol)),
        equipment_("EQ-" + id_, "motor")
  {
  }

  void setGate(std::shared_ptr<ProbeHangGate> gate) { this->gate_ = std::move(gate); }
  void setPeerAvailable(bool available) { this->peerAvailable_.store(available); }
  void setDelay(std::chrono::milliseconds delay) { this->delay_ = delay; }
  void setNeverReturn(bool enabled) { this->neverReturn_.store(enabled); }
  void setLateSuccess(bool enabled) { this->lateSuccess_.store(enabled); }

  int connectEntered() const { return this->connectEntered_.load(); }
  int connectCompleted() const { return this->connectCompleted_.load(); }
  int connectFailed() const { return this->connectFailed_.load(); }

  bool waitConnectEntered(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(this->mu_);
    return this->enteredCv_.wait_for(
        lock, timeout, [this]() { return this->entered_; });
  }

  std::string id() const override { return this->id_; }
  std::string protocol() const override { return this->protocol_; }
  ConnectionState connectionState() const override { return this->state_; }
  std::string lastError() const override { return this->lastError_; }

  bool connect() override
  {
    ++this->connectEntered_;
    {
      std::lock_guard<std::mutex> lock(this->mu_);
      this->entered_ = true;
    }
    this->enteredCv_.notify_all();

    if (this->delay_.count() > 0)
    {
      std::this_thread::sleep_for(this->delay_);
    }
    if (this->neverReturn_.load() && this->gate_)
    {
      this->gate_->wait();
    }
    else if (this->gate_)
    {
      this->gate_->wait();
    }

    const bool success = this->peerAvailable_.load() || this->lateSuccess_.load();
    if (success)
    {
      this->state_ = ConnectionState::Connected;
      this->lastError_.clear();
      ++this->connectCompleted_;
      return true;
    }
    this->state_ = ConnectionState::Faulted;
    this->lastError_ = "peer unavailable";
    ++this->connectFailed_;
    return false;
  }

  void disconnect() override
  {
    this->state_ = ConnectionState::Disconnected;
  }

  std::vector<Equipment *> equipment() override
  {
    if (this->state_ == ConnectionState::Disconnected)
    {
      return {};
    }
    return {&this->equipment_};
  }

  Equipment *equipmentById(const std::string &id) override
  {
    if (id == this->equipment_.id())
    {
      return &this->equipment_;
    }
    return nullptr;
  }

  void poll() override {}

private:
  std::string id_;
  std::string protocol_;
  GenericEquipment equipment_;
  ConnectionState state_{ConnectionState::Disconnected};
  std::string lastError_;
  std::shared_ptr<ProbeHangGate> gate_;
  std::atomic<bool> peerAvailable_{true};
  std::atomic<bool> neverReturn_{false};
  std::atomic<bool> lateSuccess_{false};
  std::chrono::milliseconds delay_{0};
  std::atomic<int> connectEntered_{0};
  std::atomic<int> connectCompleted_{0};
  std::atomic<int> connectFailed_{0};
  std::mutex mu_;
  std::condition_variable enteredCv_;
  bool entered_{false};
};

}  // namespace test
}  // namespace virtual_factory

#endif
