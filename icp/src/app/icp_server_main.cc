#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/icp/app/HttpApiServer.hh>

#include <cstdlib>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace
{

std::string envOr(const char *name, const std::string &fallback)
{
  const char *value = std::getenv(name);
  if (value == nullptr || value[0] == '\0')
  {
    return fallback;
  }
  return value;
}

}  // namespace

int main(int argc, char **argv)
{
  std::string host = envOr("ICP_BIND_HOST", "127.0.0.1");
  int port = 8080;
  std::string configPath = envOr("ICP_CONFIG_PATH", "icp-config.json");
#ifdef VF_ICP_GUI_ROOT
  std::string guiRoot = envOr("ICP_GUI_ROOT", VF_ICP_GUI_ROOT);
#else
  std::string guiRoot = envOr("ICP_GUI_ROOT", "icp/gui");
#endif

  for (int i = 1; i < argc; ++i)
  {
    const std::string arg = argv[i];
    if ((arg == "--host" || arg == "-h") && i + 1 < argc)
    {
      host = argv[++i];
    }
    else if ((arg == "--port" || arg == "-p") && i + 1 < argc)
    {
      port = std::atoi(argv[++i]);
    }
    else if ((arg == "--config" || arg == "-c") && i + 1 < argc)
    {
      configPath = argv[++i];
    }
    else if ((arg == "--gui-root" || arg == "-g") && i + 1 < argc)
    {
      guiRoot = argv[++i];
    }
    else if (arg == "--help")
    {
      std::cout
          << "icp_server — standalone Industrial Connectivity Platform\n"
          << "  --host <addr>       bind address (default 127.0.0.1)\n"
          << "  --port <port>       bind port (default 8080)\n"
          << "  --config <path>     ICP-1B JSON configuration path\n"
          << "  --gui-root <path>   static GUI root directory\n"
          << "No MES. No CIC. Hilscher remains optional.\n";
      return 0;
    }
  }

  virtual_factory::icp::ApplicationService service(configPath);
  service.start();
  // Best-effort load of existing configuration (empty/missing is OK).
  (void)service.loadConfiguration();

  virtual_factory::icp::HttpApiServer api(service, guiRoot, host, port);
  if (!api.start())
  {
    std::cerr << "Failed to start ICP Application API on " << host << ":" << port
              << std::endl;
    service.stop();
    return 1;
  }

  std::cout << "ICP standalone server listening on http://" << host << ":" << port
            << std::endl;
  std::cout << "GUI root: " << guiRoot << std::endl;
  std::cout << "Config:   " << configPath << std::endl;
  std::cout << "MES dependency: no" << std::endl;
  std::cout << "CIC dependency: no" << std::endl;
  std::cout << "Press Ctrl+C to stop." << std::endl;

  // Block on the server thread via stop-on-signal would be nicer; for this
  // milestone join until process exit by parking on running flag.
  while (api.running())
  {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  api.stop();
  service.stop();
  return 0;
}
