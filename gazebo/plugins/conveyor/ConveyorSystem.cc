#include "ConveyorSystem.hh"

#include <chrono>
#include <sstream>

#include <gz/plugin/Register.hh>

#include <gz/sim/Model.hh>

#include <gz/sim/Link.hh>

#include <gz/sim/components/Name.hh>

#include <gz/sim/components/Pose.hh>

#include <virtual_factory/equipment/Conveyor.hh>

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
  if (!this->equipment_)
  {
    return;
  }

  this->equipment_->start();

  gzmsg << "[VirtualFactory] Conveyor START command accepted"
        << " | equipment=" << this->equipment_->id()
        << std::endl;
}

//////////////////////////////////////////////////
void ConveyorSystem::Stop()
{
  if (!this->equipment_)
  {
    return;
  }

  this->equipment_->stop();

  gzmsg << "[VirtualFactory] Conveyor STOP command accepted"
        << " | equipment=" << this->equipment_->id()
        << std::endl;
}


//////////////////////////////////////////////////
void ConveyorSystem::SetSpeed(double _speed)
{
  if (!this->equipment_)
  {
    return;
  }

  if (!this->equipment_->setSpeed(_speed))
  {
    gzmsg << "[VirtualFactory] Invalid conveyor speed: "
          << _speed << " m/s"
          << std::endl;
    return;
  }

  gzmsg << "[VirtualFactory] Conveyor speed set to "
        << this->equipment_->speed() << " m/s"
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

  const std::string name = model.Name(_ecm);

  // Find the belt link inside CV-001.
  this->beltEntity_ = model.LinkByName(_ecm, "belt");

  if (this->beltEntity_ == gz::sim::kNullEntity)
  {
    gzerr << "[VirtualFactory] ERROR: Could not find belt link"
          << std::endl;
    return;
  }

  this->equipment_ = std::make_unique<Conveyor>(name);

  gzmsg << "[VirtualFactory] Belt link found"
        << " | entity=" << this->beltEntity_
        << std::endl;
  gzmsg << "[VirtualFactory] ConveyorSystem configured for: "
        << this->equipment_->id() << std::endl;

  gzmsg << "[VirtualFactory] Initial state: STOPPED"
        << std::endl;

  gzmsg << "[VirtualFactory] Initial speed: "
        << this->equipment_->speed() << " m/s"
        << std::endl;
}

//////////////////////////////////////////////////

void ConveyorSystem::PreUpdate(
    const gz::sim::UpdateInfo &_info,
    gz::sim::EntityComponentManager &_ecm)
{
  // Do nothing while the simulation is paused.
  if (_info.paused)
    return;

  if (!this->equipment_)
  {
    return;
  }

  // Count this simulation update.
  ++this->updateCount_;

  // ------------------------------------------------------------
  // Find PRODUCT-001 once it becomes available in the ECM
  // ------------------------------------------------------------

  if (this->productEntity_ == gz::sim::kNullEntity)
  {
    _ecm.Each<gz::sim::components::Name>(
        [&](const gz::sim::Entity &_entity,
            const gz::sim::components::Name *_name)
        {
          if (_name && _name->Data() == "PRODUCT-001")
          {
            this->productEntity_ = _entity;

            gzmsg << "[VirtualFactory] Product found"
                  << " | entity=" << this->productEntity_
                  << std::endl;

            return false;
          }

          return true;
        });
  }

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

  // ------------------------------------------------------------
  // Move PRODUCT-001 with the conveyor
  // ------------------------------------------------------------
  //
  // _info.dt is std::chrono::steady_clock::duration.
  // On this platform that duration's tick is one nanosecond.
  // _info.dt.count() is therefore nanoseconds, not seconds.
  // Convert to seconds before applying speed (m/s).

  const double dt =
      std::chrono::duration<double>(_info.dt).count();

  if (this->equipment_->running() &&
      this->productEntity_ != gz::sim::kNullEntity)
  {
    auto *pose =
        _ecm.Component<gz::sim::components::Pose>(
            this->productEntity_);

    if (pose)
    {
      auto currentPose = pose->Data();

      currentPose.Pos().X() += this->equipment_->speed() * dt;

      _ecm.SetComponentData<gz::sim::components::Pose>(
          this->productEntity_,
          currentPose);
    }
  }

  // ------------------------------------------------------------
  // Control the physical belt
  // ------------------------------------------------------------

  gz::sim::Link belt(this->beltEntity_);

  if (this->equipment_->running())
  {
    // Move the belt in the +X direction.
    belt.SetLinearVelocity(
        _ecm,
        gz::math::Vector3d(this->equipment_->speed(), 0, 0));
  }
  else
  {
    // Keep the belt stationary.
    belt.SetLinearVelocity(
        _ecm,
        gz::math::Vector3d(0, 0, 0));
  }

  // ------------------------------------------------------------
  // Diagnostic heartbeat
  // ------------------------------------------------------------

  if (this->updateCount_ % 100 == 0)
  {
    double productX = 0.0;
    bool haveProductPose = false;

    if (this->productEntity_ != gz::sim::kNullEntity)
    {
      auto *pose =
          _ecm.Component<gz::sim::components::Pose>(
              this->productEntity_);
      if (pose)
      {
        productX = pose->Data().Pos().X();
        haveProductPose = true;
      }
    }

    const EquipmentStatus status = this->equipment_->status();

    std::ostringstream line;
    line << "[VirtualFactory] ConveyorSystem heartbeat"
         << " | equipment=" << status.id
         << " | type=" << status.type
         << " | updates=" << this->updateCount_
         << " | running=" << std::boolalpha << this->equipment_->running()
         << " | fault=" << std::boolalpha << status.fault
         << " | dt=" << dt << " s";

    for (const auto &point : this->equipment_->telemetry())
    {
      line << " | " << point.name << "=" << point.value << " " << point.unit;
    }
    if (haveProductPose)
    {
      line << " | product_x=" << productX << " m";
    }
    gzmsg << line.str() << std::endl;
  }
}


}  // namespace virtual_factory


// Register the system with Gazebo.
GZ_ADD_PLUGIN(
    virtual_factory::ConveyorSystem,
    gz::sim::System,
    virtual_factory::ConveyorSystem::ISystemConfigure,
    virtual_factory::ConveyorSystem::ISystemPreUpdate)
