#include "ConveyorSystem.hh"

#include <gz/plugin/Register.hh>

#include <gz/sim/Model.hh>

namespace virtual_factory
{

//////////////////////////////////////////////////
ConveyorSystem::ConveyorSystem()
{
}

//////////////////////////////////////////////////
ConveyorSystem::~ConveyorSystem()
{
}

//////////////////////////////////////////////////
void ConveyorSystem::Configure(
    const gz::sim::Entity &_entity,
    const std::shared_ptr<const sdf::Element> & /*_sdf*/,
    gz::sim::EntityComponentManager &_ecm,
    gz::sim::EventManager & /*_eventMgr*/)
{
  this->entity_ = _entity;

  // The plugin is attached to a model,
  // so interpret the entity as a Gazebo model.
  gz::sim::Model model(this->entity_);

  this->name_ = model.Name(_ecm);

  gzmsg << "[VirtualFactory] ConveyorSystem configured for: "
        << this->name_ << std::endl;

  gzmsg << "[VirtualFactory] Initial state: STOPPED"
        << std::endl;

  gzmsg << "[VirtualFactory] Initial speed: "
        << this->speed_ << " m/s"
        << std::endl;
}

//////////////////////////////////////////////////
void ConveyorSystem::PreUpdate(
    const gz::sim::UpdateInfo &_info,
    gz::sim::EntityComponentManager & /*_ecm*/)
{
  // Phase 1.3B-1:
  // We are only proving that the update callback works.

  if (_info.paused)
    return;

  // Nothing physically changes yet.
}

}  // namespace virtual_factory


// Register the system with Gazebo.
GZ_ADD_PLUGIN(
    virtual_factory::ConveyorSystem,
    gz::sim::System,
    virtual_factory::ConveyorSystem::ISystemConfigure,
    virtual_factory::ConveyorSystem::ISystemPreUpdate)
