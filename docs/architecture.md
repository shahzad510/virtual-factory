# Architecture

How the Virtual Factory platform is structured, and how the current Gazebo plant sits inside it.

**Architectural authority:** [`MES_SCADA_Virtual_Factory_Source_of_Truth.pdf`](MES_SCADA_Virtual_Factory_Source_of_Truth.pdf) — the **only** active Source of Truth.

**Implementation reality:** [`implementation-status.md`](implementation-status.md). Layers marked **PLANNED** do not exist in code.

**Do not trust** [`archive/`](archive/) for current architecture.

Status labels used below: **IMPLEMENTED** | **PARTIALLY IMPLEMENTED** | **PLANNED**.

---

## 1. Overall system

Intended production stack (SoT):

```text
PHYSICAL FACTORY
  PLCs / sensors / machines / other equipment
        │
        ▼
INDUSTRIAL ADAPTERS          (protocol-oriented)
  ┌─────┼──────┐
  ▼     ▼      ▼
OPC UA  Modbus REST
  └─────┼──────┘
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

### Diagram A — Real factory path — PARTIALLY IMPLEMENTED (Equipment + mock + OPC UA)

```text
Actual machine / PLC
        │
Industrial Adapter          PARTIALLY IMPLEMENTED
  mock IMPLEMENTED
  OPC UA IMPLEMENTED
  Modbus / REST NOT IMPLEMENTED
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
  Modbus / REST NOT IMPLEMENTED
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

## 2. Physical factory path — PARTIALLY IMPLEMENTED (adapter contract + mock + OPC UA)

```text
Physical equipment
        ▼
Industrial adapter          PARTIALLY IMPLEMENTED (contract + mock + OPC UA)
        ▼
Normalized Equipment        IMPLEMENTED (Phase 5)
        ▼
MES / SCADA                 PLANNED
```

Production OPC UA adapter: **IMPLEMENTED** (open62541 client + node mapping; ADR-025).
Production Modbus / REST adapters: **NOT IMPLEMENTED**.

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

Adapters sit below the equipment model. They are protocol-oriented (`opcua`, `modbus`, `rest`, `mock`), not one class per PLC vendor or machine type.

```text
MES / SCADA                 PLANNED
        ▼
Equipment                   IMPLEMENTED
        ▼
IndustrialAdapter           IMPLEMENTED (contract)
  MockIndustrialAdapter     IMPLEMENTED
  OpcUaIndustrialAdapter    IMPLEMENTED
  Modbus / REST             NOT IMPLEMENTED
```

`IndustrialAdapter` (C++): `id()`, `protocol()`, `connectionState()`, `lastError()`, `connect()` / `disconnect()`, `equipment()` / `equipmentById()`, `poll()`.

`ConnectionState::Faulted` is a **communication** fault. `Equipment::fault()` is a **machine** fault.

Library `virtual_factory_industrial` links `virtual_factory_equipment` and **open62541** (OPC UA client). `IndustrialAdapter.hh` does not include open62541. The Gazebo plugin does not link industrial or open62541.

**One adapter instance = one industrial source.** Several OPC UA servers ⇒ several `OpcUaIndustrialAdapter` instances (ADR-026). `connectionState()` is per-server. A faulted PLC does not take down equipment on other adapters.

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

REST remains a **fallback** for gateway/vendor APIs (ADR-013). REST does not replace OPC UA or Modbus.

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

MES consumes `Equipment` (and adapter connection state), not Gazebo or raw tags. It does **not** live inside `Equipment` or `IndustrialAdapter`.

### 10.1 Intended MES Core scope (all PLANNED)

1. Production order management  
2. Product / process definitions  
3. Routing and operations  
4. **Resource Management** (see §10.2–10.6)  
5. Scheduling  
6. Dispatching  
7. Production execution tracking  
8. Material management  
9. Quality management  
10. Sampling and test results  
11. Traceability / genealogy  
12. Downtime  
13. OEE  
14. Maintenance integration (constraint/boundary; not a full CMMS in Phase 7)  
15. Production reporting  

### 10.2 Equipment vs MES resource — PLANNED

| `Equipment` (Phase 5, IMPLEMENTED) | MES resource (Phase 7, PLANNED) |
| --- | --- |
| Technical asset | How production uses that asset (and others) |
| Identity, type, operational state, commands, telemetry, fault | Capability, availability, allocation, reservation, scheduling/maintenance constraints, relationships |

A physical machine may be both. Do **not** put scheduling, allocation, or reservation on `Equipment`.

### 10.3 Resource Management — PLANNED (ADR-024)

Resources may include equipment, machines, work centers, production lines, operators, technicians, materials, tools, fixtures, inspection resources, and other production resources.

**Capability** ≠ **availability.** Example: M-001 can make Product X but is unavailable; another machine is free but cannot make Product X. Both facts are required before dispatch.

Resource Management must eventually consider: capability, availability, allocation, reservation, utilization, maintenance state, fault state, scheduling commitments, resource dependencies, qualifications, material availability, tooling availability.

#### Diagram C — MES resource architecture — PLANNED

```text
Production Order
       │
       ▼
MES
 |
 +-- Order Management
 +-- Routing
 +-- Resource Management
 |     +-- Equipment
 |     +-- Work Centers
 |     +-- Personnel
 |     +-- Tools
 |     +-- Materials
 |     +-- Availability
 |     +-- Capability
 |     +-- Allocation
 |     +-- Reservation
 |
 +-- Scheduling
 +-- Dispatching
 +-- Production Tracking
 +-- Quality
 +-- Traceability
 +-- OEE
```

### 10.4 Work Centers — PLANNED

First-class MES concepts. Example:

```text
Plant: Factory-01
 └── Area: Assembly
      └── Work Center: WC-ASSY-01
            ├── Robot-001, Press-001, Conveyor-001   (as MES resources over Equipment)
            ├── Operator group
            └── Inspection station
```

No Work Center C++/classes exist in the repository.

### 10.5 Production-order resource readiness — PLANNED

```text
Production Order → required operations → required resources
  (equipment capability/availability, work center, material,
   operator qualification, tools, maintenance, quality)
        │
        ▼
 RESOURCE READY?
    │         │
   YES        NO
    │         │
 Schedule    HOLD
    │
 Dispatch → Production execution
```

### 10.6 Materials, personnel, tools, maintenance — PLANNED

- **Materials:** required vs available vs allocated vs reserved vs expected inbound (example: A 500 kg needed / 720 kg available → READY; B 250 / 180 → NOT READY). No inventory database yet.  
- **Personnel:** operator, technician, inspector, supervisor, maintenance technician; qualifications (e.g. CNC Operator Level 2). Distinct from Phase 9 authentication/RBAC.  
- **Tools:** availability, allocation, remaining life, compatibility.  
- **Maintenance:** scheduled windows, unavailable-for-maintenance, maintenance vs fault state. Full CMMS is out of Phase 7; MES observes the constraint boundary.

### 10.7 Production orders and quality — PLANNED

Orders: product, quantity, due date, routing, operations, required resources/materials, status, execution results.

Quality: sampling, test requests, measured values, pass/fail, results, operator/tester, timestamps, traceability to order/batch/unit.

---

## 11. SCADA boundary — PLANNED (Phase 8)

Live state, operator commands, alarms/ack, trends, role-specific HMI. Not a substitute for MES. Not a C++ desktop inside the plugin. **NOT STARTED.**

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

Real PLCs/sensors/machines through production adapters without rewriting MES. Depends on Phase 6 protocol adapters + Phase 7. **NOT STARTED.**

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
| 6 | Industrial Adapter Layer | **PARTIALLY IMPLEMENTED** (contract + mock + OPC UA; Modbus/REST not implemented) |
| 7 | MES Core | **PLANNED** |
| 8 | SCADA / Operational HMI | **PLANNED** |
| 9 | Security & Authorization | **PLANNED** |
| 10 | Real Factory Integration | **PLANNED** |
| 11 | Commercial Hardening | **PLANNED** |

Phase 6 is **IN PROGRESS**. Architecture + mock + OPC UA are done. Modbus and REST remain follow-on. Do not mark the whole phase complete.

Do not use Stage 0–25 or other retired numbering as the live plan.

---

## 21. What this file is not

This is not a second SoT. Resource Management (ADR-024) refines SoT Phase 7 in Markdown; the PDF is unchanged. Broader architectural change still requires an ADR and an intentional SoT PDF update. Do not silently drift.
