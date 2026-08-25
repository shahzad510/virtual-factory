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
| 6 | Industrial Adapter Layer | **IN PROGRESS** (slices **6A–6E** done. **6F–6H NOT IMPLEMENTED**) |
| 7 | MES Core + Resource Management | **NOT STARTED** |
| 8 | SCADA / Operational HMI | **PLANNED** |
| 9 | Security & Authorization | **PLANNED** |
| 10 | Real Factory Integration | **PLANNED** |
| 11 | Commercial Hardening & Enterprise Integration | **PLANNED** |

Nothing else is IN PROGRESS. Do not implement Phase 7 until instructed. Do not start slice 6F until separately approved.

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
- **Major components:** `IndustrialAdapter`; `MockIndustrialAdapter`; `OpcUaIndustrialAdapter` (open62541); `ModbusIndustrialAdapter` (libmodbus); `RestIndustrialAdapter` (libcurl); planned MQTT/EtherNet/IP; PROFINET only if a valid production path is approved.
- **Dependencies:** Phase 5.
- **Official numbering:** Phase 6 of Phases **1–11**. Implementation uses slices **6A–6H** (ADR-041), not extra official phases.
- **Slice status:**
  - **6A** architecture + mock — **DONE**
  - **6B** OPC UA / open62541 — **DONE**
  - **6C** OPC UA multi-server validation (10–200 simulated in-process servers) — **VALIDATED** (not production capacity)
  - **6D** Modbus TCP / libmodbus — **DONE**
  - **6E** REST industrial gateway (HTTP client, libcurl) — **DONE** (localhost fixture; not vendor certification)
  - **6F** MQTT (one broker connection; Paho C candidate) — **NOT IMPLEMENTED**
  - **6G** EtherNet/IP (CIP scanner; library ADR before code) — **NOT IMPLEMENTED**
  - **6H** PROFINET investigation; no fake stack — **NOT IMPLEMENTED**
- **Order:** 6A → 6B → 6C → 6D → 6E → 6F → 6G → 6H → Phase 6 final audit → Phase 7.
- **Status:** **IN PROGRESS**. Do not start Phase 7 until this Phase 6 scope is complete or 6H is explicitly marked by an approved ADR.

## Phase 7 — MES Core + Resource Management

- **Objective:** Production execution and manufacturing information: plant configuration, onboarding, resource readiness, orders, materials, quality, genealogy, OEE, downtime, scheduling, and operational analytics. Siemens Opcenter is a **capability benchmark only** (ADR-035).
- **Major components (none in repo — all PLANNED):**
  1. Configurable plant hierarchy (enterprise/site/building/floor/area/line/cell/work center — data, not C++ inheritance; ADR-027)
  2. Dynamic PLC/equipment onboarding via configuration (another protocol adapter instance + mappings; no new C++ class per PLC; ADR-028)
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
- **Dependencies:** Phase 5 equipment contract; realistically Phase 6 for live equipment. Do not put this logic in `Equipment` or `IndustrialAdapter`.
- **Status:** **NOT STARTED** / **NOT IMPLEMENTED**. Do not implement until explicitly instructed.

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
| OPC UA nodes, Modbus maps, UAExpert | Phase 6 | OPC UA **DONE** (6B, ADR-025). 6C **VALIDATED** (simulated). Modbus TCP **DONE** (6D, ADR-036). REST **DONE** (6E, ADR-037; local fixture ≠ vendor certification). MQTT/EtherNet/IP/PROFINET **NOT IMPLEMENTED**. UAExpert diagnostic only. |
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
- Starting 6F MQTT without a separate implementation approval
- Calling a TCP mock “PROFINET support”
