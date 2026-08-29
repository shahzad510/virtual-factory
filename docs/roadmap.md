# Roadmap

Official sequence: **SoT Phases 1–11** only.

This file is **not** a second architecture and **not** evidence of implementation. See [`implementation-status.md`](implementation-status.md) for what exists.

**Do not trust** [`archive/`](archive/) for the live plan.

Retired numbering (do not revive): Stage 0–25, old Phase 0–10, sensor-first or gateway-only roadmaps.

---

## Summary

| Phase | Name | Status |
| --- | --- | --- |
| 1 | Factory Foundation | **DONE** |
| 2 | Equipment Plugin Foundation | **DONE** |
| 3 | Conveyor Control | **DONE** |
| 4 | Product Motion | **DONE** |
| 5 | Industrial Equipment Abstraction | **DONE** |
| 6 | Industrial Adapter Layer | **COMPLETE** (6A–6G implemented/tested; 6H supported via gateway; native PN deferred) |
| 7 | MES Core + Resource Management | **NOT STARTED** |
| 8 | SCADA / Operational HMI | **PLANNED** |
| 9 | Security & Authorization | **PLANNED** |
| 10 | Real Factory Integration | **PLANNED** |
| 11 | Commercial Hardening & Enterprise Integration | **PLANNED** |

Nothing else is IN PROGRESS. Phase 6 is **COMPLETE** (ICP adapter foundation). **ICP product (ICP-1) NOT STARTED.** Phase 7 MES Core **NOT STARTED**. Do not implement without explicit slice approval.

---

## Product 1 — Industrial Connectivity Platform (ICP)

- **Status:** **PLANNED** — adapter foundation **COMPLETE** (Phase 6); product **NOT STARTED** (ADR-042, ADR-044).
- **Objective:** Standalone industrial connectivity product: adapters, runtime, config, northbound CIC API, **ICP Designer GUI**.
- **Must work without MES.**
- **Implementation slices (not official SoT phase numbers):**

| Slice | Scope | Status |
| --- | --- | --- |
| **ICP-1A** | AdapterManager, PollScheduler, LiveStateCache | **NOT STARTED** |
| **ICP-1B** | Persistent configuration storage | **NOT STARTED** |
| **ICP-1C** | CIC v1 northbound API (gRPC/REST/stream) | **NOT STARTED** |
| **ICP-1D** | Command gateway, industrial events | **NOT STARTED** |
| **ICP-1E** | Standalone deployable package | **NOT STARTED** |
| **ICP-1F** | **ICP Designer** GUI (drag/drop/configure/connect/deploy) | **NOT STARTED** |

- **Detail:** `docs/icp-product-architecture.md`, `docs/connectivity-integration-contract.md`
- **PROFINET:** gateway-supported (6H); native deferred in ICP when approved.

---

## Phase 1 — Factory Foundation

- **Objective:** Virtual plant: world, floor, CV-001, PRODUCT-001, dimensions and poses.
- **Major components:** `factory.sdf`, conveyor and product models.
- **Dependencies:** none.
- **Status:** **DONE**.

## Phase 2 — Equipment Plugin Foundation

- **Objective:** Load `libConveyorSystem.so`; identify model and belt; Configure + PreUpdate.
- **Major components:** `ConveyorSystem`, plugin CMake, SDF plugin element.
- **Dependencies:** Phase 1, gz-sim8, gz-plugin2.
- **Status:** **DONE**.

## Phase 3 — Conveyor Control

- **Objective:** Start / Stop / SetSpeed, running/speed, development timing, heartbeat.
- **Major components:** plugin control API; timers 1000/5000 (development-only).
- **Dependencies:** Phase 2.
- **Status:** **DONE**.

## Phase 4 — Product Motion

- **Objective:** Discover PRODUCT-001; integrate pose in +X while running; `dt` in seconds.
- **Major components:** ECM Name scan, Pose write, duration→seconds.
- **Dependencies:** Phase 3.
- **Status:** **DONE** (runtime verified: −1.5 m → 0.5 m at 0.5 m/s).

## Phase 5 — Industrial Equipment Abstraction

- **Objective:** Gazebo-independent open-ended equipment model for later adapters and MES/SCADA.
- **Major components:** `Equipment`, `GenericEquipment`, `Conveyor` (simulation example only).
- **Dependencies:** Phase 4.
- **Status:** **DONE**. Not a closed Robot/Pump/Oven class tree.

## Phase 6 — Industrial Adapter Layer

- **Objective:** Protocol connectors that populate/control the normalized `Equipment` model without exposing vendor APIs to MES/SCADA.
- **Major components:** `IndustrialAdapter`; `MockIndustrialAdapter`; `OpcUaIndustrialAdapter` (open62541); `ModbusIndustrialAdapter` (libmodbus); `RestIndustrialAdapter` (libcurl); `MqttIndustrialAdapter` (Paho MQTT C); `EtherNetIpIndustrialAdapter` (libplctag); PROFINET only if a valid production path is approved.
- **Dependencies:** Phase 5.
- **Official numbering:** Phase 6 of Phases **1–11**. Implementation uses slices **6A–6H** (ADR-041), not extra official phases.
- **Slice status:**
  - **6A** architecture + mock — **DONE**
  - **6B** OPC UA / open62541 — **DONE**
  - **6C** OPC UA multi-server validation (10–200 simulated in-process servers) — **VALIDATED** (not production capacity)
  - **6D** Modbus TCP / libmodbus — **DONE**
  - **6E** REST industrial gateway (HTTP client, libcurl) — **DONE** (localhost fixture; not vendor certification)
  - **6F** MQTT (one broker connection; Paho MQTT C) — **DONE** (localhost Mosquitto; not vendor certification). Multi-equipment scale **VALIDATED** (10/50/100/200 + 2×50; not production capacity)
  - **6G** EtherNet/IP (libplctag explicit CIP tag messaging) — **DONE** (local `ab_server`; not hardware certification)
  - **6H** PROFINET — **SUPPORTED VIA GATEWAY** (ADR-040); native IO-Controller **DEFERRED**. See `docs/profinet-gateway-integration.md`
- **Order:** 6A → 6B → 6C → 6D → 6E → 6F → 6G → 6H → Phase 6 final audit → Phase 7.
- **Status:** **COMPLETE** (final audit 2026-08-28). Do not start Phase 7 until explicitly instructed.

## Phase 7 — MES Core + Resource Management (**Product 2**)

- **Objective:** Production execution and manufacturing information (MES Core product). Consumes industrial data **only via CIC** (ADR-043, ADR-045) — not protocol SDKs.
- **Major components (none in repo — all PLANNED):**
  1. Configurable plant hierarchy (enterprise/site/building/floor/area/line/cell/work center — data, not C++ inheritance; ADR-027)
  2. Dynamic equipment assignment via CIC (`equipmentId` → plant location); **ICP owns protocol onboarding** (ADR-028 amendment)
  3. Production order / work order / operation management
  4. Product / process definitions, routing, BOM, bill of process
  5. Resource Management — capability vs availability; allocation; reservation; capacity (ADR-024, ADR-030)
  6. Work centers as capability cells (not 1:1 with machines)
  7. Organizational responsibility (supervisors/managers as data; RBAC is Phase 9)
  8. Resource readiness with **specific hold reasons** (ADR-029)
  9. Planning, scheduling, dispatching, rescheduling (explain why an order cannot be scheduled)
  10. Production execution tracking
  11. Material management (raw, component, WIP, finished, consumable, packaging; lots/serials; ADR-031)
  12. Scrap / waste / rework
  13. Quality execution, NCR, sampling; SPC later
  14. Forward/backward genealogy (ADR-034)
  15. Downtime and reason trees
  16. OEE (distinct from broader efficiency; ADR-032)
  17. Contextualized events and operational analytics (real-time through yearly; ADR-033)
  18. Bottleneck identification
  19. Personnel / qualifications / shifts (not RBAC)
  20. Tools / fixtures
  21. Maintenance availability (not a full CMMS)
  22. Reporting / KPI dashboards (GUI later)
- **Dependencies:** Phase 5 equipment **contract** semantics; **ICP-1C (CIC v1) minimum** for live industrial data; simulated provider may precede. Do not link protocol SDKs into MES.
- **MES GUI:** core product component (ADR-045) — separate from ICP Designer.
- **Detail:** `docs/mes-core-product-architecture.md`
- **Status:** **NOT STARTED** / **NOT IMPLEMENTED**. Do not implement until explicitly instructed.

### Phase 7 implementation slices (proposed)

| Slice | Scope |
| --- | --- |
| **7A** | `IIndustrialDataProvider`, CIC client, simulated provider |
| **7B** | Plant hierarchy + MES equipment registry |
| **7C** | MES resource model (ADR-024, 030) |
| **7D+** | Orders, events, historian, scheduling, OEE, MES API/GUI — as approved |

## Phase 8 — SCADA / Operational HMI

- **Objective:** Live state, alarms, trends, commands, shop-floor execution views, role-specific operational screens.
- **Major components:** SCADA services; later Blazor views.
- **Dependencies:** Phases 5–6; GUI may land with this phase.
- **Status:** **PLANNED**.

## Phase 9 — Security & Authorization

- **Objective:** Authentication, RBAC, audit, least privilege.
- **Major components:** identity, roles, permissions, audit log.
- **Dependencies:** API/GUI surface.
- **Status:** **PLANNED**.

## Phase 10 — Real Factory Integration

- **Objective:** Real PLCs/sensors/machines through production adapters without rewriting MES. Reuse Phase 7 onboarding against real endpoints.
- **Major components:** production adapter configs, device mapping, production connection security.
- **Dependencies:** Phase 6 production adapters, Phase 7.
- **Status:** **PLANNED**.

## Phase 11 — Commercial Hardening & Enterprise Integration

- **Objective:** Configuration, deployment, observability, testing, hardening, backups, support; ERP/PLM/QMS/CMMS integration; advanced what-if / optimization beyond core MES analytics.
- **Major components:** CI, packaging, monitoring, operational docs, enterprise connectors, manufacturing intelligence.
- **Dependencies:** working platform as applicable.
- **Status:** **PLANNED**.

---

## Capability backlog (not extra phases)

| Capability | Maps toward | Status |
| --- | --- | --- |
| Product sensor SEN-001 | Equipment after Phase 5 | **DEFERRED** |
| OpenPLC / interlocks / auto-manual | Behind adapters / PLC | **DEFERRED** |
| OPC UA nodes, Modbus maps, UAExpert | Phase 6 | OPC UA **DONE** (6B, ADR-025). 6C **VALIDATED** (simulated). Modbus TCP **DONE** (6D, ADR-036). REST **DONE** (6E, ADR-037; local fixture ≠ vendor certification). MQTT **DONE** (6F, ADR-038; local Mosquitto ≠ vendor certification). EtherNet/IP **DONE** (6G, ADR-039; local `ab_server` ≠ hardware certification). PROFINET **SUPPORTED VIA GATEWAY** (6H, ADR-040; native deferred). UAExpert diagnostic only. |
| Multi-machine line via GenericEquipment | After contract | **DEFERRED** |
| Resource Management, plant hierarchy, dynamic PLC onboarding, readiness reasons | MES Phase 7 | **PLANNED** (ADR-024, 027–030; not implemented) |
| Materials, scrap, quality, genealogy | MES Phase 7 | **PLANNED** (ADR-031, 034; not implemented) |
| OEE, downtime, events/analytics, bottlenecks | MES Phase 7+ | **PLANNED** (ADR-032, 033; not implemented) |
| Siemens Opcenter-class capabilities | MES Phase 7–11 | **PLANNED** benchmark only (ADR-035; not parity, not implemented) |
| Automated MES tests | Phase 11 / test debt | **PLANNED** |
| ERP/PLM/QMS/CMMS, containerized demo | Phase 11 | **PLANNED** |

---

## Out of scope until named

- Plugin timers as SCADA
- Gazebo as the industrial adapter
- Product GUI in C++
- RBAC as scattered if-statements
- Implementing Phase 7 without an explicit instruction
- Calling a TCP mock “PROFINET support”
- Claiming native PROFINET IO-Controller support without an approved ADR and implementation
