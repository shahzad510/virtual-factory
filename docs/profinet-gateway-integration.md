# PROFINET gateway integration (Phase 6 slice 6H)

**Status:** **SUPPORTED VIA GATEWAY** (ADR-040). **Native PROFINET IO-Controller in MES:** **DEFERRED**.

This document describes the **approved, first-class** integration path by which PROFINET PLCs and IO-Devices reach the MES through the existing `IndustrialAdapter` layer. It is **not** a workaround or a failure to implement PROFINET.

---

## 1. MES requirement

The MES must integrate industrial equipment in a **protocol-independent** way:

```text
Industrial source → IndustrialAdapter → GenericEquipment → MES
```

PROFINET field equipment satisfies this requirement when an **industrial gateway** translates PROFINET IO into a protocol already implemented in Phase 6:

| Gateway exposes | Use adapter (Phase 6) |
| --- | --- |
| OPC UA server | `OpcUaIndustrialAdapter` (6B) |
| Modbus TCP | `ModbusIndustrialAdapter` (6D) |
| REST / HTTP API | `RestIndustrialAdapter` (6E) |
| MQTT broker | `MqttIndustrialAdapter` (6F) |

The MES **does not** need native PROFINET knowledge. PROFINET-specific engineering (GSDML, station names, cyclic IO image layout) stays in the **gateway** and vendor tools.

---

## 2. Reference architecture

### Single gateway, multiple PROFINET PLCs

```text
PROFINET PLC-001 ─┐
PROFINET PLC-002 ─┤  PROFINET plant network
PROFINET PLC-003 ─┘
         │
Industrial gateway  (Hilscher, Siemens, Phoenix, etc. — out of repo scope)
         │  OPC UA | Modbus TCP | REST | MQTT
         ▼
One IndustrialAdapter instance  (one endpoint / origin / broker session)
         ▼
GenericEquipment[]  (PLC-001, PLC-002, PLC-003 — configuration identities)
         ▼
MES (Phase 7+)
```

### Multiple gateways / networks

N gateways ⇒ N `IndustrialAdapter` instances (same rule as ADR-026/036/037/038: one adapter = one industrial **source/session**).

```text
Gateway-A (OPC UA) → OpcUaIndustrialAdapter "gw-a"
Gateway-B (Modbus)   → ModbusIndustrialAdapter "gw-b"
```

No Phase 6 adapter manager. No mega-adapter.

---

## 3. Configuration model (no new C++ class)

**Do not** create `ProfinetGatewayIntegration.hh` or `ProfinetPLC.hh`. Use existing mapping structs:

| Gateway protocol | Config type | Equipment mapping |
| --- | --- | --- |
| OPC UA | `OpcUaAdapterConfig` | `OpcUaEquipmentMapping` |
| Modbus TCP | `ModbusAdapterConfig` | `ModbusEquipmentMapping` |
| REST | `RestAdapterConfig` | `RestEquipmentMapping` |
| MQTT | `MqttAdapterConfig` | `MqttEquipmentMapping` |

Example (conceptual — gateway already mapped PN data to OPC UA nodes):

```cpp
OpcUaEquipmentMapping plc;
plc.id = "PLC-001";           // configuration identity, not a C++ class
plc.type = "plc";
plc.capabilities = {"start", "stop"};
plc.commands = {{"start", nodeRef}, {"stop", nodeRef}};
plc.telemetry = {{"speed", nodeRef, "rpm"}};
plc.state = runningNodeRef;
plc.fault = faultNodeRef;
```

Document gateway-side metadata (PROFINET station name, device ID, slot/submodule) in **Phase 7 onboarding** or external runbooks — not in `Equipment.hh`.

---

## 4. Normalized MES concepts vs gateway capability

Functionality available to MES depends on what the **gateway exposes** on its northbound interface.

| Concept | OPC UA gateway | Modbus TCP gateway | REST gateway | MQTT gateway |
| --- | --- | --- | --- | --- |
| Telemetry | ✓ (mapped nodes) | ✓ (registers/coils) | ✓ (JSON fields) | ✓ (topics) |
| Running/stopped state | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) |
| Machine fault | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) |
| Commands | ✓ (writes) | ✓ (writes) | ✓ (HTTP POST/PUT) | ✓ (publish) |
| Full PROFINET diagnostics | Gateway-dependent; often reduced | Often reduced | API-dependent | Topic-dependent |
| Cyclic RT / IRT timing | Not native PN; polled/subscribed via gateway | Polled | Request/response | Pub/sub latency |

Do **not** claim sub-millisecond PROFINET cyclic IO or GSDML engineering in the MES application.

---

## 5. Test evidence (MES-side, no fake PROFINET)

No fake PROFINET simulator exists in this repository (by design, ADR-040).

**Gateway equivalence is validated** by existing adapter tests that already model **one industrial source → multiple GenericEquipment mappings**:

| Test | What it proves for gateway path |
| --- | --- |
| `opcua_adapter_test` | Multi-equipment on one OPC UA server; telemetry, commands, state, fault; `IndustrialAdapter*` / `Equipment*` |
| `modbus_adapter_test` | Multi-equipment on one Modbus TCP endpoint; isolation across endpoints |
| `rest_adapter_test` | Multi-equipment on one REST origin |
| `mqtt_adapter_test` | Multi-equipment on one broker; two brokers isolation |

These tests use **DEVELOPMENT / INTEGRATION VALIDATION ONLY** fixtures — not PROFINET hardware or vendor gateway certification.

**Not available in CI:** real PROFINET gateway hardware bench. State explicitly when deploying to production.

---

## 6. Native PROFINET (deferred)

A future **native** `ProfinetIndustrialAdapter` (IO-Controller, cyclic RT, GSDML, full PN diagnostics) requires a **separate ADR** and commercial or PI stack approval. It must not contaminate the gateway-first architecture.

---

## 7. Future ~1,200-device benchmark

In a future combined scalability experiment (~200 mappings per protocol), PROFINET-origin equipment participates as **gateway-backed logical devices**:

- ~200 mappings via OPC UA / Modbus / REST / MQTT **origins** that represent gateway-exposed PROFINET equipment — **not** 200 native PN IO-Devices.

Report results as **VALIDATED UNDER THESE TEST CONDITIONS** with CPU, RSS, FDs, sockets, threads, latency, and failure metrics. **16 GB laptop capacity for 1,200 devices is unmeasured.**
