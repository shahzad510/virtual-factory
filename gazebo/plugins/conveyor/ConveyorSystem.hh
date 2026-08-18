#ifndef VIRTUAL_FACTORY_CONVEYOR_SYSTEM_HH_
#define VIRTUAL_FACTORY_CONVEYOR_SYSTEM_HH_

#include <memory>
#include <string>
#include <cstdint>
#include <gz/sim/System.hh>

namespace virtual_factory
{

class ConveyorSystem :
    public gz::sim::System,
    public gz::sim::ISystemConfigure,
    public gz::sim::ISystemPreUpdate
{
public:

  ConveyorSystem();

  ~ConveyorSystem() override;

  // Called once when the plugin is configured.
  void Configure(
      const gz::sim::Entity &_entity,
      const std::shared_ptr<const sdf::Element> &_sdf,
      gz::sim::EntityComponentManager &_ecm,
      gz::sim::EventManager &_eventMgr) override;

  // Called before each simulation update.
  void PreUpdate(
      const gz::sim::UpdateInfo &_info,
      gz::sim::EntityComponentManager &_ecm) override;

private:

  // Gazebo entity representing CV-001.
  gz::sim::Entity entity_{gz::sim::kNullEntity};

  // Equipment identity.
  std::string name_;

  // Initial control state.
  bool running_{false};

  // Conveyor speed in metres per second.
  double speed_{0.0};

  // Fault state.
  bool fault_{false};

  // Number of simulation updates processed by PreUpdate().
  std::uint64_t updateCount_{0};
};

}  // namespace virtual_factory

#endif
