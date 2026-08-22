# Architecture

How the Virtual Factory MES + SCADA platform is structured, and how the current Gazebo plant sits inside that structure.

**Authority:** [`MES_SCADA_Virtual_Factory_Source_of_Truth.pdf`](MES_SCADA_Virtual_Factory_Source_of_Truth.pdf) defines the architecture. This file explains it technically and records how implementation maps onto it.

**Current reality:** [`implementation-status.md`](implementation-status.md). If this file describes a layer as intended, that layer is not claimed to exist unless status says so.

---

## 1. Purpose

Build industrial MES + SCADA software that can run against a **simulated factory** (Gazebo) now and **real equipment** later **without rewriting the MES/SCADA core**.

That requires strict layering:

```text
PHYSICAL / SIMULATED EQUIPMENT
        ↓
INDUSTRIAL ADAPTER LAYER
        ↓
NORMALIZED INDUSTRIAL DATA / COMMANDS
        ↓
MES / SCADA CORE
        ↓
APPLICATION SERVICES / API
        ↓
GUI / WEB CLIENT
```

Gazebo is the safe plant. It is not the MES. The C++ plugin is simulated equipment behavior. It is not the GUI and not the adapter layer for real hardware.

---

## 2. Why these boundaries exist

| Boundary | Why |
| --- | --- |
| Gazebo = plant | Same MES/SCADA must eventually talk to real PLCs/sensors/machines. |
| C++ plugin ≠ MES | Equipment behavior stays replaceable. |
| Equipment contract | Higher software sees identity, state, commands, telemetry — not SDF, ECM, or vendor tags. |
| Industrial adapters | Hide Siemens vs Allen-Bradley vs Modbus vs REST. |
| REST is a fallback | Unsupported devices enter through a gateway/API, still normalized at the adapter boundary. REST does not replace OPC UA/Modbus. |
| MES ≠ SCADA | MES: production/quality/materials/traceability. SCADA: live monitor/control/alarms. |
| API ≠ GUI | Web/.NET UI can change without touching industrial runtime. |
| C++ vs .NET | C++ for simulation, protocols, performance. .NET/Blazor for enterprise UI (dashboards, RBAC, forms). |

---

## 3. Intended system layers

```text
USERS
  |
  v
.NET / Blazor GUI
  |
  v
Application / API
  |
  +----------------+
  |                |
  v                v
 MES              SCADA
  |                |
  +-------+--------+
          |
          v
  INDUSTRIAL CORE
          |
          v
 EQUIPMENT CONTRACT
          |
          v
 INDUSTRIAL ADAPTERS
    |       |       |
    v       v       v
 OPC UA   Modbus   REST
    |       |       |
    +-------+-------+
            |
            v
        EQUIPMENT
       /    |     \
     PLC  Sensors Machines
```

**Today only the bottom of the simulation path exists** (Gazebo + ConveyorSystem). Everything from Equipment Contract upward is PLANNED.

---

## 4. Simulation path vs real-factory path

Both paths must present the **same equipment contract** to Industrial Core / MES / SCADA.

### Simulation path (current plant)

```text
Industrial Core          PLANNED
      |
Equipment Contract       PLANNED (Phase 5)
      |
Gazebo / simulation
implementation           PLANNED as an adapter
      |
ConveyorSystem           IMPLEMENTED (Phases 2–4)
      |
Gazebo Sim 8             IMPLEMENTED (Phase 1)
      |
Virtual factory
CV-001, PRODUCT-001      IMPLEMENTED
```

### Real factory path (future)

```text
Industrial Core
      |
Equipment Contract
      |
Industrial Adapter (OPC UA / Modbus / REST / …)
      |
PLC / sensor / machine
```

The plugin is the **simulation implementation** of conveyor behavior. A future “Gazebo adapter” may sit between the contract and the plugin. Do not collapse MES into `ConveyorSystem.cc`.

---

## 5. Component relationships (current code)

```text
gz sim
  factory.sdf
    factory_floor (static)
    include model://conveyor  → CV-001 (static)
      plugin libConveyorSystem.so
        virtual_factory::ConveyorSystem
          entity_       → CV-001 model
          beltEntity_   → link "belt"
          productEntity_→ PRODUCT-001 (ECM Name search)
          running_, speed_, fault_, updateCount_
    include model://product   → PRODUCT-001 at (-1.5, 0, 1.25)
```

Control flow today:

1. `Configure` — identify model and belt.
2. `PreUpdate` each unpaused step — find product; development Start@1000 / Stop@5000; if running, `X += speed * dt_seconds`; command belt linear velocity; heartbeat.

Data flow today: **in-process only** (plugin members + ECM Pose). No tags, no bus, no API.

---

## 6. Equipment contract (Phase 5 — planned)

Normalized concepts the rest of the system should use, independent of vendor and of Gazebo:

- identity (e.g. `CV-001`)
- running / stopped
- speed (m/s)
- fault
- commands: start, stop, set-speed
- later: mode, alarms, counters, timestamps

Example: `conveyor.speed`, `conveyor.running`, `conveyor.fault` mean the same whether the backing adapter is Gazebo, Siemens, or Allen-Bradley.

---

## 7. Industrial adapters (Phase 6 — planned)

Adapters sit **immediately below the equipment boundary**. They are not the MES.

| Adapter | Role |
| --- | --- |
| OPC UA | Structured information model; open62541 intended |
| Modbus TCP/RTU | Registers/coils from PLC or devices |
| REST/HTTP | Vendor/gateway fallback |
| Later | MQTT, EtherNet/IP, vendor SDK |

UAExpert is a **diagnostic client**, not a core component (ADR-006).

---

## 8. MES architecture (Phase 7 — planned)

MES owns production execution and manufacturing information, including:

production orders, scheduling/routing, materials/lots, execution (start/pause/stop, scrap, downtime), quality (sampling, holds, release/reject), equipment utilization, maintenance, traceability/genealogy, alarms as production events, KPIs/OEE, work instructions.

MES consumes the **equipment contract**, not Gazebo entities and not raw PLC tags.

---

## 9. SCADA architecture (Phase 8 — planned)

SCADA owns operational monitoring and control: live equipment state, operator commands, alarms/ack, trends, role-specific HMI.

SCADA is not a substitute for MES and not a C++ desktop built inside the plugin.

---

## 10. API, GUI, security, persistence, deployment

| Layer | Intent | Status |
| --- | --- | --- |
| Application/API | .NET/C# services over MES/SCADA | PLANNED (with Phase 8/7) |
| GUI | Web UI; **Blazor** is the preferred candidate | PLANNED (SoT §12) |
| AuthN/AuthZ | Central RBAC, not scattered ifs. Supervisor = floor role. | PLANNED Phase 9 |
| Persistence | MES database, events, audit | PLANNED with Phase 7+ |
| Deployment | Reproducible env, later containers/hardening | PLANNED Phase 11 |

Roles (SoT): System Administrator, Plant Manager, Production Manager, Supervisor, Technician, Quality Officer, Operator, Maintenance. Briefing also named warehouse and viewer/auditor — reconcile at Phase 9, do not implement now.

---

## 11. Current Gazebo implementation (what exists)

**Gazebo Sim 8.15.0**, APIs from `/usr/include/gz/sim8`.

| Piece | Detail |
| --- | --- |
| World | `gazebo/worlds/phase1/factory.sdf`, ODE step 0.001 s |
| CV-001 | static; frame + belt; plugin filename `libConveyorSystem.so` |
| PRODUCT-001 | 0.4×0.4×0.3 m; world pose `-1.5 0 1.25 0 0 0` |
| Plugin | `virtual_factory::ConveyorSystem` : `gz::sim::System`, `ISystemConfigure`, `ISystemPreUpdate` |
| Build | `gazebo/plugins/conveyor/CMakeLists.txt` → `build/libConveyorSystem.so` |

Product motion:

```text
dt_s = duration<double>(_info.dt).count()   // seconds
product_x += speed * dt_s                   // while running_
```

This is a **controlled simulation abstraction**, not a physically accurate conveyor. Development Start/Stop uses update counts 1000 and 5000.

Verified APIs: `Model::LinkByName`, `Link::SetLinearVelocity`, `ECM::SetComponentData<components::Pose>(...)`. Do not assume other Gazebo versions.

---

## 12. Implementation phases (SoT 1–11)

These are the **official** implementation phases. Capability ideas from older Stage 0–25 lists belong on the [roadmap](roadmap.md) backlog, not as a second phase scale.

### Phase 1 — Factory Foundation

| | |
| --- | --- |
| **Purpose** | Create the virtual plant: world, floor, CV-001, PRODUCT-001, dimensions and poses. |
| **Components** | `factory.sdf`, `gazebo/models/conveyor/`, `gazebo/models/product/` |
| **Inputs** | Gazebo Sim 8, SDF 1.9 |
| **Outputs** | Loadable world with conveyor and product |
| **Dependencies** | None |
| **Done means** | World loads; geometry/poses match SoT §4–5 |
| **Status** | **COMPLETE** |
| **Future work** | Additional machines only in later capability work, not required to close Phase 1 |

### Phase 2 — Equipment Plugin Foundation

| | |
| --- | --- |
| **Purpose** | Load `libConveyorSystem.so`; identify model and belt; `Configure` + `PreUpdate` lifecycle. |
| **Components** | `ConveyorSystem.hh/.cc`, CMake, plugin element in conveyor SDF |
| **Inputs** | Phase 1 models |
| **Outputs** | Plugin configured against CV-001 / belt |
| **Dependencies** | Phase 1, gz-sim8, gz-plugin2 |
| **Done means** | Plugin loads; belt entity found; heartbeat possible |
| **Status** | **COMPLETE** |
| **Future work** | Shared library install path (optional) |

### Phase 3 — Conveyor Control

| | |
| --- | --- |
| **Purpose** | Start/Stop/SetSpeed, running/speed state, development timing test, heartbeat. |
| **Components** | `Start()`, `Stop()`, `SetSpeed()`, `running_`, `speed_`, update 1000/5000 |
| **Inputs** | Phase 2 plugin |
| **Outputs** | Conveyor lifecycle in simulation |
| **Dependencies** | Phase 2 |
| **Done means** | Start/Stop observable in logs; default speed 0.5 m/s |
| **Status** | **COMPLETE** (timers still development-only) |
| **Future work** | Replace timers with external commands after Phase 5+ |

### Phase 4 — Product Motion

| | |
| --- | --- |
| **Purpose** | Discover PRODUCT-001 via ECM; advance Pose in +X while the conveyor runs. |
| **Components** | `productEntity_`, Name scan, Pose read/write, `dt` in seconds |
| **Inputs** | Phase 3 running/speed |
| **Outputs** | Product translates along the belt at commanded speed |
| **Dependencies** | Phase 3 |
| **Done means** | Headless or GUI run shows ~0.5 m/s travel; `dt` in seconds |
| **Status** | **COMPLETE / RUNTIME VERIFIED** (2026-08-22, 6000 iterations) |
| **Future work** | Physics-based transport later if needed; not required to close Phase 4 |

### Phase 5 — Industrial Equipment Abstraction

| | |
| --- | --- |
| **Purpose** | Move from a Gazebo-only mental model to an equipment abstraction that adapters can back. |
| **Components** | Equipment identity/state/command contract; plugin implements it |
| **Inputs** | Phase 4 behavior |
| **Outputs** | Contract usable without `#include` of gz-sim in higher layers |
| **Dependencies** | Phase 4 |
| **Done means** | Conveyor state/commands expressed independently of ECM |
| **Status** | **NEXT / NOT STARTED** |
| **Future work** | Mapping table from contract to Gazebo |

### Phase 6 — Industrial Adapter Layer

| | |
| --- | --- |
| **Purpose** | Protocol connectors (OPC UA, Modbus, REST, later MQTT/EtherNet/IP/vendor) exposing the same contract. |
| **Components** | Adapter interface + at least a simulation adapter; protocol adapters incrementally |
| **Inputs** | Phase 5 contract |
| **Outputs** | Normalized data/commands from heterogeneous devices |
| **Dependencies** | Phase 5 |
| **Done means** | One non-Gazebo path or a clearly defined adapter interface plus simulation adapter |
| **Status** | **PLANNED / NOT STARTED** |
| **Future work** | OpenPLC may appear as equipment behind Modbus, not as the MES |

### Phase 7 — MES Core

| | |
| --- | --- |
| **Purpose** | Production orders, work centers, materials, execution, quality, traceability, feedback. |
| **Components** | MES domain services and persistence |
| **Inputs** | Equipment contract / industrial core |
| **Outputs** | Production records and execution APIs |
| **Dependencies** | Phase 5; realistically Phase 6 for live equipment |
| **Done means** | Orders can be created and related to equipment events without talking to Gazebo directly |
| **Status** | **PLANNED / NOT STARTED** |
| **Future work** | Full MES scope in SoT §13 is multi-increment |

### Phase 8 — SCADA / Operational HMI

| | |
| --- | --- |
| **Purpose** | Live state, alarms, trends, commands, role-specific operational screens. |
| **Components** | SCADA services + (with GUI) Blazor views |
| **Inputs** | Equipment contract, alarms |
| **Outputs** | Operator monitoring/control |
| **Dependencies** | Phase 5–6; GUI may land with this phase |
| **Done means** | Operator can see CV-001 running/speed and issue start/stop through the stack, not plugin timers |
| **Status** | **PLANNED / NOT STARTED** |
| **Future work** | Replace 1000/5000 test |

### Phase 9 — Security & Authorization

| | |
| --- | --- |
| **Purpose** | Authentication, RBAC, audit, least privilege. |
| **Components** | Identity, roles, permissions, audit log |
| **Inputs** | API/GUI |
| **Outputs** | Enforced access by role (Supervisor, Operator, …) |
| **Dependencies** | Phase 8 (or API surface) |
| **Done means** | Central authorization, not per-screen if-statements |
| **Status** | **PLANNED / NOT STARTED** |
| **Future work** | Enterprise IdP later |

### Phase 10 — Real Factory Integration

| | |
| --- | --- |
| **Purpose** | Real PLCs/sensors/machines through adapters without rewriting MES domain. |
| **Components** | Production adapter configs, device mapping |
| **Inputs** | Phases 6–9 |
| **Outputs** | Same MES/SCADA against real I/O |
| **Dependencies** | Phase 6, 7 |
| **Done means** | At least one real device path proven |
| **Status** | **PLANNED / NOT STARTED** |
| **Future work** | Site-specific commissioning |

### Phase 11 — Commercial Hardening

| | |
| --- | --- |
| **Purpose** | Configuration, deployment, observability, testing, security hardening, backups, support. |
| **Components** | CI, packaging, docs, monitoring |
| **Inputs** | Working platform |
| **Outputs** | Supportable system |
| **Dependencies** | Phases 7–10 as applicable |
| **Done means** | Repeatable deploy and test evidence |
| **Status** | **PLANNED / NOT STARTED** |
| **Future work** | Ongoing |

---

## 13. What this file is not

This file is not a second SoT. If implementation forces an architectural change, record it in [`decisions.md`](decisions.md) and update the SoT PDF **intentionally**. Do not silently drift.
