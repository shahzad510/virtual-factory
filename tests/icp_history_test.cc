#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/icp/app/HttpApiServer.hh>
#include <virtual_factory/icp/history/SqliteHistoryRepository.hh>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

#include <nlohmann/json.hpp>

#define CPPHTTPLIB_THREAD_POOL_COUNT 2
#include <httplib.h>

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

std::string tempPath(const char *suffix)
{
  std::ostringstream stream;
  stream << "/tmp/icp-history-" << ::getpid() << "-" << suffix;
  return stream.str();
}

}  // namespace

int main()
{
  using json = nlohmann::json;
  using virtual_factory::icp::ApplicationService;
  using virtual_factory::icp::HistoryQuery;
  using virtual_factory::icp::HttpApiServer;
  using virtual_factory::icp::NullHistoryRepository;
  using virtual_factory::icp::SqliteHistoryRepository;

  const std::string configPath = tempPath("config.json");
  const std::string historyPath = tempPath("history.sqlite");
  ::unlink(configPath.c_str());
  ::unlink(historyPath.c_str());
  ::unlink((historyPath + "-wal").c_str());
  ::unlink((historyPath + "-shm").c_str());

  // --- A: events/config survive restart ---
  {
    ApplicationService service(configPath, historyPath);
    service.start();
    expect(service.historyStatus().available, "historian available after start");

    virtual_factory::icp::AdapterConfigRecord mock;
    mock.adapterId = "mock-hist";
    mock.protocol = "mock";
    mock.enabled = true;
    virtual_factory::icp::EquipmentMappingRecord eq;
    eq.equipmentId = "Motor-H1";
    eq.type = "motor";
    eq.capabilities = {"start", "stop"};
    mock.equipment.push_back(eq);
    expect(service.upsertAdapterConfig(mock).ok, "upsert mock");
    expect(service.saveConfiguration().ok, "save config");
    expect(service.connectAdapter("mock-hist").ok, "connect mock");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    (void)service.executeEquipmentCommand("Motor-H1", "start", 0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    service.disconnectAdapter("mock-hist");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    service.stop();
  }

  {
    ApplicationService reloaded(configPath, historyPath);
    reloaded.start();
    // Connected state must NOT be restored from history.
    bool anyConnected = false;
    for (const auto &adapter : reloaded.adapters())
    {
      if (adapter.connectionState == "CONNECTED")
      {
        anyConnected = true;
      }
    }
    expect(!anyConnected, "restart does not restore CONNECTED from history");

    HistoryQuery eventsQuery;
    eventsQuery.kind = "events";
    eventsQuery.limit = 500;
    auto events = reloaded.queryHistory(eventsQuery);
    expect(events.status.available, "history available after restart");
    expect(!events.events.empty(), "persisted events survive restart");

    bool sawRuntime = false;
    bool sawConfig = false;
    for (const auto &ev : events.events)
    {
      if (ev.category == "runtime")
      {
        sawRuntime = true;
      }
      if (ev.category == "configuration")
      {
        sawConfig = true;
      }
    }
    expect(sawRuntime || sawConfig, "runtime/config events present in history");

    HistoryQuery intervals;
    intervals.kind = "communication_intervals";
    intervals.adapterId = "mock-hist";
    intervals.limit = 100;
    auto comm = reloaded.queryHistory(intervals);
    expect(!comm.communicationIntervals.empty(), "communication intervals persisted");

    HistoryQuery cmds;
    cmds.kind = "command_audit";
    cmds.equipmentId = "Motor-H1";
    auto commandHistory = reloaded.queryHistory(cmds);
    expect(!commandHistory.commandAudits.empty(), "command audit persisted");

    HistoryQuery configs;
    configs.kind = "config_revisions";
    auto configHistory = reloaded.queryHistory(configs);
    expect(!configHistory.configRevisions.empty(), "config revisions persisted");

    // Generate additional events after restart; old + new coexist chronologically.
    reloaded.recordEvent("info", "runtime", "post-restart marker event");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    reloaded.stop();
  }

  {
    ApplicationService again(configPath, historyPath);
    again.start();
    HistoryQuery eventsQuery;
    eventsQuery.kind = "events";
    eventsQuery.limit = 1000;
    auto events = again.queryHistory(eventsQuery);
    bool sawMarker = false;
    std::int64_t lastTs = -1;
    bool chronological = true;
    for (const auto &ev : events.events)
    {
      if (ev.message.find("post-restart marker") != std::string::npos)
      {
        sawMarker = true;
      }
      if (lastTs >= 0 && ev.tsUtcMs < lastTs)
      {
        chronological = false;
      }
      lastTs = ev.tsUtcMs;
    }
    expect(sawMarker, "post-restart marker coexists with older history");
    expect(chronological, "history events returned in chronological order");
    again.stop();
  }

  // --- B: historian unavailable must not block ICP ---
  {
    const std::string badDir = tempPath("not-a-dir-as-file");
    ::unlink(badDir.c_str());
    {
      std::ofstream file(badDir.c_str());
      file << "not a sqlite database";
    }
    // Point historian at a path that cannot be opened as a DB directory child.
    const std::string unwritable = badDir + "/nested/history.sqlite";
    ApplicationService service(configPath, unwritable);
    service.start();
    expect(service.running(), "ICP starts when historian path is unwritable");
    const auto st = service.historyStatus();
    expect(!st.available || st.degraded, "historian reports unavailable/degraded");
    virtual_factory::icp::AdapterConfigRecord mock;
    mock.adapterId = "mock-degraded-hist";
    mock.protocol = "mock";
    mock.enabled = true;
    expect(service.upsertAdapterConfig(mock).ok,
           "protocol/config still works when historian degraded");
    service.stop();
  }

  // --- C: HTTP /api/v1/history ---
  {
    ApplicationService httpService(configPath, historyPath);
    httpService.start();
    (void)httpService.loadConfiguration();
    const int port = 19080 + (::getpid() % 1000);
    HttpApiServer api(httpService, "icp/gui", "127.0.0.1", port);
    expect(api.start(), "http api starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(2, 0);
    auto res = client.Get("/api/v1/history?kind=events&limit=50");
    expect(res != nullptr, "history GET returns");
    if (res != nullptr)
    {
      expect(res->status == 200, "history GET status 200");
      auto body = json::parse(res->body, nullptr, false);
      expect(!body.is_discarded(), "history JSON parses");
      expect(body.contains("historian"), "historian status present");
      expect(body.contains("items"), "items present");
      expect(body["kind"] == "events", "kind=events");
    }
    auto health = client.Get("/api/v1/health");
    expect(health != nullptr && health->status == 200, "health still OK");
    api.stop();
    httpService.stop();
  }

  // --- D: direct repository open failure uses NullHistoryRepository semantics ---
  {
    SqliteHistoryRepository bad("/tmp");
    expect(!bad.openOk(), "opening directory as sqlite fails");
    NullHistoryRepository nullRepo("/tmp", "unavailable");
    expect(!nullRepo.status().available, "null historian unavailable");
    expect(nullRepo.status().degraded, "null historian degraded");
  }

  ::unlink(configPath.c_str());
  ::unlink(historyPath.c_str());
  ::unlink((historyPath + "-wal").c_str());
  ::unlink((historyPath + "-shm").c_str());

  if (failures != 0)
  {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "icp_history_test: OK\n";
  return 0;
}
