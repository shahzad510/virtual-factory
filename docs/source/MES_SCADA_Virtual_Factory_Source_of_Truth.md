# MES + SCADA + Virtual Factory — Source of Truth

<p class="subtitle">Architecture, phases, industrial integration, and MES design.<br>
Revision 2026-08-28 (Phase 6 slice 6H PROFINET investigation complete; native adapter NOT IMPLEMENTED). Living architecture: change this document <em>intentionally</em>, then regenerate the PDF.</p>

**How to update this PDF:** edit `docs/source/MES_SCADA_Virtual_Factory_Source_of_Truth.md`, then run `docs/source/generate-sot-pdf.sh`. Do not maintain a second competing Source of Truth.

**Documentation hierarchy:** SoT PDF → `implementation-status.md` → `architecture.md` → `decisions.md` → `roadmap.md` → `CHANGELOG.md` → `docs/README.md`.

**Status labels used throughout:** **IMPLEMENTED** · **PARTIALLY IMPLEMENTED** · **VALIDATED** · **PLANNED** · **NOT IMPLEMENTED**. Planned text is architecture, not code.

---

## 1. Purpose

This document is the project's **only active architectural Source of Truth**. It preserves terminology, phase structure, industrial-integration rules, and the intended MES so later work can resume without inventing a second architecture.

Gazebo is a **simulation plant**. C++ plugins are **not** the GUI. Industrial adapters are **not** the MES. SCADA is **not** MES.

When implementation and this document disagree: **current source code** is implementation reality; **this PDF** is architectural authority. Resolve the conflict with an ADR and an intentional SoT update. Do not silently drift.

---

## 2. Executive architecture

```text
PHYSICAL FACTORY / GAZEBO PLANT
  PLCs, sensors, machines, simulated equipment
                 |
                 v
        INDUSTRIAL ADAPTERS          protocol-oriented
   mock | OPC UA | Modbus | REST | MQTT | EtherNet/IP | PROFINET
                 |
                 v
     NORMALIZED EQUIPMENT MODEL
                 |
        +--------+--------+
        |                 |
        v                 v
       MES              SCADA
        |                 |
        +--------+--------+
                 |
                 v
        APPLICATION / API / GUI
           (.NET / Blazor later)
```

The Industrial Adapter is the boundary between heterogeneous factory equipment and the application. MES must not understand every PLC vendor, NodeId, Modbus map, MQTT topic, HTTP path, CIP tag, PROFINET slot, or Gazebo ECM.

Adapters are **protocol-oriented**, not machine-oriented. Do not create `PumpOpcUaAdapter` / `RobotModbusAdapter`. Represent machines as `GenericEquipment` plus mappings.

**One adapter instance = one industrial communication source/session** (ADR-026, ADR-036, ADR-041):

- OPC UA: one server/endpoint / one `UA_Client`
- Modbus TCP: one TCP endpoint/session
- REST: one HTTP origin/gateway
- MQTT: one **broker** connection (many machines via topic mappings)
- EtherNet/IP: one device/session
- PROFINET: relationship TBD after investigation (ADR-040); do not assume TCP-equivalence

N sources ⇒ N adapter instances. An adapter **manager/registry** is **not** part of Phase 6; onboarding of large numbers of adapters is Phase 7 (ADR-028).

---

## 3. What we are building

| Layer | Role | Status |
| --- | --- | --- |
| Virtual factory | Gazebo representation of a plant | **IMPLEMENTED** (Phases 1–4) |
| Equipment control | C++ Gazebo System plugins | **IMPLEMENTED** (conveyor example) |
| Equipment abstraction | Open-ended technical asset model | **IMPLEMENTED** (Phase 5) |
| Industrial adapter layer | Protocol connectors into Equipment | **PARTIALLY IMPLEMENTED** (Phase 6 slices 6A–6G **IMPLEMENTED**/ **VALIDATED**; 6H **NOT IMPLEMENTED**) |
| MES | Production execution and manufacturing information | **NOT IMPLEMENTED** (Phase 7) |
| SCADA / HMI | Live monitoring, alarms, operator control | **NOT IMPLEMENTED** (Phase 8) |
| Security | Authentication, RBAC, audit | **NOT IMPLEMENTED** (Phase 9) |
| Real factory | Production devices through adapters | **NOT IMPLEMENTED** (Phase 10) |
| Commercial / enterprise | Hardening, ERP/PLM/QMS/CMMS, advanced intelligence | **NOT IMPLEMENTED** (Phase 11) |

---

## 4. Official phases (1–11)

This numbering is authoritative. Do not revive Stage 0–25, sensor-first, or gateway-only plans.

| Phase | Name | Status |
| ---: | --- | --- |
| 1 | Factory Foundation | **IMPLEMENTED** |
| 2 | Equipment Plugin Foundation | **IMPLEMENTED** |
| 3 | Conveyor Control | **IMPLEMENTED** |
| 4 | Product Motion | **IMPLEMENTED** |
| 5 | Industrial Equipment Abstraction | **IMPLEMENTED** |
| 6 | Industrial Adapter Layer | **IN PROGRESS** — slices 6A–6G done; 6H **NOT IMPLEMENTED** (see §7) |
| 7 | MES Core + Resource Management | **PLANNED / NOT IMPLEMENTED** |
| 8 | SCADA / Operational HMI | **PLANNED / NOT IMPLEMENTED** |
| 9 | Security & Authorization | **PLANNED / NOT IMPLEMENTED** |
| 10 | Real Factory Integration | **PLANNED / NOT IMPLEMENTED** |
| 11 | Commercial Hardening & Enterprise Integration | **PLANNED / NOT IMPLEMENTED** |

Capability mapping (all **PLANNED** unless noted):

- Dynamic plant configuration, PLC/equipment onboarding, hierarchy, work centers, resource management, orders, materials, scrap, quality, genealogy, OEE, downtime, scheduling, personnel, tools, events, operational analytics → **Phase 7**
- Live visualization, shop-floor execution views, alarm HMI → **Phase 8**
- Identity, authentication, RBAC, organizational permissions → **Phase 9**
- Production-grade connection of real PLCs/machines (no MES rewrite) → **Phase 10**
- ERP/PLM/QMS/CMMS, advanced what-if/optimization, cost accounting, deployment/observability → **Phase 11**

Official numbering remains Phases **1–11**. Phase 6 is divided into **implementation slices 6A–6H** (not extra official phases).

Do not implement Phase 7 until Phase 6 scope is completed or an approved architectural decision explicitly marks remaining slices (especially 6H PROFINET). Remaining Phase 6 slice: **6H PROFINET**. Order: 6A → 6B → 6C → 6D → 6E → 6F → 6G → 6H → Phase 6 final audit → Phase 7.

---

## 5. Current implementation (facts)

**IMPLEMENTED**

- Gazebo plant: world, floor, CV-001, PRODUCT-001, conveyor plugin, kinematic product motion (`dt` in seconds).
- `Equipment` / `GenericEquipment` / `Conveyor` (Gazebo example only).
- `IndustrialAdapter` / `MockIndustrialAdapter` / `OpcUaIndustrialAdapter` (open62541 1.4.0-rc2) / `ModbusIndustrialAdapter` (libmodbus 3.1.10 TCP) / `RestIndustrialAdapter` (libcurl 8.5.0 + nlohmann/json 3.11.3) / `MqttIndustrialAdapter` (Paho MQTT C 1.3.13, MQTT 3.1.1) / `EtherNetIpIndustrialAdapter` (libplctag 2.7.1 explicit messaging).
- One OPC UA adapter instance = one `UA_Client` = one endpoint (ADR-026).
- One Modbus TCP adapter instance = one TCP session (ADR-036).
- One REST adapter instance = one HTTP origin (ADR-037).
- One MQTT adapter instance = one broker/session (ADR-038).
- One EtherNet/IP adapter instance = one device/session (ADR-039).
- Multi-source composition of adapter instances (no mega-adapter).
- OPC UA multi-server **validation** at 10, 25, 50, 100, and 200 **simulated in-process** servers (slice 6C). **VALIDATED** under those test conditions. **Not** production hardware proof. **Not** “unlimited PLCs.” **Not** factory capacity certification.

**NOT IMPLEMENTED**

- MES, resource management, scheduler, OEE engine, materials, scrap, quality, genealogy, analytics engine
- Dynamic PLC management UI/API; adapter manager/registry
- SCADA, GUI, authentication/RBAC, database, application API
- PROFINET (6H)
- Machine-specific classes such as `Robot.hh`, `Pump.hh`, `Oven.hh`

Development Start/Stop at simulation updates 1000/5000 is a **temporary test**, not industrial control.

---

## 6. Equipment model (Phase 5) — IMPLEMENTED

Equipment is an **open-ended technical asset**, not a catalog of C++ machine classes.

A plant may contain conveyors, robots, pumps, motors, CNC, welding cells, ovens, furnaces, mixers, inspection, packaging, manual stations, test equipment, AGVs/AMRs, storage, custom machines, and **unknown future types**. Represent ordinary machines as `GenericEquipment` with identity, type metadata, capabilities, commands, telemetry, state, and fault. Add a specialized C++ class only when behaviour cannot be expressed that way (`Conveyor` is the Gazebo exception).

MES/SCADA must not `switch` on `type()`.

Equipment concepts: identity, type metadata, operational state (Stopped/Running), commands (`execute`), telemetry `{name, value, unit}`, machine fault, adapter connection state **as a separate comms signal**.

Do **not** put scheduling, allocation, reservation, capacity, or plant hierarchy on `Equipment`.

---

## 7. Industrial adapters (Phase 6) — PARTIALLY IMPLEMENTED

Adapters are **protocol-oriented**. Not: `PumpPLCAdapter`, `RobotPLCAdapter`, `ConveyorPLCAdapter`, `SiemensAdapter`.

Official Phase **6** remains “Industrial Adapter Layer.” Implementation work inside Phase 6 uses slices **6A–6H** (ADR-041). These slices are **not** additional official phases.

| Slice | Scope | Status |
| --- | --- | --- |
| **6A** | Adapter architecture (`IndustrialAdapter`) + `MockIndustrialAdapter` | **IMPLEMENTED** / **TESTED** |
| **6B** | `OpcUaIndustrialAdapter` (open62541 client) | **IMPLEMENTED** / **TESTED** |
| **6C** | OPC UA multi-server scalability validation (10–200 simulated in-process servers) | **VALIDATED** under those test conditions. **Not** production capacity certification |
| **6D** | `ModbusIndustrialAdapter` (libmodbus TCP) | **IMPLEMENTED** / **TESTED** (working tree; preserve one TCP session per instance) |
| **6E** | REST industrial **gateway** adapter (HTTP client) | **IMPLEMENTED** / **TESTED** (localhost HTTP fixture; **not** vendor certification) |
| **6F** | MQTT industrial adapter (broker client) | **IMPLEMENTED** / **TESTED** (localhost Mosquitto; **not** vendor certification). Multi-equipment scale **VALIDATED** at 10/50/100/200 mappings + 2×50 brokers (see `docs/mqtt-scalability-test.md`; **not** production capacity) |
| **6G** | EtherNet/IP industrial adapter (libplctag explicit CIP tag messaging) | **IMPLEMENTED** / **TESTED** (local `ab_server`; **not** hardware certification). Two-device isolation **VALIDATED** under test conditions |
| **6H** | PROFINET — investigation **COMPLETE**; native adapter **NOT IMPLEMENTED** (ADR-040) | **NOT IMPLEMENTED** |

Intended order: 6A → 6B → 6C → 6D → 6E → 6F → 6G → 6H → Phase 6 final audit → Phase 7.

Communication fault (`IndustrialAdapter::ConnectionState::Faulted`) is distinct from machine/process fault (`Equipment::fault()`). Reconnect remains **explicit** `connect()` after Faulted. Local test fixtures are **validation only**, never production certification.

Do not put NodeId, coil/register, HTTP, MQTT topic/broker, CIP, or PROFINET concepts on `Equipment`.

### 7.1 OPC UA (6B, 6C) — IMPLEMENTED / VALIDATED

```text
one OpcUaIndustrialAdapter instance
        |
        +-- one UA_Client
        |
        +-- one OPC UA server / endpoint
        |
        +-- mapped GenericEquipment
```

N OPC UA PLCs ⇒ N adapter instances. Mapping is C++ config today (`OpcUaAdapterConfig`). Future MES onboarding (Phase 7) stores equivalent configuration. Test security (`SecurityPolicy#None`, anonymous localhost) is **DEVELOPMENT ONLY**. Slice 6C **VALIDATED** 10–200 simulated in-process servers; that is **not** hardware or factory-capacity proof.

### 7.2 Modbus TCP (6D) — IMPLEMENTED / TESTED

One `ModbusIndustrialAdapter` = one TCP `host:port` session (libmodbus). N endpoints ⇒ N instances. Register/coil mappings stay in adapter config. Unsecured localhost slave is **DEVELOPMENT ONLY**. Isolation at 2 and 4 localhost endpoints is correctness validation, **not** capacity certification.

### 7.3 REST (6E) — IMPLEMENTED / TESTED (ADR-013, ADR-037)

REST is an **application HTTP API**, not a native PLC fieldbus. The adapter is an HTTP **client** to one industrial gateway/vendor origin (libcurl). JSON mapping uses nlohmann/json inside the industrial implementation. It is **not** the future MES REST API. One instance = one HTTP origin (scheme + host + port + optional base path). N origins ⇒ N instances. Credentials stay in adapter config, not on Equipment. TLS verification is on by default. Tests: local HTTP fixture — **DEVELOPMENT/INTEGRATION VALIDATION ONLY**, not vendor API certification.

### 7.4 MQTT (6F) — IMPLEMENTED / TESTED (ADR-038)

MQTT is **broker pub/sub**. The adapter is an MQTT **client** (Eclipse Paho MQTT C MQTTAsync, MQTT 3.1.1). One instance = **one broker connection**. Multiple machines/devices are topic mappings (`GenericEquipment`; e.g. PLC-001 is an instance identity, not a C++ class). Subscribe telemetry/state/fault; publish commands. Do not embed a broker in this application. Broker/topic/QoS/retain stay in adapter config. Equipment must not expose MQTT types. `poll()` is bounded (default 50 ms; bounded receive queue with latest-value drop-oldest when full). Explicit `connect()` after Faulted restores subscriptions. Communication Faulted ≠ `Equipment::fault()`. Local Mosquitto tests are **DEVELOPMENT/INTEGRATION VALIDATION ONLY**, not cloud/vendor certification. Multi-equipment scale was **VALIDATED** at 10/50/100/200 mappings on one broker and 2×50 across two brokers under those test conditions — **not** a production capacity claim. Sparkplug B, MQTT 5 architecture, wildcard subscriptions, and the Phase 7 MES event bus are **NOT IMPLEMENTED**.

### 7.5 EtherNet/IP (6G) — IMPLEMENTED / TESTED (ADR-039)

CIP over Ethernet via **libplctag v2.7.1** (commit `bdb10aeaf4f374cec7ae4e66887446dedf952dc1`, MPL-2.0). The adapter is a **scanner/client**, not “Modbus with another library.” **Explicit messaging / symbolic tag read/write only.** Class 1 implicit/cyclic I/O (UDP 2222) is **NOT IMPLEMENTED**. Do not claim implicit/cyclic I/O unless genuinely implemented and tested. One instance = one device/session (host + port + CIP path + plc type). Several logical machines on one device are `GenericEquipment` mappings (`PLC-001` is configuration identity, not a C++ class). Private `eip_session` wrapper; public headers do not include `libplctag.h`. `connect()` after `Faulted` is explicit. Communication Faulted ≠ `Equipment::fault()`. Tests: libplctag **`ab_server`** ControlLogix emulator — **DEVELOPMENT/INTEGRATION VALIDATION ONLY**, not Allen-Bradley/Rockwell hardware certification. Two-device isolation **VALIDATED** under those test conditions — **not** production capacity.

### 7.6 PROFINET (6H) — investigation COMPLETE; NOT IMPLEMENTED (ADR-040)

PROFINET IO uses **IO-Controller / IO-Device** roles, Ethernet Layer 2 **cyclic process data**, DCP, GSDML, slot/submodule addressing, diagnostics, and alarms. It is **not** TCP/UDP request/response like OPC UA, Modbus, REST, MQTT, or EtherNet/IP.

**Investigation completed 2026-08-28.** The industrial adapter must act as an **IO-Controller** (client to IO-Devices). **No credible open-source C/C++ IO-Controller stack** is available for embedding in `virtual_factory_industrial` without GPL-only bindings, PI membership gates, or commercial licenses.

| Candidate | Role | Outcome |
| --- | --- | --- |
| RT-Labs **p-net** | IO-Device | GPLv3; **not** IO-Controller — rejected for adapter |
| **PROFINET Community Stack** (PI) | Integration toolkit | PI membership; not public OSS drop-in |
| **profinet-py** | IO-Controller (Python) | GPL-3.0; not C++ industrial library |
| Commercial stacks | IO-Controller | Future path — separate ADR |

**`ProfinetIndustrialAdapter` is NOT IMPLEMENTED.** Do not fake PROFINET with raw TCP/UDP. **Phase 6 remains IN PROGRESS** (6A–6G done).

**Future topology (if approved):** one adapter ≈ one IO-Controller on one Ethernet interface; multiple IO-Devices per controller; `PLC-001`/`PLC-002` as `GenericEquipment` mappings to process data — not C++ classes. `IndustrialAdapter` contract sufficient; private cyclic thread inside adapter. Linux: raw Ethernet, often root/CAP_NET_RAW; dedicated NIC.

**Alternatives without native 6H:** gateway (PROFINET device → OPC UA / Modbus / REST adapters already in 6B–6E).

Phase 7 onboarding (ADR-028) instantiates these protocol adapters from configuration. No new C++ adapter class per PLC. No Phase 6 adapter manager.

---

## 8. MES vs Equipment vs Adapter

| Concern | Owner | Status |
| --- | --- | --- |
| Technical asset, telemetry, commands, machine fault | `Equipment` | **IMPLEMENTED** |
| Protocol, endpoint, comms lifecycle | `IndustrialAdapter` | **PARTIALLY IMPLEMENTED** |
| Capability, availability, allocation, reservation, capacity, readiness | MES Resource Management | **PLANNED** |
| Orders, materials, quality, genealogy, OEE, schedule | MES | **PLANNED** |
| Live HMI, alarms, operator commands | SCADA | **PLANNED** |
| Login, RBAC | Phase 9 | **PLANNED** |

A machine may be technically Running and still **MES-unavailable** (reserved, maintenance window, quality hold). Do not confuse `Equipment::fault()` with MES availability.

---

## 9. Configurable plant hierarchy — PLANNED (Phase 7)

Organizational structure is **data**, not C++ inheritance.

Minimum conceptual tree (all node types optional/configurable):

```text
Enterprise
  └── Site / Plant
        └── Building
              └── Floor
                    └── Area
                          └── Production line / assembly line / process cell
                                └── Work Center
                                      ├── Equipment resources
                                      ├── Personnel
                                      ├── Tools / fixtures
                                      └── Other resources
```

Valid alternatives include Plant → Area → Process Cell → Work Center, or Plant → Building → Floor → Line → Work Center. The MES stores parent/child location entities and assignments. “Assembly line” is not the only allowed production structure.

Work Center is a **production capability**, not necessarily one machine. Example: WC-100 may combine Robot-17, Welder-03, Operator-22, Fixture-4, with constraints (qualification, fixture, maintenance, material, capacity).

---

## 10. Dynamic PLC / equipment onboarding — PLANNED (Phase 7)

Real scenario: a plant has 100 PLCs; six months later a new line adds 100 more. The administrator must **not** modify and recompile C++ merely to add those PLCs.

Future configuration/UI/API (Phase 7, UI later with Phase 8/9) must allow:

- Add a PLC / industrial source (identity, protocol, endpoint, connection parameters)
- Define equipment represented by that source and node/register mappings
- Assign plant / building / floor / area / line / work center
- Assign responsible supervisor/manager
- Assign capabilities and related resources
- Enable/disable connection, test connectivity, monitor health

For OPC UA:

```text
PLC / OPC UA server
        v
OpcUaIndustrialAdapter instance (created from config)
        v
Normalized Equipment
        v
MES (resource + location assignment)
```

No new C++ adapter class per PLC. Phase 9 later restricts who may change configuration.

---

## 11. Supervisors and responsibility — PLANNED

The MES data model must record responsibility/ownership, for example Plant Manager, Area Manager, Production Manager, Line Supervisor, Work Center Supervisor, Maintenance Supervisor, Quality Supervisor. A person may be responsible for multiple resources.

**Authentication/RBAC is Phase 9 and is not implemented.** Operational qualification (may this operator weld?) is a Phase 7 resource concept and is distinct from login permission.

---

## 12. MES resource availability — PLANNED

Equipment technical state (examples): Stopped, Running, Faulted.

MES availability (examples): Available, Unavailable, Reserved, Allocated, Maintenance, Blocked, Quality Hold, Scheduled, Decommissioned.

These are different axes.

---

## 13. Resource readiness before dispatch — PLANNED

Before dispatch, MES evaluates required resources. Result is **READY** or **HOLD** with **specific reasons**, not a generic “resource unavailable.”

Example holds: equipment unavailable; equipment faulted; maintenance active; operator unavailable; qualification expired; material shortage; material quality hold; tool unavailable; work-center capacity exhausted; line committed; incompatible setup; resource reserved by another order.

---

## 14. Production orders — PLANNED (Phase 7)

Orders, work orders, operations, routing, BOM, bill of process, dependencies, required resources/capabilities/materials/personnel/tools, quantities, planned/actual times, priority, due date, status, holds, completion, partial completion, scrap, rework.

Do not implement in this documentation pass.

---

## 15. Materials — PLANNED (Phase 7)

First-class MES capability: raw materials, components, subassemblies, WIP, finished goods, consumables, packaging.

Track identity, lot/batch, serial where applicable, quantity/unit, location, status, quality status, expiry, reservations, consumption, production, scrap, return, transfer.

Material availability participates in scheduling and readiness.

---

## 16. Scrap and waste — PLANNED (Phase 7)

Track planned / good / scrap / rework quantities; scrap reason and category; operation, work center, equipment, material lot, operator, timestamp, order, product.

Do **not** compute factory efficiency from machine runtime alone. Scrap and material waste must be visible.

---

## 17. OEE — PLANNED (Phase 7)

OEE = Availability × Performance × Quality.

- Availability: planned production time vs downtime (planned, unplanned, maintenance, faults, configured changeover).
- Performance: ideal vs actual cycle time, counts, speed losses.
- Quality: total / good / rejected / scrap / rework.

Provide OEE views at Equipment, Work Center, Line, Area, and Plant **using defined aggregation rules**. Do **not** blindly average OEE percentages across machines. Prefer count-weighted or time-weighted rollups of the underlying components, documented per KPI.

Keep OEE mathematically distinct from broader plant efficiency (yield, material variance, labor utilization, on-time completion).

---

## 18. Downtime — PLANNED (Phase 7)

Downtime events with start/end/duration, resource, reason tree (mechanical, electrical, controls, material, operator, quality, changeover, setup, maintenance, waiting, starvation, blockage, unknown), category, order/operation, operator, source, acknowledgement, comments.

---

## 19. Quality — PLANNED (Phase 7)

Inspection plans, characteristics, specifications, sampling, measurements, pass/fail, defects, nonconformance, rework, scrap, corrective actions, quality holds, release. SPC (control charts, limits, trends, out-of-control detection) is a **planned** capability, not Phase 7 code today.

---

## 20. Genealogy / traceability — PLANNED (Phase 7)

Forward and backward genealogy: finished product → consumed lots, operators, equipment, work center, inspections; and material lot → products that consumed it. Required for recall and root-cause analysis.

---

## 21. Events and analytics layers — PLANNED

Distinguish:

1. **Raw industrial data** (OPC UA nodes, Modbus registers, REST/JSON behind adapters; later MQTT/CIP/PROFINET)
2. **Contextualized MES facts** (order, operation, work center, material, person)
3. **Aggregated analytics** (hour/day/week/month/year KPIs)

Dashboards must not compute everything from raw tags.

Preserve historical events such as EquipmentStateChanged, DowntimeStarted/Ended, ProductionStarted/Completed, MaterialConsumed/Produced, ScrapRecorded, InspectionCompleted, NonconformanceCreated, ResourceAllocated/Released, OrderDispatched/Completed — attributable to time, plant, line, work center, equipment, order, operation, material, person where applicable.

Analytics periods: real-time, hourly, daily, weekly, monthly, yearly. Each period compares to target, previous period, plan, and historical baseline.

Example KPIs (all **PLANNED**): Material Yield, Scrap Rate, First Pass Yield, Rework Rate, Material Variance, Production Yield, Resource Utilization, Labor Utilization, Capacity Utilization, OEE, Throughput, On-Time Completion.

Loss analysis (downtime, speed, scrap, rework, material, labor, capacity, changeover) may later estimate cost via ERP. Phase 7 does **not** require a finance engine.

---

## 22. Scheduling — PLANNED (Phase 7)

Planning, scheduling, dispatching, rescheduling using priority, due date, resource/equipment/work-center capability, material, operator, qualification, tooling, maintenance windows, changeover, sequence-dependent constraints, capacity, operation dependencies.

The scheduler should explain why a schedule was chosen or why an order cannot be scheduled. Advanced what-if / optimization may extend into Phase 11.

Bottleneck analysis: which work center/equipment/material/labor/tool is constraining, why, duration, affected orders.

---

## 23. Maintenance, personnel, tools — PLANNED

MES observes maintenance availability (planned windows, unplanned, status, related downtime). A full CMMS is **not** Phase 7; CMMS integration is Phase 11.

Personnel: roles, qualifications, certifications, shifts, availability, assignment, labor utilization. Distinct from Phase 9 RBAC.

Tools/fixtures: availability, assignment, reservation, compatibility, usage, calibration/maintenance status.

---

## 24. SCADA (Phase 8) — PLANNED

Live state, commands, alarms/ack, trends, role-specific operational screens. Consumes Equipment + MES context. Not a substitute for MES. Not a C++ desktop inside the Gazebo plugin.

---

## 25. Security (Phase 9) — PLANNED

Authentication, RBAC, audit, least privilege. Supervisor is the floor-management role name. Do not scatter permission if-statements in equipment/adapters.

---

## 26. Real factory (Phase 10) — PLANNED

Connect real PLCs/sensors/machines through production adapters without rewriting MES. Uses the same one-adapter-per-source/session rule (MQTT: one broker connection per adapter).

---

## 27. Commercial hardening & enterprise (Phase 11) — PLANNED

Configuration, deployment, observability, testing, backups, support. ERP/PLM/QMS/CMMS integration where appropriate. Advanced manufacturing intelligence beyond core MES analytics.

---

## 28. Siemens Opcenter — capability benchmark only

Siemens Opcenter is an **external industry benchmark** for production execution, tracking, resource utilization, qualifications, materials, work orders, scheduling, constraints, visibility, OEE, downtime, quality, NCR, root-cause, genealogy, SPC, KPIs, closed-loop feedback, and enterprise integration.

This project does **not** claim Opcenter feature parity and does **not** copy proprietary architecture. Those capabilities are **PLANNED** industry-aligned goals, not implemented software.

---

## 29. GUI and application stack — PLANNED

C++ remains for Gazebo plugins and industrial/performance code. Application GUI is web/.NET; Blazor is the documented candidate. Do not build the product MES/SCADA GUI as a large C++ desktop.

---

## 30. Simulation notes (IMPLEMENTED)

CV-001 is static; PRODUCT-001 is dynamic. Product motion is pose integration, not a physically accurate belt. Geometry and the 1000/5000 development timers remain as previously decided until deliberately changed.

Gazebo plugin is **not** an `IndustrialAdapter`.

---

## 31. Decisions to preserve

1. Gazebo is the virtual plant, not MES/SCADA.
2. C++ is for simulation and industrial/performance code, not the main MES GUI.
3. Adapters isolate MES/SCADA from vendors and protocols.
4. Multiple protocols; REST is a gateway fallback, not a replacement and not the MES API.
5. MES consumes normalized Equipment and MES resources, not NodeIds, topics, HTTP paths, or Gazebo ECM.
6. One adapter instance per industrial source/session (OPC UA server, Modbus TCP endpoint, REST origin, MQTT broker, EtherNet/IP device).
7. Equipment ≠ MES resource.
8. Plant hierarchy is configurable data.
9. Adding PLCs is configuration, not a new C++ class per PLC.
10. Resource readiness reports specific constraint reasons.
11. Materials and scrap participate in readiness and analytics.
12. OEE is distinct from broader efficiency; do not blindly average OEE %.
13. Analytics use contextualized events, not raw tags alone.
14. Genealogy is forward and backward.
15. Opcenter is a benchmark, not a claim of parity.
16. There is one SoT. Update it on purpose.
17. Development timers are not industrial control.
18. Security includes authentication, authorization, and audit (Phase 9).
19. Supervisor is the floor-management role name.
20. Do not implement Phase 7 until Phase 6 (slices 6A–6H, with 6H honestly marked) is complete or an approved ADR says otherwise.
21. Phase 6 slices 6A–6H are implementation labels inside official Phase 6; they are not extra official phases.
22. PROFINET must not be faked; p-net is an IO-Device stack, not an IO-Controller.

---

## 32. Next direction

**Now:** Phase 6 slice **6H investigation is complete**; native PROFINET adapter **NOT IMPLEMENTED**. Future native PROFINET requires a **new approved ADR** (commercial IO-Controller stack, PI Community Stack integration, or explicit gateway-only decision). **Do not start MES code.**

**Then:** Phase 6 final audit. **Only after that, when explicitly instructed:** Phase 7 MES Core + Resource Management, still consuming the existing Equipment and adapter contracts.

If a future choice conflicts with this document, revise the Source of Truth deliberately and regenerate this PDF.
