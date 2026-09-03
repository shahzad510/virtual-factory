# Architecture

How the Virtual Factory platform is structured, and how the current Gazebo plant sits inside it.

**Architectural authority:** [`MES_SCADA_Virtual_Factory_Source_of_Truth.pdf`](MES_SCADA_Virtual_Factory_Source_of_Truth.pdf) — the **only** active Source of Truth.

**Implementation reality:** [`implementation-status.md`](implementation-status.md). Layers marked **PLANNED** do not exist in code.

**Do not trust** [`archive/`](archive/) for current architecture.

Status labels used below: **IMPLEMENTED** | **PARTIALLY IMPLEMENTED** | **VALIDATED** | **PLANNED** | **NOT IMPLEMENTED**.

---

## 1. Overall system

Intended production stack (SoT) — **two commercial products** (ADR-042):

```text
PHYSICAL FACTORY
  PLCs / sensors / machines / gateways
        │
        ▼
┌─────────────────────────────────────┐
│ INDUSTRIAL CONNECTIVITY PLATFORM    │
│  (ICP — Product 1)                  │
│  adapters │ runtime │ ICP Designer   │
└─────────────────┬───────────────────┘
                  │ Connectivity Integration Contract (CIC)
                  ▼
┌─────────────────────────────────────┐
│ MES CORE (Product 2 — Phase 7)      │
│  MES GUI                            │
└─────────────────┬───────────────────┘
                  │
         SCADA (Phase 8) │ ERP (Phase 11)
                  │
         APPLICATION / API
                  ▼
              .NET / Blazor
```

**Phase 6 / ICP today:** ICP **adapter foundation** COMPLETE. **ICP-1A** runtime **IMPLEMENTED** / **TESTED**. **ICP-1B** persistent configuration **IMPLEMENTED** / **TESTED**. **ICP standalone GUI / Application API IMPLEMENTED** / **TESTED** (isolated branch; `docs/icp-gui-architecture.md`). **ICP-1C CIC NOT STARTED.** **ICP Designer NOT STARTED.** **MES Core NOT STARTED.**

Legacy layer view (still valid inside ICP):

```text
INDUSTRIAL ADAPTERS          (protocol-oriented)
  mock | OPC UA | Modbus | REST | MQTT | EtherNet/IP | PROFINET (gateway + native scaffolding) | PROFIBUS (gateway + native scaffolding)
        ▼
NORMALIZED EQUIPMENT MODEL
```

Gazebo is a **simulation environment**, not this production stack.

Detail: `docs/icp-product-architecture.md`, `docs/mes-core-product-architecture.md`, `docs/connectivity-integration-contract.md`.

MES/SCADA must never depend directly on Gazebo ECM, Gazebo System plugins, Siemens/Allen-Bradley APIs, Modbus register maps, OPC UA node IDs, or vendor SDKs.

### Diagram A — Real factory path — PARTIALLY IMPLEMENTED (Equipment + mock + OPC UA + Modbus + REST + MQTT + EtherNet/IP)

```text
Actual machine / PLC
        │
Industrial Adapter          PARTIALLY IMPLEMENTED
  mock IMPLEMENTED
  OPC UA IMPLEMENTED
  Modbus IMPLEMENTED
  REST IMPLEMENTED
  MQTT IMPLEMENTED
  EtherNet/IP IMPLEMENTED
  PROFINET SUPPORTED VIA GATEWAY (6H; ADR-040)
  Native PN IMPLEMENTED TO SOFTWARE BOUNDARY (HARDWARE VALIDATION PENDING)
        │
Equipment abstraction       IMPLEMENTED
        │
MES / SCADA                 PLANNED
```

### Diagram B — Simulation path — IMPLEMENTED through Equipment; MES/SCADA PLANNED

```text
Gazebo
        │
Gazebo integration          IMPLEMENTED (ConveyorSystem)
        │
Equipment abstraction       IMPLEMENTED
        │
MES / SCADA                 PLANNED
```

### Diagram D — Overall architecture

```text
Factory equipment
        │
PLCs / sensors / machines   (real)     Gazebo plant (simulation)
        │                                    │
Industrial Adapters                          ConveyorSystem
  mock IMPLEMENTED                           IMPLEMENTED
  OPC UA IMPLEMENTED
  Modbus IMPLEMENTED
  REST IMPLEMENTED
  MQTT IMPLEMENTED
  EtherNet/IP IMPLEMENTED
  PROFINET SUPPORTED VIA GATEWAY (6H; ADR-040)
  Native PN IMPLEMENTED TO SOFTWARE BOUNDARY (HARDWARE VALIDATION PENDING)
        │
Normalized Equipment Model  IMPLEMENTED
        │
+---------------------------+
| MES                       | PLANNED (Phase 7) — includes Resource Management
| SCADA                     | PLANNED (Phase 8)
| Future API                | PLANNED
+---------------------------+
        │
Future .NET / Blazor GUI    PLANNED
```

---

## 2. Physical factory path — PARTIALLY IMPLEMENTED (adapter contract + mock + OPC UA + Modbus + REST + MQTT + EtherNet/IP)

```text
Physical equipment
        ▼
   Industrial adapter          PARTIALLY IMPLEMENTED (contract + mock + OPC UA + Modbus + REST + MQTT)
        ▼
Normalized Equipment        IMPLEMENTED (Phase 5)
        ▼
MES / SCADA                 PLANNED
```

Production OPC UA adapter: **IMPLEMENTED** (open62541 client + node mapping; ADR-025, ADR-026).
Production Modbus TCP adapter: **IMPLEMENTED** (libmodbus client + register mapping; ADR-036).
Production REST industrial gateway adapter: **IMPLEMENTED** (libcurl client + JSON mapping; ADR-037). Local HTTP fixture tests are development/integration validation, **not** vendor API certification.
MQTT adapter: **IMPLEMENTED** / **TESTED** (Paho MQTT C client, one broker per instance; ADR-038). Local Mosquitto tests are development/integration validation, **not** cloud/vendor certification.
EtherNet/IP adapter: **IMPLEMENTED** / **TESTED** (libplctag 2.7.1 explicit CIP tag messaging; ADR-039). Class 1 implicit/cyclic I/O **NOT IMPLEMENTED**. Local `ab_server` tests are development/integration validation, **not** Allen-Bradley/Rockwell hardware certification.
PROFINET (6H): **SUPPORTED VIA GATEWAY** (ADR-040, [`profinet-gateway-integration.md`](profinet-gateway-integration.md)). Native IO-Controller **DEFERRED**. A TCP mock is not PROFINET.

---

## 3. Simulation path — IMPLEMENTED (Phases 1–5)

```text
Gazebo Sim 8
        ▼
ConveyorSystem              IMPLEMENTED (Gazebo System plugin)
        ▼
Conveyor                    IMPLEMENTED (simulation-specific Equipment)
        ▼
Equipment abstraction       IMPLEMENTED (generic contract)
```

`ConveyorSystem` is **not** an `IndustrialAdapter`. It is Gazebo-only plant behaviour.

---

## 4. Industrial adapter boundary — PARTIALLY IMPLEMENTED (contract + mock + OPC UA + Modbus + REST + MQTT + EtherNet/IP)

Adapters sit below the equipment model. They are protocol-oriented (`opcua`, `modbus`, `rest`, `mqtt`, `ethernet-ip`, `profinet`, `mock`), not one class per PLC vendor or machine type.

```text
MES / SCADA                 PLANNED
        ▼
Equipment                   IMPLEMENTED
        ▼
IndustrialAdapter           IMPLEMENTED (contract)
  MockIndustrialAdapter     IMPLEMENTED
  OpcUaIndustrialAdapter    IMPLEMENTED
  ModbusIndustrialAdapter   IMPLEMENTED
  RestIndustrialAdapter     IMPLEMENTED (6E)
  MqttIndustrialAdapter     IMPLEMENTED (6F)
  EtherNetIpIndustrialAdapter IMPLEMENTED (6G)
  (no native ProfinetIndustrialAdapter — 6H via gateway; ADR-040)
```

`IndustrialAdapter` (C++): `id()`, `protocol()`, `connectionState()`, `lastError()`, `connect()` / `disconnect()`, `equipment()` / `equipmentById()`, `poll()`.

`ConnectionState::Faulted` is a **communication** fault. `Equipment::fault()` is a **machine** fault.

Library `virtual_factory_industrial` links `virtual_factory_equipment`, **open62541** (OPC UA client), **libmodbus** (Modbus TCP client), **libcurl** (REST HTTP client), **Paho MQTT C** (`libpaho-mqtt3as`, MQTT 3.1.1), and **libplctag** (EtherNet/IP explicit messaging). nlohmann/json is adapter-private. `IndustrialAdapter.hh` does not include open62541, libmodbus, curl, nlohmann/json, Paho, or libplctag types. The Gazebo plugin does not link industrial, open62541, libmodbus, libcurl, Paho, or libplctag.

**One adapter instance = one industrial source/session.** Several OPC UA servers ⇒ several `OpcUaIndustrialAdapter` instances (ADR-026). Several Modbus TCP endpoints ⇒ several `ModbusIndustrialAdapter` instances (ADR-036). Several REST origins ⇒ several `RestIndustrialAdapter` instances (ADR-037). Several MQTT brokers ⇒ several `MqttIndustrialAdapter` instances (ADR-038). Several EtherNet/IP devices ⇒ several `EtherNetIpIndustrialAdapter` instances (ADR-039). `connectionState()` is per-source. A faulted source does not take down equipment on other adapters. **An adapter manager** is **ICP-1A** (`virtual_factory::icp::AdapterManager`) — not Phase 6 and not MES Core. **Persistent configuration** is **ICP-1B** (`ConfigurationCatalog` + JSON repository; ADR-047) — not MES and not CIC.

Measured in-process validation (not production proof): [`opcua-scalability-test.md`](opcua-scalability-test.md). Validated at 100 and 200 simulated servers under those test conditions. Do not treat that as “unlimited PLCs” or production hardware certification.

### 4.1 OPC UA adapter — IMPLEMENTED (ADR-025, ADR-026)

```text
PLC-1 OPC UA server          PLC-2 OPC UA server
        │                            │
OpcUaIndustrialAdapter        OpcUaIndustrialAdapter
  (one UA_Client)               (one UA_Client)
        │                            │
        └────────────┬───────────────┘
                     ▼
              IndustrialAdapter*
                     ▼
              GenericEquipment
                     ▼
              MES / SCADA later
```

```text
OPC UA server / PLC
        │
OpcUaIndustrialAdapter (open62541 client)
        │
IndustrialAdapter
        │
GenericEquipment
        │
MES / SCADA later
```

Mapping is C++ config (`OpcUaAdapterConfig`): equipment id/type, command name → node, telemetry name/unit → node, optional Running/Fault nodes. MES/SCADA never see NodeIds.

Command path: `execute("start")` → adapter → OPC UA write. Telemetry path: node read → `poll()` → `GenericEquipment.telemetry()`.

`connect()` after `Faulted` recreates the client and reconnects if the endpoint is reachable. Automatic background reconnect is **NOT IMPLEMENTED**.

`TelemetryPoint` remains `{name, value, unit}`. OPC UA timestamps/quality are **NOT IMPLEMENTED** (later enhancement; do not redesign telemetry for this milestone).

The in-process test server exposes several **test-only** machines (mixer, pump, unknown machine) as string NodeIds. They are not a required machine catalog and have no C++ subclasses.

**DEVELOPMENT ONLY security:** tests use localhost, `SecurityPolicy#None`, anonymous access. Not production industrial security. SignAndEncrypt, certificates, subscriptions, history, and alarms/conditions are **NOT IMPLEMENTED**.

### 4.2 Modbus TCP adapter — IMPLEMENTED (ADR-036)

```text
PLC-A Modbus TCP :1502        PLC-B Modbus TCP :1503
        │                            │
ModbusIndustrialAdapter        ModbusIndustrialAdapter
  (one TCP session)              (one TCP session)
        │                            │
        └────────────┬───────────────┘
                     ▼
              IndustrialAdapter*
                     ▼
              GenericEquipment
                     ▼
              MES / SCADA later
```

```text
Modbus TCP endpoint / PLC
        │
ModbusIndustrialAdapter (libmodbus client)
        │
IndustrialAdapter
        │
GenericEquipment
        │
MES / SCADA later
```

**One adapter instance = one Modbus TCP host:port session.** N endpoints ⇒ N adapter instances. Several logical machines on one endpoint are several `GenericEquipment` objects via mapping, not extra adapter classes. Do not create `PumpModbusAdapter` / `MixerModbusAdapter` / `RobotModbusAdapter`.

Mapping is C++ config (`ModbusAdapterConfig` / `ModbusEquipmentMapping`): equipment id/type metadata, command name → coil or holding register, telemetry name/unit → coil/discrete/holding/input, optional running and fault coils. MES/SCADA never see unit ids, function codes, or addresses.

Command path: `execute("start")` → adapter → Modbus write (coil true, or holding 1). Names starting with `set_` write the `execute()` double as a uint16 holding register (or coil true/false). Discrete inputs and input registers are read-only.

Telemetry path: Modbus read → `poll()` → `GenericEquipment.telemetry()`. Optional state coil maps to Running/Stopped; optional fault coil maps to `Equipment::fault()`.

`ConnectionState::Faulted` is a **communication** failure (connect refused, I/O error, illegal address exception). `Equipment::fault()` is a **machine** process fault. A dropped TCP session does not set machine fault. Last-known equipment remains listed while Faulted.

`connect()` after `Faulted` closes and recreates the libmodbus client session (explicit reconnect). Automatic background reconnect is **NOT IMPLEMENTED**.

Implemented client functions: FC 1–6 (read coils / discrete inputs / holding / input registers; write single coil / single holding register). Batch writes, FC 15/16, Modbus RTU, TLS, and unit-id multiplexing across multiple TCP sessions inside one adapter are **NOT IMPLEMENTED**.

Tests use an in-process **libmodbus TCP slave** (`tests/modbus_test_server.*`) on localhost with mapped test machines (mixer, pump, unknown) as **data/labels only**. **DEVELOPMENT ONLY:** no authentication, no TLS. Not a production PLC.

Isolation was checked with two independent endpoints and a modest 4-endpoint loop on localhost. That is correctness validation in this environment, **not** a production capacity claim and **not** “supports hundreds of Modbus PLCs.”

REST does not replace OPC UA or Modbus (ADR-013).

### 4.3 REST industrial gateway (6E) — IMPLEMENTED (ADR-037)

```text
Gateway-A HTTP origin          Gateway-B HTTP origin
        │                            │
RestIndustrialAdapter          RestIndustrialAdapter
  (one HTTP origin)              (one HTTP origin)
        │                            │
        └────────────┬───────────────┘
                     ▼
              IndustrialAdapter*
                     ▼
              GenericEquipment
                     ▼
              MES / SCADA later
```

HTTP **client** to one industrial gateway/vendor origin (libcurl). Not a fieldbus. Not the future MES REST API. JSON parsing (nlohmann/json) stays inside the industrial implementation. Public headers do not include `<curl/curl.h>` or `<nlohmann/json.hpp>`.

**One adapter instance = one HTTP origin** (scheme + host + port + optional base path). N independent REST systems ⇒ N adapter instances. Several logical machines on one API are several `GenericEquipment` objects via mapping. Do not create `PumpRestAdapter` / `MixerRestAdapter` / a multi-origin REST adapter.

Mapping is C++ config (`RestAdapterConfig` / `RestEquipmentMapping`): equipment id/type metadata, command name → POST/PUT/PATCH path and optional `{{value}}` JSON body, telemetry name/unit → JSON Pointer into one GET resource, optional state/fault pointers into that same JSON. MES/SCADA never see URLs, HTTP methods, JSON Pointers, or credentials.

`poll()` GETs each mapped telemetry resource once and extracts multiple values. `execute()` performs the mapped write. Commands are generic names from mapping; the adapter does not assume every machine has start/stop/set_speed.

`ConnectionState::Faulted` is a **communication** failure (curl/DNS/refused/timeout/applicable HTTP 4xx/5xx). `Equipment::fault()` is a **machine** process fault from mapped JSON. An HTTP failure does not set machine fault. Last-known equipment remains listed while Faulted.

`connect()` after `Faulted` recreates the curl session (explicit reconnect). Automatic background reconnect is **NOT IMPLEMENTED**. Optional configured health GET on `connect()`; if omitted, TCP connectivity to the origin is enough and mapped requests may proceed.

Authentication: Basic and Bearer in adapter config only. Passwords and tokens are not written to `lastError()`. TLS certificate verification is **on by default** for HTTPS. Disabling verification is an explicit development/testing opt-in.

Implemented methods: GET (reads), POST/PUT/PATCH (commands). DELETE is **NOT IMPLEMENTED**.

Tests use an in-process HTTP/1.1 fixture (`tests/rest_test_server.*`) on localhost. **DEVELOPMENT/INTEGRATION VALIDATION ONLY:** no TLS on the fixture. Not vendor API certification and not a production gateway. Isolation was checked with two independent origins. That is correctness validation, **not** production scalability.

### 4.4 MQTT (6F) — IMPLEMENTED (ADR-038)

```text
Broker A                         Broker B
        │                                │
MqttIndustrialAdapter            MqttIndustrialAdapter
  (one broker/session)             (one broker/session)
        │                                │
   PLC-001  PLC-002                 PLC-101  PLC-102
   (GenericEquipment mappings)      (GenericEquipment mappings)
        │                                │
        └──────────────┬─────────────────┘
                       ▼
                IndustrialAdapter*
                       ▼
                GenericEquipment
                       ▼
                MES / SCADA later
```

MQTT **client** to one broker (Eclipse Paho MQTT C, MQTTAsync, MQTT 3.1.1). Not a broker. Not an MES event bus. JSON payloads use nlohmann/json inside the industrial implementation. Public headers do not include `<MQTTAsync.h>`, `<MQTTClient.h>`, or `<nlohmann/json.hpp>`.

**One adapter instance = one MQTT broker/session.** Several logical machines on one broker are several `GenericEquipment` mappings (e.g. `id() = "PLC-001"`, `type() = "plc"` as metadata — not a `PLC001.hh` class). A second broker uses a second adapter instance. Do not create `PumpMqttAdapter` / one adapter per PLC for MQTT.

Mapping is C++ config (`MqttAdapterConfig` / `MqttEquipmentMapping`): exact telemetry/state/fault topics and payload encodings (JSON Pointer, numeric text, boolean text); command name → topic, body template with optional `{{value}}`, QoS (default 1), retain (default false). Wildcard `+` / `#` subscriptions are rejected in 6F. Broker host/port, client id, credentials, and TLS stay in adapter config. Equipment must not expose MQTT types or secrets.

`poll()` waits up to `pollTimeoutMs` (default **50 ms**) on a bounded in-process queue filled by Paho's internal network thread. It must not block indefinitely. Keepalive is serviced by that Paho thread, not by an application reconnect thread. Automatic reconnect is **NOT IMPLEMENTED**; `connect()` after `Faulted` recreates the client and restores subscriptions.

`ConnectionState::Faulted` is a **communication** failure (broker down, connect/subscribe/publish failure). `Equipment::fault()` is a **machine** process fault from mapped payload/topic only. A broker failure does not set machine fault. Last-known equipment remains listed while Faulted. Malformed telemetry is ignored; it does not Faulted the session.

MQTT client IDs must be unique among simultaneously connected clients to the same broker in this process. An empty `clientId` generates `vf.<adapterId>.<serial>` (never from secrets). Duplicate configured IDs on the same broker are rejected.

Authentication: username/password in adapter config. TLS certificate verification is **on by default**. Insecure TLS is an explicit development/testing opt-in. Passwords are not written to `lastError()`.

6F maps latest-value telemetry/state/commands. Durable MES events (cycle complete, scrap, quality, OEE) are **Phase 7** and are not implemented here. Sparkplug B and MQTT 5-specific architecture are **NOT IMPLEMENTED**.

Tests use a local **Mosquitto** process (`tests/mqtt_test_broker.*`) — **DEVELOPMENT/INTEGRATION VALIDATION ONLY**, not cloud/vendor MQTT certification. Isolation was checked with two adapters on one broker and two independent brokers. Multi-equipment scale was **VALIDATED** at 10/50/100/200 mappings on one broker and 2×50 across two brokers (`docs/mqtt-scalability-test.md`). That is correctness/scale validation under those conditions, **not** a claim of hundreds of production PLCs or brokers.

### 4.5 EtherNet/IP (6G) — IMPLEMENTED / TESTED (ADR-039)

CIP scanner/client via **libplctag v2.7.1** (commit `bdb10aeaf4f374cec7ae4e66887446dedf952dc1`, MPL-2.0). Not Modbus with another library. **Explicit messaging / symbolic tag read/write only.** Class 1 implicit/cyclic I/O (UDP 2222) is **NOT IMPLEMENTED**. Do not claim implicit I/O without library+test evidence.

```text
PLC-A EtherNet/IP device       PLC-B EtherNet/IP device
        │                            │
EtherNetIpIndustrialAdapter    EtherNetIpIndustrialAdapter
  (one device session)             (one device session)
        │                            │
        └────────────┬───────────────┘
                     ▼
              IndustrialAdapter*
                     ▼
              GenericEquipment
                     ▼
              MES / SCADA later
```

**One adapter instance = one EtherNet/IP device/session** (host + port + CIP path + plc type). N devices ⇒ N adapter instances. Several logical machines on one device are several `GenericEquipment` mappings (`PLC-001` is a configuration identity, not a C++ class).

Mapping is C++ config (`EtherNetIpAdapterConfig`): equipment id/type, command name → tag + value type (BOOL/DINT/REAL), telemetry name/unit → tag, optional Running/Fault tags. MES/SCADA never see CIP paths or libplctag handles.

Command path: `execute("start")` → adapter → libplctag tag write. Telemetry path: tag read → `poll()` → `GenericEquipment.telemetry()`.

`connect()` after `Faulted` recreates tags and reconnects explicitly. Automatic background reconnect is **NOT IMPLEMENTED**.

`ConnectionState::Faulted` is a **communication** failure (connect refused, timeout, tag create/read/write error). `Equipment::fault()` is a **machine** process fault from mapped fault tag only. A dropped device session does not set machine fault. Last-known equipment remains listed while Faulted.

Private `eip_session` wraps libplctag; public headers do not include `libplctag.h`. libplctag is linked privately (shared `libplctag.so` preferred).

Tests use libplctag **`ab_server`** ControlLogix emulator (`tests/eip_test_server.*`) — **DEVELOPMENT/INTEGRATION VALIDATION ONLY**, not Allen-Bradley/Rockwell hardware certification. Two-device isolation was **VALIDATED** under those test conditions (`eip_adapter_test`). That is correctness/isolation validation, **not** a claim of hundreds of production PLCs.

### 4.6 PROFINET (6H) — **SUPPORTED VIA GATEWAY**; native IO-Controller **DEFERRED** (ADR-040)

PROFINET IO is **not** OPC UA / Modbus / REST / MQTT / EtherNet/IP. Native integration requires an **IO-Controller** (Layer 2 cyclic IO, DCP, GSDML). **No OSS C/C++ IO-Controller** suitable for this repo was found (p-net is IO-Device/GPLv3 only).

**Final decision (ADR-040):** Phase 6 **6H is satisfied by supported gateway integration** — a **first-class**, **approved** path, not a workaround. PROFINET field equipment integrates via an **external industrial gateway** exposing **OPC UA, Modbus TCP, REST, or MQTT** to existing adapters (6B–6F). MES/SCADA still consume `GenericEquipment`; they never see PROFINET slots.

```text
PROFINET IO-Device(s)
        │
Industrial gateway (out of repo)
        │ OPC UA | Modbus | REST | MQTT
        ▼
Existing IndustrialAdapter (6B–6F)
        ▼
GenericEquipment
        ▼
MES (Phase 7+)
```

**`ProfinetIndustrialAdapter` is NOT IMPLEMENTED** and is **deferred** unless a future ADR approves a commercial or PI Community Stack path.

See [`profinet-gateway-integration.md`](profinet-gateway-integration.md) for configuration model, capability matrix, multi-PLC topology, and test evidence.

**Trade-offs vs native:** higher latency; gateway-dependent diagnostics; cyclic IO becomes polled gateway data, not direct RT Class 1. **Acceptable for Phase 6** equipment abstraction goal.

**Failure isolation:** One gateway/adapter failure must not fault unrelated gateways. Existing tests prove per-endpoint isolation (`opcua_adapter_test`, `modbus_adapter_test`, `rest_adapter_test`, `mqtt_adapter_test`, `eip_adapter_test`). Communication `Faulted` ≠ `Equipment::fault()`. See [`profinet-gateway-integration.md`](profinet-gateway-integration.md) §5.

**Capability honesty:** Gateway integration preserves MES-relevant mapped data only; native PN timing/diagnostics may not be preserved.

**Future 1,200-device benchmark:** PROFINET “200 devices” = **200 gateway-backed logical mappings** via existing adapters — **not** 200 native PN IO-Devices. **VALIDATED UNDER THESE TEST CONDITIONS** only after measurement; 16 GB laptop capacity unclaimed. Logical device count ≠ network session count.

---

## 5. Normalized equipment model — IMPLEMENTED

Open-ended contract. **Not** a required catalog:

```text
Equipment                    REQUIRED ARCHITECTURE
   └── GenericEquipment      ordinary machines (id, type metadata, capabilities, …)

Conveyor                     ALLOWED EXCEPTION — Gazebo belt example only
```

Do **not** require `Robot.hh` / `Pump.hh` / `Oven.hh` / `Mixer.hh`. Add specialized C++ only when behaviour cannot be expressed as capabilities + commands + telemetry.

`type()` is metadata (e.g. `belt_conveyor`). MES/SCADA must not `switch` on type.

Headers:

- `equipment/include/virtual_factory/equipment/Equipment.hh`
- `equipment/include/virtual_factory/equipment/GenericEquipment.hh`
- `equipment/include/virtual_factory/equipment/Conveyor.hh`

No `#include <gz/...>`.

---

## 6. Equipment commands — IMPLEMENTED (generic)

`commands()` and `execute(name, parameter)`. `start()` / `stop()` wrap `execute("start"|"stop")`.

Do not put every industrial verb on `Equipment` (`setPressure`, `move`, `loadRecipe`, …). Capabilities decide what is valid. Parameter is currently a single `double`.

---

## 7. Equipment state — IMPLEMENTED (coarse)

`operationalState()` → Stopped / Running. `running()` helper. `status()` snapshot: id, type, operational state, fault.

Timestamps, modes, counters: **NOT IMPLEMENTED** (long-term SoT model).

---

## 8. Telemetry — IMPLEMENTED (generic)

`telemetry()` → `{name, value, unit}`. Names are open-ended (`speed`, `pressure`, `temperature`, …). No fixed struct of every industrial measurement. Conveyor speed is telemetry `"speed"` (m/s), not a field on `Equipment`.

---

## 9. Alarms / faults — PARTIALLY IMPLEMENTED

- Machine fault: `Equipment::fault()`; `setFault` on GenericEquipment/Conveyor. Plugin does not inject faults.
- Communication fault: adapter `ConnectionState::Faulted`.
- Alarm lists, ack, escalation: **PLANNED** (SCADA / MES).

---

## 10. MES boundary — PLANNED (Phase 7) — **NOT IMPLEMENTED**

MES consumes `Equipment` (and adapter connection state), not Gazebo or raw tags. It does **not** live inside `Equipment` or `IndustrialAdapter`. There is **no MES database, scheduler, OEE engine, GUI, or API in the repository.**

Authoritative detail: SoT PDF §§8–23. Supporting ADRs: 024, 027–035.

### 10.1 Intended MES Core scope (all PLANNED)

1. Configurable plant hierarchy and location assignment  
2. Dynamic PLC / equipment onboarding (configuration, not new C++ per PLC)  
3. Production order / work order / operation management  
4. Product / process definitions, routing, BOM, bill of process  
5. **Resource Management** (capability, availability, allocation, reservation, capacity)  
6. Work centers (capability cells, not 1:1 with machines)  
7. Organizational responsibility (supervisors/managers; RBAC is Phase 9)  
8. Resource readiness checks with **specific hold reasons**  
9. Planning, scheduling, dispatching, rescheduling  
10. Production execution tracking  
11. Material management (raw, component, WIP, finished, consumable, packaging)  
12. Scrap / waste / rework  
13. Quality execution, NCR, sampling; SPC later  
14. Forward/backward genealogy  
15. Downtime and reason trees  
16. OEE (distinct from broader efficiency)  
17. Operational analytics (real-time through yearly)  
18. Bottleneck identification  
19. Personnel / qualifications / shifts (not RBAC)  
20. Tools / fixtures  
21. Maintenance **availability** (not a full CMMS)  
22. Contextualized production events  
23. Reporting / KPI dashboards (application GUI later)  
24. Closed-loop production feedback  
25. Future ERP/PLM/QMS/CMMS hooks (Phase 11 owns deep integration)

### 10.2 Equipment vs MES resource — PLANNED (ADR-024)

| `Equipment` (Phase 5, **IMPLEMENTED**) | MES resource (Phase 7, **PLANNED**) |
| --- | --- |
| Technical asset | What production execution requires or constrains |
| Identity, type, operational state, commands, telemetry, machine fault | Capability, availability, allocation, reservation, capacity, utilization, readiness, relationships |

A physical machine may appear in both models. Do **not** put scheduling, allocation, or reservation on `Equipment`. Do **not** put MES scheduling in `IndustrialAdapter`.

MES resources include equipment, work centers, lines, operators, technicians, tools, fixtures, materials, energy/capacity constraints where relevant, and maintenance availability.

### 10.3 Resource Management — PLANNED (ADR-024)

**Capability** ≠ **availability.** Example: M-001 can make Product X but is unavailable; another machine is free but cannot make Product X. Both facts are required before dispatch.

#### Diagram C — MES resource architecture — PLANNED

```text
Production Order
       │
       ▼
MES
 |
 +-- Plant configuration / hierarchy
 +-- Order Management
 +-- Routing / BOM / BOP
 +-- Resource Management
 |     +-- Equipment resources
 |     +-- Work Centers
 |     +-- Personnel
 |     +-- Tools
 |     +-- Materials
 |     +-- Availability vs capability
 |     +-- Allocation / reservation / capacity
 |
 +-- Readiness check (specific reasons)
 +-- Scheduling / dispatching
 +-- Production Tracking
 +-- Quality / scrap
 +-- Traceability
 +-- Downtime / OEE / analytics
```

### 10.4 Configurable plant hierarchy — PLANNED (ADR-027)

Hierarchy is **configuration**, not C++ inheritance. A default conceptual tree:

```text
Enterprise → Site/Plant → Building → Floor → Area
  → Production line / assembly line / process cell
    → Work Center → Equipment, personnel, tools, other resources
```

Other valid trees: Plant → Area → Process Cell → Work Center; or Plant → Building → Floor → Line → Work Center. “Assembly line” is not the only allowed structure.

### 10.5 Work Centers — PLANNED

A Work Center is a **production capability**, not necessarily one machine. Example WC-100: capabilities welding/drilling/inspection; resources Robot-17, Welder-03, Operator-22, Fixture-4; constraints on qualification, fixture, maintenance, material, capacity.

No Work Center types exist in the repository.

### 10.6 Dynamic PLC / equipment onboarding — PLANNED (ADR-028)

Adding PLCs later must **not** require a new C++ class or a rebuild of MES. Configuration/UI/API (UI later) creates another protocol adapter instance (`OpcUaIndustrialAdapter`, `ModbusIndustrialAdapter`, `RestIndustrialAdapter`, `MqttIndustrialAdapter`, `EtherNetIpIndustrialAdapter`, later PROFINET if approved) plus mappings, then assigns the resulting `Equipment` to plant/line/work center and a responsible supervisor.

Today, adapter instances are constructed in C++/tests. That is **not** the future onboarding product.

### 10.7 MES availability vs equipment state — PLANNED (ADR-030)

Equipment technical state: Stopped / Running / Faulted (comms fault is adapter `ConnectionState::Faulted`).

MES availability examples: Available, Unavailable, Reserved, Allocated, Maintenance, Blocked, Quality Hold, Scheduled, Decommissioned.

A healthy running machine may still be MES-unavailable.

### 10.8 Resource readiness — PLANNED (ADR-029)

```text
Production Order → operations → required resources
        │
        ▼
 RESOURCE READY?
    │         │
  READY      HOLD + specific reasons
    │
 Schedule → Dispatch → Execution
```

HOLD reasons must be explicit (fault, maintenance, operator, qualification, material shortage, material quality hold, tool, capacity, reservation, incompatible setup, …), not a single “unavailable” flag.

### 10.9 Orders, materials, scrap, quality, genealogy — PLANNED

- **Orders:** production/work orders, operations, routing, BOM/BOP, quantities, planned/actual times, priority, due date, status, holds, partial completion, scrap, rework.  
- **Materials:** identity, lot/serial, quantity, location, status, quality status, expiry, reservation, consumption, production, scrap, transfer. Material availability participates in readiness (ADR-031).  
- **Scrap:** planned/good/scrap/rework, reason, operation, work center, equipment, lot, operator, time, order, product. Scrap feeds quality and efficiency KPIs (ADR-031).  
- **Quality:** inspection plans, characteristics, specs, sampling, measurements, pass/fail, defects, NCR, rework, scrap, CAPA, holds, release. SPC is a later capability.  
- **Genealogy:** forward and backward (ADR-034).

### 10.10 OEE, downtime, efficiency, losses — PLANNED (ADR-032)

OEE = Availability × Performance × Quality. Aggregate Equipment → Work Center → Line → Area → Plant using **documented weighting** (counts or time). Do **not** blindly average OEE percentages.

Broader efficiency also uses material yield, scrap, rework, labor/capacity utilization, on-time completion. Keep OEE a distinct formula.

Downtime events use a reason tree (mechanical, electrical, controls, material, operator, quality, changeover, setup, maintenance, waiting, starvation, blockage, unknown).

Loss analysis (downtime, speed, scrap, rework, material, labor, capacity, changeover) may later estimate cost via ERP. No finance engine in Phase 7.

### 10.11 Scheduling, bottlenecks, what-if — PLANNED

Planning / scheduling / dispatching / rescheduling using priority, due date, capabilities, availability, materials, qualifications, tooling, maintenance windows, changeover, sequence constraints, capacity, operation dependencies. The scheduler should explain why an order cannot be scheduled.

Bottlenecks: where, why, duration, affected orders, constraining resource class. Heavy what-if/optimization may extend into Phase 11.

### 10.12 Personnel, tools, maintenance, responsibility — PLANNED

- Personnel: qualifications, certifications, shifts, availability, assignment, labor utilization. **Not** Phase 9 RBAC.  
- Tools/fixtures: availability, reservation, compatibility, usage, calibration/maintenance.  
- Maintenance: MES observes availability; full CMMS is Phase 11.  
- Responsibility: Plant/Area/Line/Work Center supervisors and managers as data relationships (ADR-027). Permissions to edit that data are Phase 9.

### 10.13 Events and analytics layers — PLANNED (ADR-033)

Raw industrial data → adapter → Equipment → MES contextualization → production/downtime/quality/material events → analytics → dashboard.

Do not compute dashboards only from raw OPC UA tags. Periods: real-time, hourly, daily, weekly, monthly, yearly, each comparable to target / previous / plan / baseline.

### 10.14 Industry benchmark — PLANNED (ADR-035)

Siemens Opcenter is a **capability benchmark** (execution, tracking, materials, quality, genealogy, SPC, OEE, scheduling, visibility). **Not implemented. Not feature parity. Not a copied proprietary architecture.**


---

## 11. SCADA boundary — PLANNED (Phase 8)

Live state, operator commands, alarms/ack, trends, shop-floor execution views, role-specific HMI. Not a substitute for MES. Not a C++ desktop inside the plugin. **NOT STARTED.**

---

## 12. API boundary — PLANNED

.NET/C# application services over MES/SCADA. **NOT IMPLEMENTED.**

---

## 13. .NET / C# application layer — PLANNED

Enterprise services, API, orchestration. C++ remains for simulation and industrial/protocol code (ADR-002, ADR-011). **NOT IMPLEMENTED.**

---

## 14. Blazor GUI — PLANNED

Long-term ADR-011 hint for product GUIs. **ICP standalone browser GUI** (vanilla SPA + `/api/v1`) is **IMPLEMENTED** / **TESTED** on the isolated GUI branch — see `docs/icp-gui-architecture.md`. That GUI is not Blazor and not ICP Designer.

Preferred web UI (SoT §12, ADR-011). **NOT IMPLEMENTED.**

---

## 15. Authentication / authorization — PLANNED (Phase 9)

Central RBAC, audit, least privilege. Supervisor is the floor-management role name (ADR-014). **NOT IMPLEMENTED.**

---

## 16. Database — PLANNED

MES persistence, events, audit. **NOT IMPLEMENTED.**

---

## 17. Real factory integration — PLANNED (Phase 10)

Real PLCs/sensors/machines through production adapters without rewriting MES. Reuses Phase 7 onboarding configuration against real endpoints. Same one-adapter-per-source rule. Depends on Phase 6 protocol adapters + Phase 7. **NOT STARTED.**

Deep ERP/PLM/QMS/CMMS integration and manufacturing intelligence beyond core MES analytics belong to **Phase 11**, not Phase 7 implementation.

---

## 18. Scalability to arbitrary equipment

Normal path: `GenericEquipment` or adapter-populated `Equipment` with id, type metadata, capabilities, commands, telemetry, state, faults.

Specialized C++ (like `Conveyor`) only when custom logic is required. Core software must not grow a closed machine-class tree.

---

## 19. Current Gazebo mapping (IMPLEMENTED)

| Piece | Detail |
| --- | --- |
| World | `gazebo/worlds/phase1/factory.sdf`, step 0.001 s |
| CV-001 | static; plugin `libConveyorSystem.so` |
| PRODUCT-001 | ~0.4×0.4×0.3 m; pose `-1.5 0 1.25` |
| Plugin | `virtual_factory::ConveyorSystem` : Configure + PreUpdate; owns `Conveyor` |
| Motion | `dt_s = duration<double>(info.dt).count()`; `x += speed * dt_s` while running |

Development Start/Stop at updates 1000/5000 is **not** SCADA (ADR-017).

---

## 20. Official phases (SoT 1–11)

| Phase | Name | Code status |
| --- | --- | --- |
| 1 | Factory Foundation | **IMPLEMENTED** |
| 2 | Equipment Plugin Foundation | **IMPLEMENTED** |
| 3 | Conveyor Control | **IMPLEMENTED** |
| 4 | Product Motion | **IMPLEMENTED** |
| 5 | Industrial Equipment Abstraction | **IMPLEMENTED** |
| 6 | Industrial Adapter Layer | **COMPLETE** (6A–6G implemented/tested; 6H supported via gateway; native PN deferred) |
| 7 | MES Core + Resource Management | **PLANNED / NOT IMPLEMENTED** |
| 8 | SCADA / Operational HMI | **PLANNED / NOT IMPLEMENTED** |
| 9 | Security & Authorization | **PLANNED / NOT IMPLEMENTED** |
| 10 | Real Factory Integration | **PLANNED / NOT IMPLEMENTED** |
| 11 | Commercial Hardening & Enterprise Integration | **PLANNED / NOT IMPLEMENTED** |

Phase 6 is **COMPLETE** (final audit 2026-08-28). Slices **6A–6G** implemented/tested; **6H** supported via gateway (ADR-040). Official numbering stays Phases **1–11** (ADR-041). Do not start Phase 7 until explicitly instructed.

Do not use Stage 0–25 or other retired numbering as the live plan.

---

## 21. What this file is not

This is not a second SoT. The active PDF is generated from `docs/source/MES_SCADA_Virtual_Factory_Source_of_Truth.md` via `docs/source/generate-sot-pdf.sh`. The previous PDF is in `docs/archive/` and is **not** authoritative. Do not silently drift.
