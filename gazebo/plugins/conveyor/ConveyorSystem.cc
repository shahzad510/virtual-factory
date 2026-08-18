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
void ConveyorSystem::Start()
{
  this->running_ = true;

if (this->speed_ <= 0.0)
  {
    this->speed_ = 0.5;
  }

  gzmsg << "[VirtualFactory] Conveyor START command accepted"
        << " | equipment=" << this->name_
        << std::endl;
}

//////////////////////////////////////////////////
void ConveyorSystem::Stop()
{
  this->running_ = false;
  this->speed_ = 0.0;

  gzmsg << "[VirtualFactory] Conveyor STOP command accepted"
        << " | equipment=" << this->name_
        << std::endl;
}


//////////////////////////////////////////////////
void ConveyorSystem::SetSpeed(double _speed)
{
  if (_speed < 0.0)
  {
    gzmsg << "[VirtualFactory] Invalid conveyor speed: "
          << _speed << " m/s"
          << std::endl;

    return;
  }

  this->speed_ = _speed;

  gzmsg << "[VirtualFactory] Conveyor speed set to "
        << this->speed_ << " m/s"
        << std::endl;
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
  // Do nothing while the simulation is paused.
  if (_info.paused)
    return;

  // Count this simulation update.
  ++this->updateCount_;

  // Temporary development test.
  // Start the conveyor after 1000 updates.
  if (this->updateCount_ == 1000)
  {
    this->Start();
  }

  // Temporary development test.
  // Stop the conveyor after 5000 updates.
  if (this->updateCount_ == 5000)
  {
    this->Stop();
  }

  // Print diagnostic information every 100 updates.
  if (this->updateCount_ % 100 == 0)
  {
    gzmsg << "[VirtualFactory] ConveyorSystem heartbeat"
          << " | equipment=" << this->name_
          << " | updates=" << this->updateCount_
          << " | running=" << std::boolalpha << this->running_
          << " | speed=" << this->speed_ << " m/s"
          << std::endl;
  }
}


}  // namespace virtual_factory


// Register the system with Gazebo.
GZ_ADD_PLUGIN(
    virtual_factory::ConveyorSystem,
    gz::sim::System,
    virtual_factory::ConveyorSystem::ISystemConfigure,
    virtual_factory::ConveyorSystem::ISystemPreUpdate)
