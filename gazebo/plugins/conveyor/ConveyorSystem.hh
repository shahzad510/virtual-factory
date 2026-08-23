#ifndef VIRTUAL_FACTORY_CONVEYOR_SYSTEM_HH_
#define VIRTUAL_FACTORY_CONVEYOR_SYSTEM_HH_

#include <cstdint>
#include <memory>

#include <gz/sim/System.hh>

namespace virtual_factory
{

class Conveyor;

class ConveyorSystem :
    public gz::sim::System,
    public gz::sim::ISystemConfigure,
    public gz::sim::ISystemPreUpdate
{
public:

  ConveyorSystem();

  ~ConveyorSystem() override;

  // Start through the generic Equipment contract.
  void Start();

  // Stop through the generic Equipment contract.
  void Stop();

  // Conveyor-specific speed command (not part of generic Equipment).
  void SetSpeed(double _speed);


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

  // Gazebo entity representing the moving belt link.
  gz::sim::Entity beltEntity_{gz::sim::kNullEntity};

  // Gazebo entity representing PRODUCT-001.
  gz::sim::Entity productEntity_{gz::sim::kNullEntity};

  // Gazebo-independent conveyor equipment (SoT Phase 5).
  std::unique_ptr<Conveyor> equipment_;

  // Number of simulation updates processed by PreUpdate().
  std::uint64_t updateCount_{0};
};

}  // namespace virtual_factory

#endif
