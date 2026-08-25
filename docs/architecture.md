# Architecture

How the Virtual Factory platform is structured, and how the current Gazebo plant sits inside it.

**Architectural authority:** [`MES_SCADA_Virtual_Factory_Source_of_Truth.pdf`](MES_SCADA_Virtual_Factory_Source_of_Truth.pdf) — the **only** active Source of Truth.

**Implementation reality:** [`implementation-status.md`](implementation-status.md). Layers marked **PLANNED** do not exist in code.

**Do not trust** [`archive/`](archive/) for current architecture.

Status labels used below: **IMPLEMENTED** | **PARTIALLY IMPLEMENTED** | **VALIDATED** | **PLANNED** | **NOT IMPLEMENTED**.

---

## 1. Overall system

Intended production stack (SoT):

```text
PHYSICAL FACTORY
  PLCs / sensors / machines / other equipment
        │
        ▼
INDUSTRIAL ADAPTERS          (protocol-oriented)
  mock | OPC UA | Modbus | REST | MQTT | EtherNet/IP | PROFINET
        ▼
NORMALIZED EQUIPMENT MODEL
        │
   ┌────┴────┐
   ▼         ▼
 SCADA      MES
   └────┬────┘
        ▼
APPLICATION / API
        ▼
     .NET / C#
        ▼
  Blazor / Web GUI
```

Gazebo is a **simulation environment**, not this production stack.

MES/SCADA must never depend directly on Gazebo ECM, Gazebo System plugins, Siemens/Allen-Bradley APIs, Modbus register maps, OPC UA node IDs, or vendor SDKs.

### Diagram A — Real factory path — PARTIALLY IMPLEMENTED (Equipment + mock + OPC UA + Modbus)

```text
Actual machine / PLC
        │
Industrial Adapter          PARTIALLY IMPLEMENTED
  mock IMPLEMENTED
  OPC UA IMPLEMENTED
  Modbus IMPLEMENTED
  REST NOT IMPLEMENTED
  MQTT NOT IMPLEMENTED
  EtherNet/IP NOT IMPLEMENTED
  PROFINET NOT IMPLEMENTED
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
  REST NOT IMPLEMENTED
  MQTT NOT IMPLEMENTED
  EtherNet/IP NOT IMPLEMENTED
  PROFINET NOT IMPLEMENTED
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

## 2. Physical factory path — PARTIALLY IMPLEMENTED (adapter contract + mock + OPC UA + Modbus)

```text
Physical equipment
        ▼
Industrial adapter          PARTIALLY IMPLEMENTED (contract + mock + OPC UA + Modbus)
        ▼
Normalized Equipment        IMPLEMENTED (Phase 5)
        ▼
MES / SCADA                 PLANNED
```

Production OPC UA adapter: **IMPLEMENTED** (open62541 client + node mapping; ADR-025, ADR-026).
Production Modbus TCP adapter: **IMPLEMENTED** (libmodbus client + register mapping; ADR-036).
Production REST adapter: **NOT IMPLEMENTED** (slice 6E; ADR-037).
MQTT adapter: **NOT IMPLEMENTED** (slice 6F; ADR-038).
EtherNet/IP adapter: **NOT IMPLEMENTED** (slice 6G; ADR-039).
PROFINET: **NOT IMPLEMENTED** / investigation required (slice 6H; ADR-040). A TCP mock is not PROFINET.

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

## 4. Industrial adapter boundary — PARTIALLY IMPLEMENTED

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
  RestIndustrialAdapter     NOT IMPLEMENTED (6E)
  MqttIndustrialAdapter     NOT IMPLEMENTED (6F)
  EtherNetIpIndustrialAdapter NOT IMPLEMENTED (6G)
  ProfinetIndustrialAdapter NOT IMPLEMENTED (6H; investigation)
```

`IndustrialAdapter` (C++): `id()`, `protocol()`, `connectionState()`, `lastError()`, `connect()` / `disconnect()`, `equipment()` / `equipmentById()`, `poll()`.

`ConnectionState::Faulted` is a **communication** fault. `Equipment::fault()` is a **machine** fault.

Library `virtual_factory_industrial` links `virtual_factory_equipment`, **open62541** (OPC UA client), and **libmodbus** (Modbus TCP client). `IndustrialAdapter.hh` does not include open62541 or libmodbus. The Gazebo plugin does not link industrial, open62541, or libmodbus.

**One adapter instance = one industrial source/session.** Several OPC UA servers ⇒ several `OpcUaIndustrialAdapter` instances (ADR-026). Several Modbus TCP endpoints ⇒ several `ModbusIndustrialAdapter` instances (ADR-036). Planned: one REST origin, one MQTT **broker**, one EtherNet/IP device per instance. `connectionState()` is per-source. A faulted source does not take down equipment on other adapters. An adapter manager is **Phase 7**, not Phase 6.

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

REST remains **NOT IMPLEMENTED**. REST does not replace OPC UA or Modbus (ADR-013).

### 4.3 REST industrial gateway (6E) — PLANNED / NOT IMPLEMENTED (ADR-037)

HTTP **client** to one industrial gateway/vendor origin. Not a fieldbus. Not the future MES REST API. Candidate: libcurl. One instance = one origin. Tests: local HTTP fixture. Credentials stay in adapter config.

### 4.4 MQTT (6F) — PLANNED / NOT IMPLEMENTED (ADR-038)

MQTT **client** to one broker. Many machines via topic mappings. Subscribe telemetry/state/fault; publish commands. Do not embed a broker. Preferred candidate: Paho MQTT C. Broker/topic/QoS stay off `Equipment`. Drain on `poll()`. Explicit reconnect.

### 4.5 EtherNet/IP (6G) — PLANNED / NOT IMPLEMENTED (ADR-039)

CIP scanner/client. Not Modbus with another library. First subset: **explicit messaging**. Library must be recorded in ADR-039 before code. Do not claim implicit/cyclic I/O without library+test evidence. Local mock ≠ hardware certification.

### 4.6 PROFINET (6H) — PLANNED / NOT IMPLEMENTED / investigation (ADR-040)

IO-Controller / IO-Device, cyclic RT IO, naming, GSDML, diagnostics. A TCP mock is **not** PROFINET. p-net is an **IO-Device** stack, not a controller. Implement only if a production-capable path is approved; otherwise remain PLANNED or specify gateway/commercial integration.

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

Adding PLCs later must **not** require a new C++ class or a rebuild of MES. Configuration/UI/API (UI later) creates another protocol adapter instance (`OpcUaIndustrialAdapter`, `ModbusIndustrialAdapter`, later REST) plus mappings, then assigns the resulting `Equipment` to plant/line/work center and a responsible supervisor.

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
| 6 | Industrial Adapter Layer | **PARTIALLY IMPLEMENTED** (slices 6A–6D done; 6E–6H not implemented) |
| 7 | MES Core + Resource Management | **PLANNED / NOT IMPLEMENTED** |
| 8 | SCADA / Operational HMI | **PLANNED / NOT IMPLEMENTED** |
| 9 | Security & Authorization | **PLANNED / NOT IMPLEMENTED** |
| 10 | Real Factory Integration | **PLANNED / NOT IMPLEMENTED** |
| 11 | Commercial Hardening & Enterprise Integration | **PLANNED / NOT IMPLEMENTED** |

Phase 6 is **IN PROGRESS**. Slices **6A–6D** are done. **6E REST → 6F MQTT → 6G EtherNet/IP → 6H PROFINET** remain. Official numbering stays Phases **1–11** (ADR-041). Do not mark the whole phase complete. Do not start Phase 7.

Do not use Stage 0–25 or other retired numbering as the live plan.

---

## 21. What this file is not

This is not a second SoT. The active PDF is generated from `docs/source/MES_SCADA_Virtual_Factory_Source_of_Truth.md` via `docs/source/generate-sot-pdf.sh`. The previous PDF is in `docs/archive/` and is **not** authoritative. Do not silently drift.
