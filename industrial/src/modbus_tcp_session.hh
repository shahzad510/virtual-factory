#ifndef VIRTUAL_FACTORY_MODBUS_TCP_SESSION_HH_
#define VIRTUAL_FACTORY_MODBUS_TCP_SESSION_HH_

// Compatibility shim — prefer modbus_session.hh.
#include "modbus_session.hh"

namespace virtual_factory
{
namespace internal
{
using ModbusTcpSession = ModbusSession;
}  // namespace internal
}  // namespace virtual_factory

#endif
