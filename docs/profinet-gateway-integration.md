# PROFINET gateway integration (Phase 6 slice 6H)

**Status:** **SUPPORTED VIA GATEWAY** (ADR-040). This path remains **first-class** and must **not** be removed when native PROFINET is added.

**Native PROFINET:** **APPROVED FOR IMPLEMENTATION** in **ICP** pending commercial stack procurement (ADR-040 amendment 2026-08-29). Code **NOT IMPLEMENTED**. See [`profinet-native-evaluation.md`](profinet-native-evaluation.md). Native PROFINET does **not** belong in MES.

This document describes the **approved, first-class gateway** integration path by which PROFINET PLCs and IO-Devices reach ICP / MES consumers through the existing `IndustrialAdapter` layer. It is **not** a workaround, a stub, or a failure to implement PROFINET.

**Functional requirement:** A PLC using PROFINET must be capable of communicating with consumers in the **same normalized way** as a PLC using OPC UA, Modbus TCP, REST, MQTT, or EtherNet/IP. That requirement is met when a PROFINET gateway exposes process data through a protocol already implemented in Phase 6 — and will also be met by a future native IO-Controller adapter behind the same `Equipment` boundary.

---

## 1. End-to-end architecture

```text
PROFINET PLC / IO network
        ↓
Industrial PROFINET Gateway          (vendor appliance; out of repo scope)
        ↓
OPC UA | Modbus TCP | REST | MQTT
        ↓
Existing IndustrialAdapter           (Phase 6 — unchanged)
        ↓
GenericEquipment                     (configuration identity, not C++ class)
        ↓
MES                                  (Phase 7+ — not started)
```

All three deployment examples below produce the **same conceptual MES equipment model**:

### Example A — gateway exposes OPC UA

```text
PROFINET PLC-001 → Gateway → OPC UA → OpcUaIndustrialAdapter → GenericEquipment(id="PLC-001")
```

### Example B — gateway exposes Modbus TCP

```text
PROFINET PLC-002 → Gateway → Modbus TCP → ModbusIndustrialAdapter → GenericEquipment(id="PLC-002")
```

### Example C — gateway exposes MQTT

```text
PROFINET PLC-003 → Gateway → MQTT broker → MqttIndustrialAdapter → GenericEquipment(id="PLC-003")
```

The MES consumes `GenericEquipment` and `Equipment` — it does **not** need native PROFINET knowledge.

---

## 2. Responsibility split

### A. PROFINET-side responsibility (gateway / field network)

The **industrial gateway** (or equivalent PROFINET proxy) owns all PROFINET-specific concerns. These must **not** leak into `Equipment.hh`, `IndustrialAdapter.hh`, or MES business logic:

| Concern | Owner |
| --- | --- |
| PROFINET IO-Controller / device communication | Gateway |
| Cyclic process data (RT Class 1/2/3, IRT) | Gateway |
| PROFINET-specific diagnostics and alarms | Gateway (where supported) |
| GSDML / device engineering | Gateway vendor tools |
| Slot / submodule / module handling | Gateway |
| PROFINET timing and watchdogs | Gateway |
| Station name, IP, DCP | Gateway / plant engineering |

This repository does **not** implement any of the above. No `ProfinetIndustrialAdapter`, no PROFINET dependency, no fake TCP/UDP PROFINET.

### B. MES-side responsibility (this platform)

The MES and industrial adapter layer own **normalized** equipment semantics only:

| Concern | Owner |
| --- | --- |
| `GenericEquipment` identity (`id`, `type` metadata) | Configuration + adapter mapping |
| Telemetry `{name, value, unit}` | Adapter `poll()` → equipment |
| Commands via `Equipment::execute()` | Adapter → gateway northbound protocol |
| Machine running/stopped state | Mapped into `OperationalState` |
| Machine fault | `Equipment::fault()` from mapped signal |
| Communication / connection health | `IndustrialAdapter::connectionState()` |
| Protocol mapping (nodes, registers, topics, paths) | Adapter config structs |
| Polling / bounded receive | Existing adapters |
| MES business logic (orders, scheduling, OEE, …) | Phase 7+ — **not started** |

### C. Gateway protocol choice (deployment decision)

The gateway vendor determines which northbound protocol(s) are available. **Do not assume every gateway supports every option.**

| Northbound protocol | Phase 6 adapter | Typical gateway use | Notes |
| --- | --- | --- | --- |
| **OPC UA** | `OpcUaIndustrialAdapter` (6B) | Preferred when structured variables, metadata, or richer diagnostics are needed | One adapter = one OPC UA server/endpoint |
| **Modbus TCP** | `ModbusIndustrialAdapter` (6D) | Register/coil-oriented process data | One adapter = one TCP endpoint |
| **MQTT** | `MqttIndustrialAdapter` (6F) | Broker-based telemetry, state, commands | One adapter = one broker session; many PLCs via topic mappings |
| **REST** | `RestIndustrialAdapter` (6E) | HTTP/JSON gateway or vendor API fallback | One adapter = one HTTP origin |

**Not in scope:** a REST **MES application API** — that is a future application layer (Phase 7+). `RestIndustrialAdapter` is an industrial **HTTP client** to a gateway, not the MES server.

---

## 3. Identity and configuration model

Equipment identity is **configuration**, not a C++ type. Do **not** create `PLC001.hh`, `Mixer01.hh`, or `ProfinetPLC001.hh`. Do **not** hard-code `if (id == "PLC-001")` in production code.

```text
PROFINET station / device identity     e.g. Line1_Mixer01  (gateway engineering)
        ↓
Gateway mapping                        vendor tool maps PN IO → OPC UA nodes / Modbus / topics
        ↓
Adapter equipment mapping              OpcUaEquipmentMapping / ModbusEquipmentMapping / …
        ↓
MES equipment identity                 GenericEquipment id = "PLC-001", type = "plc"
```

Gateway-side metadata (PROFINET station name, device ID, slot/submodule) belongs in **Phase 7 onboarding configuration** or external runbooks — not in `Equipment.hh`.

### No new C++ classes

**Do not** create `ProfinetGatewayIntegration.hh`, `ProfinetIndustrialAdapter`, or protocol-specific equipment subclasses. Reuse existing mapping structs:

| Gateway protocol | Config type | Equipment mapping |
| --- | --- | --- |
| OPC UA | `OpcUaAdapterConfig` | `OpcUaEquipmentMapping` |
| Modbus TCP | `ModbusAdapterConfig` | `ModbusEquipmentMapping` |
| REST | `RestAdapterConfig` | `RestEquipmentMapping` |
| MQTT | `MqttAdapterConfig` | `MqttEquipmentMapping` |

Example (conceptual — gateway already mapped PN data to OPC UA nodes):

```cpp
OpcUaEquipmentMapping plc;
plc.id = "PLC-001";           // MES equipment identity — configuration, not a C++ class
plc.type = "plc";
plc.capabilities = {"start", "stop"};
plc.commands = {{"start", nodeRef}, {"stop", nodeRef}};
plc.telemetry = {{"speed", nodeRef, "rpm"}};
plc.state = runningNodeRef;
plc.fault = faultNodeRef;
// Gateway-side PN metadata (station Line1_Mixer01, slot/subslot) lives in onboarding/runbook
```

---

## 4. Multiple gateways / multiple PROFINET networks

There is **no** requirement for one giant PROFINET adapter. Existing Phase 6 topology remains authoritative:

**One adapter instance = one industrial source/session** (OPC UA endpoint, Modbus TCP session, REST origin, MQTT broker, EtherNet/IP device).

```text
PROFINET Network A          PROFINET Network B          PROFINET Network C
  PLC-001                     PLC-101                     PLC-201
  PLC-002                     PLC-102
        ↓                           ↓                           ↓
  Gateway A                   Gateway B                   Gateway C
        ↓ OPC UA                      ↓ Modbus TCP                ↓ MQTT
  OpcUaIndustrialAdapter A    ModbusIndustrialAdapter B   MqttIndustrialAdapter C
        ↓                           ↓                           ↓
  GenericEquipment[]          GenericEquipment[]          GenericEquipment[]
```

N gateways ⇒ N `IndustrialAdapter` instances. No Phase 6 adapter manager. No mega-adapter.

A **single** gateway may expose **many** PROFINET devices through **one** northbound endpoint (e.g. one OPC UA server with multiple mapped `GenericEquipment` instances) — same pattern as multi-PLC OPC UA or multi-equipment MQTT.

---

## 5. Failure isolation

Gateway and adapter failures must remain **isolated per industrial source**.

### Communication failure vs machine fault

| Condition | Signal | Meaning |
| --- | --- | --- |
| Gateway/network/adapter unreachable | `IndustrialAdapter::ConnectionState::Faulted` | **Communication** failure |
| Process fault reported by mapped data | `Equipment::fault() == true` | **Machine** fault |
| Comms fault while equipment listed | Last-known `Equipment*` remains; commands rejected | Adapter design (Phase 6) |

**Do not conflate** `ConnectionState::Faulted` with `Equipment::fault()`. A dropped gateway session does not automatically set machine fault; a running machine on a healthy gateway can still report `fault()` from mapped process data.

### Multi-gateway isolation (validated by existing tests)

If Gateway A fails, Adapter A → `Faulted`; Adapter B can remain `Connected`:

| Test | Isolation evidence |
| --- | --- |
| `opcua_adapter_test` | Two OPC UA servers; stop one → that adapter `Faulted`, other stays `Connected` |
| `opcua_multi_server_scalability_test` | Victim server stop at 38+ adapters; others remain connected |
| `modbus_adapter_test` | Four localhost endpoints; one fails → that adapter `Faulted`, others `Connected` |
| `rest_adapter_test` | Two HTTP origins; stop origin A → adapter A `Faulted`, origin B unaffected |
| `mqtt_adapter_test` | Two brokers; stop broker A → adapter A `Faulted`, broker B stays `Connected` |
| `eip_adapter_test` | Two EtherNet/IP devices; device A stop → adapter A `Faulted`, B unaffected |

These tests model **gateway-equivalent** topologies (one northbound endpoint per adapter). They use **DEVELOPMENT / INTEGRATION VALIDATION ONLY** fixtures — **not** PROFINET hardware or vendor gateway certification.

**Native PROFINET gateway interoperability was not hardware-tested in this environment.**

---

## 6. Capability mapping and honest limits

Gateway integration preserves **MES-relevant process data and control capabilities according to the gateway's exposed protocol and mapping**. Native PROFINET timing, diagnostics, slot/subslot detail, and other protocol-specific capabilities **may not be preserved**.

### PROFINET native vs gateway northbound

| Capability | PROFINET native (approved pending stack) | Gateway → OPC UA | Gateway → Modbus | Gateway → MQTT | Gateway → REST |
| --- | --- | --- | --- | --- | --- |
| Cyclic RT / IRT IO | ✓ (native stack) | Polled/subscribed via gateway | Polled registers | Pub/sub latency | Request/response |
| Sub-ms process timing | Possible (native) | Not guaranteed | Not guaranteed | Not guaranteed | Not guaranteed |
| Structured variables / metadata | PN + engineering | ✓ (nodes, namespaces) | Limited | Topic/JSON dependent | API dependent |
| Full PN diagnostics / alarms | ✓ (native) | Gateway-dependent | Often reduced | Topic-dependent | API dependent |
| GSDML / slot / submodule in MES | Engineering tools | External metadata only | External metadata only | External metadata only | External metadata only |
| Telemetry | ✓ | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) |
| Running/stopped state | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) |
| Machine fault | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) | ✓ (if mapped) |
| Commands | ✓ (if mapped) | ✓ (writes) | ✓ (writes) | ✓ (publish) | ✓ (HTTP) |

The MES abstraction exposes **only what is actually mapped** on the northbound interface. Do **not** claim sub-millisecond PROFINET cyclic IO, GSDML engineering, RT Class 3, IRT, or PROFINET certification in the MES application.

---

## 7. Test evidence (MES-side; no fake PROFINET)

No fake PROFINET simulator exists in this repository (by design, ADR-040). No `profinet_adapter_test`. No PROFINET hardware certification claim.

**Gateway equivalence is validated** by existing protocol adapter tests:

| Test | Validates for gateway path |
| --- | --- |
| `opcua_adapter_test` | Multi-equipment on one server; telemetry, commands, state, fault; comms vs machine fault; reconnect |
| `modbus_adapter_test` | Multi-equipment; multi-endpoint isolation (4 adapters) |
| `rest_adapter_test` | Multi-equipment; two-origin isolation |
| `mqtt_adapter_test` | Multi-equipment on one broker; two brokers; comms fault ≠ machine fault |
| `eip_adapter_test` | Multi-equipment; two-device isolation |
| `opcua_multi_server_scalability_test` | Many independent adapter instances; fault isolation at scale |

Run the full suite:

```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

---

## 8. Native PROFINET (approved pending stack; not coded)

A future **native** `ProfinetIndustrialAdapter` (IO-Controller, cyclic RT, GSDML, PN diagnostics) is **APPROVED FOR IMPLEMENTATION** in **ICP** pending Softing/Hilscher procurement (ADR-040 amendment 2026-08-29). See [`profinet-native-evaluation.md`](profinet-native-evaluation.md). It must **not** replace or remove this gateway path.

Until native code ships: **PROFINET is supported through gateway integration** — not “unsupported.”

---

## 9. Future ~1,200-device benchmark (not run yet)

Preserved for a **future** platform-scale experiment after Phase 7 runtime exists. **Do not run now** unless explicitly requested.

### Target composition (~200 logical devices per category)

| Category | How represented in benchmark |
| --- | --- |
| OPC UA–backed | `OpcUaIndustrialAdapter` instances / mappings |
| Modbus-backed | `ModbusIndustrialAdapter` instances / mappings |
| MQTT-backed | `MqttIndustrialAdapter` broker sessions / topic mappings |
| REST-backed | `RestIndustrialAdapter` origins / mappings |
| EtherNet/IP | `EtherNetIpIndustrialAdapter` device sessions |
| Gateway-backed PROFINET | Logical `GenericEquipment` via OPC UA / Modbus / REST / MQTT **origins** — **not** native PN IO-Devices |

**≈ 1,200 logical `GenericEquipment` mappings** — **not** 1,200 physical PLC network connections.

Session count depends on architecture:

- One MQTT broker may carry hundreds of logical machines on **one** broker connection.
- One OPC UA server (e.g. one PROFINET gateway) may expose many logical machines on **one** `UA_Client`.
- EtherNet/IP typically uses one adapter per device session.

### Metrics to capture (when run)

CPU, RAM/RSS, file descriptors, sockets, threads, network traffic, polling latency, update latency, connection time, message rate, failure isolation, queue utilization (where applicable), DB/API latency (Phase 7+ platform).

### Reporting rule

Report as: **VALIDATED UNDER THESE TEST CONDITIONS**

Never claim: “supports 1,200 PLCs” or “16 GB laptop certified for 1,200 devices” without measured evidence.

---

## 10. Phase status

| Item | Status |
| --- | --- |
| Phase 6 | **COMPLETE** |
| 6A–6G | **IMPLEMENTED / TESTED** |
| 6H | **SUPPORTED VIA GATEWAY** |
| Native PROFINET IO-Controller | **APPROVED FOR IMPLEMENTATION** pending stack; **NOT IMPLEMENTED** |
| ICP-1A | **IMPLEMENTED / TESTED** |
| ICP-1B–1F | **NOT STARTED** |
| Phase 7 / MES | **NOT STARTED** |

See also: ADR-040, `docs/architecture.md` §4.6, `docs/implementation-status.md`.
