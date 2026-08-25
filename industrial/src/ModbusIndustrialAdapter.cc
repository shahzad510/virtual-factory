#include <virtual_factory/industrial/ModbusIndustrialAdapter.hh>

#include <virtual_factory/equipment/GenericEquipment.hh>

#include "modbus_tcp_session.hh"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace virtual_factory
{

namespace
{

bool isSetCommand(const std::string &command)
{
  return command.size() > 4 && command.compare(0, 4, "set_") == 0;
}

}  // namespace

struct ModbusIndustrialAdapter::ClientHandle
{
  internal::ModbusTcpSession session;
};

class ModbusIndustrialAdapter::BoundEquipment : public Equipment
{
public:
  BoundEquipment(
      ModbusIndustrialAdapter *adapter, const ModbusEquipmentMapping *mapping)
      : adapter_(adapter), mapping_(mapping), inner_(mapping->id, mapping->type)
  {
    for (const auto &capability : mapping->capabilities)
    {
      this->inner_.addCapability(capability);
    }
    for (const auto &command : mapping->commands)
    {
      this->inner_.addCapability(command.command);
    }
  }

  std::string id() const override
  {
    return this->inner_.id();
  }

  std::string type() const override
  {
    return this->inner_.type();
  }

  OperationalState operationalState() const override
  {
    return this->inner_.operationalState();
  }

  bool fault() const override
  {
    return this->inner_.fault();
  }

  std::vector<std::string> capabilities() const override
  {
    return this->inner_.capabilities();
  }

  std::vector<std::string> commands() const override
  {
    return this->inner_.commands();
  }

  CommandResult execute(
      const std::string &command, double parameter) override
  {
    if (this->adapter_->connectionState() != ConnectionState::Connected)
    {
      if (this->adapter_->connectionState() == ConnectionState::Faulted)
      {
        return {false, "communication fault"};
      }
      return {false, "adapter not connected"};
    }

    const ModbusCommandMapping *mapped = nullptr;
    for (const auto &entry : this->mapping_->commands)
    {
      if (entry.command == command)
      {
        mapped = &entry;
        break;
      }
    }

    if (mapped == nullptr)
    {
      return {false, "unknown command"};
    }

    bool ok = false;
    if (isSetCommand(command))
    {
      ok = this->adapter_->writeDouble(mapped->target, parameter);
    }
    else
    {
      ok = this->adapter_->writeBoolean(mapped->target, true);
    }

    if (!ok)
    {
      return {false, "modbus write failed"};
    }

    return {true, "written"};
  }

  std::vector<TelemetryPoint> telemetry() const override
  {
    return this->inner_.telemetry();
  }

  EquipmentStatus status() const override
  {
    return this->inner_.status();
  }

  bool refreshFromServer()
  {
    for (const auto &point : this->mapping_->telemetry)
    {
      double value = 0.0;
      if (!this->adapter_->readDouble(point.source, &value))
      {
        return false;
      }
      this->inner_.setTelemetry(point.name, value, point.unit);
    }

    if (this->mapping_->stateCoil.mapped)
    {
      bool running = false;
      if (!this->adapter_->readBoolean(this->mapping_->stateCoil, &running))
      {
        return false;
      }
      this->inner_.setOperationalState(
          running ? OperationalState::Running : OperationalState::Stopped);
    }

    if (this->mapping_->faultCoil.mapped)
    {
      bool fault = false;
      if (!this->adapter_->readBoolean(this->mapping_->faultCoil, &fault))
      {
        return false;
      }
      this->inner_.setFault(fault);
    }

    return true;
  }

private:
  ModbusIndustrialAdapter *adapter_;
  const ModbusEquipmentMapping *mapping_;
  GenericEquipment inner_;
};

ModbusIndustrialAdapter::ModbusIndustrialAdapter(
    std::string id, ModbusAdapterConfig config)
    : id_(std::move(id)),
      config_(std::move(config)),
      client_(std::make_unique<ClientHandle>())
{
}

ModbusIndustrialAdapter::~ModbusIndustrialAdapter()
{
  this->disconnect();
}

std::string ModbusIndustrialAdapter::id() const
{
  return this->id_;
}

std::string ModbusIndustrialAdapter::protocol() const
{
  return "modbus";
}

ConnectionState ModbusIndustrialAdapter::connectionState() const
{
  return this->connection_state_;
}

std::string ModbusIndustrialAdapter::lastError() const
{
  return this->last_error_;
}

bool ModbusIndustrialAdapter::connect()
{
  if (this->connection_state_ == ConnectionState::Connected)
  {
    return true;
  }

  if (this->config_.host.empty() || this->config_.port == 0)
  {
    this->enterFault("missing Modbus TCP host or port");
    return false;
  }

  this->client_->session.close();
  if (!this->client_->session.connect(
          this->config_.host, this->config_.port, this->config_.timeoutMs))
  {
    this->enterFault(
        std::string("Modbus TCP connect failed: ") +
        this->client_->session.lastError());
    return false;
  }

  this->bindEquipment();
  this->connection_state_ = ConnectionState::Connected;
  this->last_error_.clear();
  return true;
}

void ModbusIndustrialAdapter::disconnect()
{
  this->bound_.clear();
  if (this->client_)
  {
    this->client_->session.close();
  }
  this->connection_state_ = ConnectionState::Disconnected;
  this->last_error_.clear();
}

std::vector<Equipment *> ModbusIndustrialAdapter::equipment()
{
  std::vector<Equipment *> result;
  if (this->connection_state_ == ConnectionState::Disconnected)
  {
    return result;
  }

  result.reserve(this->bound_.size());
  for (auto &item : this->bound_)
  {
    result.push_back(item.get());
  }
  return result;
}

Equipment *ModbusIndustrialAdapter::equipmentById(const std::string &id)
{
  if (this->connection_state_ == ConnectionState::Disconnected)
  {
    return nullptr;
  }

  for (auto &item : this->bound_)
  {
    if (item->id() == id)
    {
      return item.get();
    }
  }
  return nullptr;
}

void ModbusIndustrialAdapter::poll()
{
  if (this->connection_state_ != ConnectionState::Connected)
  {
    if (this->connection_state_ == ConnectionState::Faulted &&
        this->last_error_.empty())
    {
      this->last_error_ = "poll failed: communication fault";
    }
    return;
  }

  for (auto &item : this->bound_)
  {
    if (!item->refreshFromServer())
    {
      this->enterFault(
          std::string("Modbus read failed during poll: ") +
          this->client_->session.lastError());
      return;
    }
  }
}

void ModbusIndustrialAdapter::bindEquipment()
{
  this->bound_.clear();
  for (const auto &mapping : this->config_.equipment)
  {
    this->bound_.push_back(std::make_unique<BoundEquipment>(this, &mapping));
  }
}

void ModbusIndustrialAdapter::enterFault(const std::string &reason)
{
  this->connection_state_ = ConnectionState::Faulted;
  this->last_error_ = reason;
}

bool ModbusIndustrialAdapter::readBoolean(const ModbusRef &ref, bool *value)
{
  if (!ref.mapped || value == nullptr || !this->client_->session.connected())
  {
    return false;
  }

  std::uint8_t bit = 0;
  std::uint16_t word = 0;
  bool ok = false;
  switch (ref.table)
  {
    case ModbusTable::Coil:
      ok = this->client_->session.readCoils(ref.unitId, ref.address, &bit);
      if (ok)
      {
        *value = bit != 0;
      }
      break;
    case ModbusTable::DiscreteInput:
      ok = this->client_->session.readDiscreteInputs(
          ref.unitId, ref.address, &bit);
      if (ok)
      {
        *value = bit != 0;
      }
      break;
    case ModbusTable::HoldingRegister:
      ok = this->client_->session.readHoldingRegisters(
          ref.unitId, ref.address, &word);
      if (ok)
      {
        *value = word != 0;
      }
      break;
    case ModbusTable::InputRegister:
      ok = this->client_->session.readInputRegisters(
          ref.unitId, ref.address, &word);
      if (ok)
      {
        *value = word != 0;
      }
      break;
  }
  return ok;
}

bool ModbusIndustrialAdapter::readDouble(const ModbusRef &ref, double *value)
{
  if (!ref.mapped || value == nullptr || !this->client_->session.connected())
  {
    return false;
  }

  std::uint8_t bit = 0;
  std::uint16_t word = 0;
  bool ok = false;
  switch (ref.table)
  {
    case ModbusTable::Coil:
      ok = this->client_->session.readCoils(ref.unitId, ref.address, &bit);
      if (ok)
      {
        *value = bit != 0 ? 1.0 : 0.0;
      }
      break;
    case ModbusTable::DiscreteInput:
      ok = this->client_->session.readDiscreteInputs(
          ref.unitId, ref.address, &bit);
      if (ok)
      {
        *value = bit != 0 ? 1.0 : 0.0;
      }
      break;
    case ModbusTable::HoldingRegister:
      ok = this->client_->session.readHoldingRegisters(
          ref.unitId, ref.address, &word);
      if (ok)
      {
        *value = static_cast<double>(word);
      }
      break;
    case ModbusTable::InputRegister:
      ok = this->client_->session.readInputRegisters(
          ref.unitId, ref.address, &word);
      if (ok)
      {
        *value = static_cast<double>(word);
      }
      break;
  }
  return ok;
}

bool ModbusIndustrialAdapter::writeBoolean(const ModbusRef &ref, bool value)
{
  if (!ref.mapped || !this->client_->session.connected())
  {
    this->enterFault("Modbus write failed: invalid mapping or client");
    return false;
  }

  bool ok = false;
  switch (ref.table)
  {
    case ModbusTable::Coil:
      ok = this->client_->session.writeCoil(ref.unitId, ref.address, value);
      break;
    case ModbusTable::HoldingRegister:
      ok = this->client_->session.writeHoldingRegister(
          ref.unitId, ref.address, value ? 1 : 0);
      break;
    case ModbusTable::DiscreteInput:
    case ModbusTable::InputRegister:
      this->enterFault("Modbus write failed: read-only table");
      return false;
  }

  if (!ok)
  {
    this->enterFault(
        std::string("Modbus write failed: ") +
        this->client_->session.lastError());
    return false;
  }
  return true;
}

bool ModbusIndustrialAdapter::writeDouble(const ModbusRef &ref, double value)
{
  if (!ref.mapped || !this->client_->session.connected())
  {
    this->enterFault("Modbus write failed: invalid mapping or client");
    return false;
  }

  bool ok = false;
  switch (ref.table)
  {
    case ModbusTable::Coil:
      ok = this->client_->session.writeCoil(
          ref.unitId, ref.address, value != 0.0);
      break;
    case ModbusTable::HoldingRegister:
    {
      const double clipped = std::max(0.0, std::min(65535.0, value));
      ok = this->client_->session.writeHoldingRegister(
          ref.unitId,
          ref.address,
          static_cast<std::uint16_t>(std::lround(clipped)));
      break;
    }
    case ModbusTable::DiscreteInput:
    case ModbusTable::InputRegister:
      this->enterFault("Modbus write failed: read-only table");
      return false;
  }

  if (!ok)
  {
    this->enterFault(
        std::string("Modbus write failed: ") +
        this->client_->session.lastError());
    return false;
  }
  return true;
}

}  // namespace virtual_factory
