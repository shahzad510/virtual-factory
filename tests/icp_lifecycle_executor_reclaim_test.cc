/// LifecycleExecutor runtime worker reclamation: non-blocking Teardown,
/// abandonStaleInFlight 1-for-1 replacement, hard bounds, generation safety.

#include <virtual_factory/icp/LifecycleExecutor.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{

int failures = 0;

void expect(bool condition, const std::string &message)
{
  if (!condition)
  {
    std::cerr << "FAIL: " << message << std::endl;
    ++failures;
  }
}

bool waitFor(
    const std::function<bool()> &pred, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (pred())
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}

struct HangGate
{
  std::mutex mu;
  std::condition_variable cv;
  bool release{false};

  void wait()
  {
    std::unique_lock<std::mutex> lock(this->mu);
    this->cv.wait(lock, [this]() { return this->release; });
  }

  void open()
  {
    std::lock_guard<std::mutex> lock(this->mu);
    this->release = true;
    this->cv.notify_all();
  }
};

class StubAdapter : public virtual_factory::IndustrialAdapter
{
public:
  explicit StubAdapter(std::string id) : id_(std::move(id)) {}

  std::string id() const override { return this->id_; }
  std::string protocol() const override { return "mock"; }
  virtual_factory::ConnectionState connectionState() const override
  {
    return this->state_;
  }
  std::string lastError() const override { return {}; }
  bool connect() override
  {
    this->state_ = virtual_factory::ConnectionState::Connected;
    return true;
  }
  void disconnect() override
  {
    ++this->disconnects_;
    this->state_ = virtual_factory::ConnectionState::Disconnected;
  }
  void poll() override {}
  std::vector<virtual_factory::Equipment *> equipment() override { return {}; }
  virtual_factory::Equipment *equipmentById(const std::string &) override
  {
    return nullptr;
  }
  int disconnects() const { return this->disconnects_.load(); }

private:
  std::string id_;
  virtual_factory::ConnectionState state_{
      virtual_factory::ConnectionState::Connected};
  std::atomic<int> disconnects_{0};
};

}  // namespace

int main()
{
  using virtual_factory::icp::LifecycleExecutor;
  using virtual_factory::icp::LifecycleJob;
  using virtual_factory::icp::LifecycleOp;
  using virtual_factory::icp::kMaxAbandonedLifecycleWorkers;
  using virtual_factory::icp::kMaxAbandonedPerAdapter;

  // ---------------------------------------------------------------------
  // H1 — Run B at the executor: two abandoned hangs must not starve Gen2.
  // ---------------------------------------------------------------------
  {
    LifecycleExecutor exec;
    std::mutex mu;
    std::unordered_map<std::uint64_t, std::shared_ptr<HangGate>> gates;
    std::unordered_map<std::uint64_t, std::atomic<int>> entered;
    for (std::uint64_t g = 0; g < 3; ++g)
    {
      gates[g] = std::make_shared<HangGate>();
      entered[g] = 0;
    }
    std::atomic<int> otherEntered{0};
    exec.setHandler([&](const LifecycleJob &job) {
      if (job.adapterId == "other")
      {
        ++otherEntered;
        return;
      }
      ++entered[job.generation];
      auto it = gates.find(job.generation);
      if (it != gates.end())
      {
        it->second->wait();
      }
    });
    exec.start(4);
    expect(exec.liveWorkerCount() == 4, "H1 live workers start at 4");

    LifecycleJob j0;
    j0.adapterId = "hang";
    j0.op = LifecycleOp::RecoveryConnect;
    j0.generation = 0;
    expect(exec.enqueue(j0), "H1 enqueue Gen0");
    expect(waitFor([&]() { return entered[0].load() >= 1; },
                   std::chrono::milliseconds(2000)),
           "H1 Gen0 entered");

    const std::uint64_t gen1 = exec.bumpGeneration("hang");
    expect(gen1 == 1, "H1 bump to generation 1");
    expect(!exec.hasInFlight("hang", LifecycleOp::RecoveryConnect),
           "H1 stale Gen0 is not current-generation in-flight");
    expect(exec.hasInFlight("hang", LifecycleOp::RecoveryConnect, 0),
           "H1 Gen0 still tracked as stale in-flight");
    expect(exec.abandonStaleInFlight("hang", 0), "H1 abandon Gen0");
    expect(exec.liveWorkerCount() == 4, "H1 live workers still 4 after Gen0 abandon");
    expect(exec.abandonedWorkerCount() == 1, "H1 abandonedCount == 1");
    expect(exec.abandonedWorkerCount("hang") == 1, "H1 per-adapter abandoned == 1");

    auto io = std::make_shared<std::mutex>();
    io->lock();
    auto stub0 = std::make_shared<StubAdapter>("hang-old-0");
    LifecycleJob tear0;
    tear0.adapterId = "hang#teardown-0";
    tear0.op = LifecycleOp::Teardown;
    tear0.teardownAdapter = stub0;
    tear0.teardownIoMutex = io;
    expect(exec.enqueue(tear0), "H1 enqueue busy Teardown 0");

    LifecycleJob j1;
    j1.adapterId = "hang";
    j1.op = LifecycleOp::RecoveryConnect;
    j1.generation = gen1;
    expect(exec.enqueue(j1), "H1 enqueue Gen1");
    expect(waitFor([&]() { return entered[1].load() >= 1; },
                   std::chrono::milliseconds(2000)),
           "H1 Gen1 entered while Gen0 blocked and Teardown parked");
    expect(entered[0].load() == 1, "H1 Gen0 still in its first connect");

    const std::uint64_t gen2 = exec.bumpGeneration("hang");
    expect(exec.abandonStaleInFlight("hang", gen1), "H1 abandon Gen1");
    expect(exec.liveWorkerCount() == 4, "H1 live workers still 4 after Gen1 abandon");
    expect(exec.abandonedWorkerCount() <= kMaxAbandonedLifecycleWorkers,
           "H1 abandonedCount within global bound");
    expect(exec.abandonedWorkerCount("hang") == 2, "H1 per-adapter abandoned == 2");

    auto stub1 = std::make_shared<StubAdapter>("hang-old-1");
    LifecycleJob tear1;
    tear1.adapterId = "hang#teardown-1";
    tear1.op = LifecycleOp::Teardown;
    tear1.teardownAdapter = stub1;
    tear1.teardownIoMutex = io;
    expect(exec.enqueue(tear1), "H1 enqueue busy Teardown 1");

    LifecycleJob j2;
    j2.adapterId = "hang";
    j2.op = LifecycleOp::RecoveryConnect;
    j2.generation = gen2;
    expect(exec.enqueue(j2), "H1 enqueue Gen2");
    expect(waitFor([&]() { return entered[2].load() >= 1; },
                   std::chrono::milliseconds(2000)),
           "H1 Gen2 ENTERED while Gen0 and Gen1 remain blocked");
    expect(entered[0].load() == 1 && entered[1].load() == 1,
           "H1 Gen0 and Gen1 still blocked");
    expect(exec.liveWorkerCount() == 4, "H1 live workers == 4 while Gen2 runs");
    expect(exec.abandonedWorkerCount() == 2, "H1 abandonedCount == 2");

    LifecycleJob other;
    other.adapterId = "other";
    other.op = LifecycleOp::Connect;
    other.generation = 0;
    expect(exec.enqueue(other), "H1 sibling Connect enqueue");
    expect(waitFor([&]() { return otherEntered.load() >= 1; },
                   std::chrono::milliseconds(2000)),
           "H1 sibling Connect runs while hangs remain blocked");

    expect(!exec.canAbandonStaleInFlight("hang", gen2),
           "H1 per-adapter budget blocks a third abandon");

    gates[2]->open();
    expect(waitFor(
               [&]() {
                 return !exec.hasInFlight("hang", LifecycleOp::RecoveryConnect, gen2);
               },
               std::chrono::milliseconds(2000)),
           "H1 Gen2 completed");

    gates[0]->open();
    gates[1]->open();
    expect(waitFor([&]() { return exec.abandonedWorkerCount() == 0; },
                   std::chrono::milliseconds(2000)),
           "H1 abandoned workers exit and decrement accounting");
    expect(exec.liveWorkerCount() == 4, "H1 live workers still 4 after stale exit");
    expect(stub0->disconnects() == 0 && stub1->disconnects() == 0,
           "H1 parked Teardown did not disconnect while mutex held");
    io->unlock();
    expect(waitFor(
               [&]() {
                 return stub0->disconnects() >= 1 && stub1->disconnects() >= 1;
               },
               std::chrono::milliseconds(2000)),
           "H1 parked Teardowns complete after mutex release");

    exec.stop();
  }

  // ---------------------------------------------------------------------
  // H4 — abandonment budgets never exceeded; live workers stay at 4.
  // ---------------------------------------------------------------------
  {
    LifecycleExecutor exec;
    std::mutex mu;
    std::vector<std::shared_ptr<HangGate>> gates;
    std::atomic<int> entered{0};
    exec.setHandler([&](const LifecycleJob &job) {
      (void)job;
      ++entered;
      std::shared_ptr<HangGate> gate;
      {
        std::lock_guard<std::mutex> lock(mu);
        if (!gates.empty())
        {
          gate = gates.back();
        }
      }
      if (gate)
      {
        gate->wait();
      }
    });
    exec.start(4);
    expect(exec.liveWorkerCount() == 4, "H4 live workers start at 4");

    for (int i = 0; i < 6; ++i)
    {
      const std::string id = "a" + std::to_string(i);
      auto gate = std::make_shared<HangGate>();
      {
        std::lock_guard<std::mutex> lock(mu);
        gates.push_back(gate);
      }
      LifecycleJob job;
      job.adapterId = id;
      job.op = LifecycleOp::RecoveryConnect;
      job.generation = 0;
      expect(exec.enqueue(job), "H4 enqueue " + id);
      expect(waitFor([&]() { return exec.hasInFlight(id, LifecycleOp::RecoveryConnect, 0); },
                     std::chrono::milliseconds(2000)),
             "H4 " + id + " in-flight");
      (void)exec.bumpGeneration(id);
      const bool abandoned = exec.abandonStaleInFlight(id, 0);
      if (i < static_cast<int>(kMaxAbandonedLifecycleWorkers))
      {
        expect(abandoned, "H4 abandon succeeds within global budget for " + id);
      }
      expect(exec.abandonedWorkerCount() <= kMaxAbandonedLifecycleWorkers,
             "H4 abandonedCount never exceeds 8");
      expect(exec.abandonedWorkerCount(id) <= kMaxAbandonedPerAdapter,
             "H4 per-adapter abandoned never exceeds 2");
      expect(exec.liveWorkerCount() == 4, "H4 live workers remain 4");
    }

    // Same adapter twice, then a third must fail.
    {
      const std::string id = "same";
      for (int n = 0; n < 3; ++n)
      {
        auto gate = std::make_shared<HangGate>();
        {
          std::lock_guard<std::mutex> lock(mu);
          gates.push_back(gate);
        }
        const std::uint64_t gen = exec.generation(id);
        LifecycleJob job;
        job.adapterId = id;
        job.op = LifecycleOp::RecoveryConnect;
        job.generation = gen;
        expect(exec.enqueue(job), "H4 enqueue same gen " + std::to_string(gen));
        expect(waitFor(
                   [&]() {
                     return exec.hasInFlight(id, LifecycleOp::RecoveryConnect, gen);
                   },
                   std::chrono::milliseconds(2000)),
               "H4 same in-flight gen " + std::to_string(gen));
        (void)exec.bumpGeneration(id);
        const bool abandoned = exec.abandonStaleInFlight(id, gen);
        if (n < static_cast<int>(kMaxAbandonedPerAdapter)
            && exec.abandonedWorkerCount() < kMaxAbandonedLifecycleWorkers)
        {
          expect(abandoned, "H4 same-adapter abandon " + std::to_string(n));
        }
        if (n >= static_cast<int>(kMaxAbandonedPerAdapter))
        {
          expect(!abandoned, "H4 third same-adapter abandon refused");
        }
        expect(exec.abandonedWorkerCount(id) <= kMaxAbandonedPerAdapter,
               "H4 same-adapter count capped at 2");
        expect(exec.liveWorkerCount() == 4, "H4 live workers remain 4 (same)");
      }
    }

    expect(exec.abandonedWorkerCount() <= kMaxAbandonedLifecycleWorkers,
           "H4 final abandonedCount <= 8");
    expect(exec.liveWorkerCount() == 4, "H4 no extra joinable workers");

    for (auto &gate : gates)
    {
      gate->open();
    }
    exec.stop(std::chrono::milliseconds(2000));
  }

  // ---------------------------------------------------------------------
  // H4-global — 9 distinct adapters with hung RecoveryConnect.
  // First 8 abandons succeed; 9th fails; abandonedCount==8; liveWorkerCount==4.
  // ---------------------------------------------------------------------
  {
    LifecycleExecutor exec;
    std::mutex mu;
    std::vector<std::shared_ptr<HangGate>> gates;
    std::unordered_map<std::string, std::shared_ptr<HangGate>> byId;
    exec.setHandler([&](const LifecycleJob &job) {
      std::shared_ptr<HangGate> gate;
      {
        std::lock_guard<std::mutex> lock(mu);
        auto it = byId.find(job.adapterId);
        if (it != byId.end())
        {
          gate = it->second;
        }
      }
      if (gate)
      {
        gate->wait();
      }
    });
    exec.start(4);
    expect(exec.liveWorkerCount() == 4, "H4g live workers start at 4");

    int abandonedOk = 0;
    for (int i = 0; i < 9; ++i)
    {
      const std::string id = "g" + std::to_string(i);
      auto gate = std::make_shared<HangGate>();
      {
        std::lock_guard<std::mutex> lock(mu);
        byId[id] = gate;
        gates.push_back(gate);
      }
      LifecycleJob job;
      job.adapterId = id;
      job.op = LifecycleOp::RecoveryConnect;
      job.generation = 0;
      expect(exec.enqueue(job), "H4g enqueue " + id);
      expect(waitFor(
                 [&]() {
                   return exec.hasInFlight(id, LifecycleOp::RecoveryConnect, 0);
                 },
                 std::chrono::milliseconds(2000)),
             "H4g " + id + " in-flight");
      (void)exec.bumpGeneration(id);
      const bool abandoned = exec.abandonStaleInFlight(id, 0);
      if (i < 8)
      {
        expect(abandoned, "H4g abandon succeeds for " + id);
        if (abandoned)
        {
          ++abandonedOk;
        }
      }
      else
      {
        expect(!abandoned, "H4g 9th abandon must fail");
      }
      expect(exec.liveWorkerCount() == 4, "H4g live workers remain 4 after " + id);
    }
    expect(abandonedOk == 8, "H4g first 8 abandons succeeded");
    expect(exec.abandonedWorkerCount() == 8, "H4g abandonedCount == 8");
    expect(exec.liveWorkerCount() == 4, "H4g liveWorkerCount == 4");

    for (auto &gate : gates)
    {
      gate->open();
    }
    exec.stop(std::chrono::milliseconds(2000));
  }

  // ---------------------------------------------------------------------
  // H5 — stale worker return does not clear a newer generation.
  // ---------------------------------------------------------------------
  {
    LifecycleExecutor exec;
    auto g0 = std::make_shared<HangGate>();
    auto g1 = std::make_shared<HangGate>();
    std::atomic<int> entered0{0};
    std::atomic<int> entered1{0};
    std::atomic<int> completed1{0};
    exec.setHandler([&](const LifecycleJob &job) {
      if (job.generation == 0)
      {
        ++entered0;
        g0->wait();
      }
      else
      {
        ++entered1;
        g1->wait();
        ++completed1;
      }
    });
    exec.start(4);
    LifecycleJob j0;
    j0.adapterId = "stale";
    j0.op = LifecycleOp::RecoveryConnect;
    j0.generation = 0;
    expect(exec.enqueue(j0), "H5 enqueue Gen0");
    expect(waitFor([&]() { return entered0.load() >= 1; },
                   std::chrono::milliseconds(2000)),
           "H5 Gen0 entered");
    (void)exec.bumpGeneration("stale");
    expect(exec.abandonStaleInFlight("stale", 0), "H5 abandon Gen0");
    LifecycleJob j1;
    j1.adapterId = "stale";
    j1.op = LifecycleOp::RecoveryConnect;
    j1.generation = 1;
    expect(exec.enqueue(j1), "H5 enqueue Gen1");
    expect(waitFor([&]() { return entered1.load() >= 1; },
                   std::chrono::milliseconds(2000)),
           "H5 Gen1 entered");
    g1->open();
    expect(waitFor([&]() { return completed1.load() >= 1; },
                   std::chrono::milliseconds(2000)),
           "H5 Gen1 completed (CONNECTED stand-in)");
    expect(!exec.hasInFlight("stale", LifecycleOp::RecoveryConnect),
           "H5 current generation idle after Gen1");
    expect(exec.hasInFlight("stale", LifecycleOp::RecoveryConnect, 0)
               || exec.abandonedWorkerCount() == 1,
           "H5 Gen0 still outstanding as abandoned worker");
    g0->open();
    expect(waitFor([&]() { return exec.abandonedWorkerCount() == 0; },
                   std::chrono::milliseconds(2000)),
           "H5 abandoned count decreases when Gen0 exits");
    expect(!exec.hasInFlight("stale", LifecycleOp::RecoveryConnect, 1),
           "H5 stale completion did not resurrect Gen1 in-flight");
    expect(exec.liveWorkerCount() == 4, "H5 live workers still 4");
    exec.stop();
  }

  // ---------------------------------------------------------------------
  // H6 — shutdown remains bounded while stale runtime workers are blocked.
  // ---------------------------------------------------------------------
  {
    LifecycleExecutor exec;
    auto gate = std::make_shared<HangGate>();
    exec.setHandler([&](const LifecycleJob &) { gate->wait(); });
    exec.start(4);
    LifecycleJob job;
    job.adapterId = "shut";
    job.op = LifecycleOp::RecoveryConnect;
    job.generation = 0;
    expect(exec.enqueue(job), "H6 enqueue hung RecoveryConnect");
    expect(waitFor(
               [&]() {
                 return exec.hasInFlight("shut", LifecycleOp::RecoveryConnect, 0);
               },
               std::chrono::milliseconds(2000)),
           "H6 in-flight");
    (void)exec.bumpGeneration("shut");
    expect(exec.abandonStaleInFlight("shut", 0), "H6 abandon stale worker");
    const auto t0 = std::chrono::steady_clock::now();
    exec.stop(std::chrono::milliseconds(200));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    expect(ms < 2000, "H6 stop did not join hung protocol I/O");
    expect(exec.hasAbandonedWorkers(),
           "H6 P2 shutdown cohort records abandoned live workers");
    gate->open();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  if (failures == 0)
  {
    std::cout << "icp_lifecycle_executor_reclaim_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_lifecycle_executor_reclaim_test: " << failures
            << " failure(s)" << std::endl;
  return 1;
}
