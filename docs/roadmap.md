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
| 6 | Industrial Adapter Layer | **IN PROGRESS** (architecture + mock + OPC UA **DONE**. Modbus/REST **NOT IMPLEMENTED**) |
| 7 | MES Core | **NOT STARTED** |
| 8 | SCADA / Operational HMI | **PLANNED** |
| 9 | Security & Authorization | **PLANNED** |
| 10 | Real Factory Integration | **PLANNED** |
| 11 | Commercial Hardening | **PLANNED** |

Nothing else is IN PROGRESS. Do not implement Phase 7 until instructed.

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
- **Major components:** `IndustrialAdapter`; `MockIndustrialAdapter`; `OpcUaIndustrialAdapter` (open62541); future Modbus / REST.
- **Dependencies:** Phase 5.
- **Status:** **IN PROGRESS**. Architecture + mock + OPC UA **DONE** (including multi-server via multiple adapter instances, ADR-026). **NOT IMPLEMENTED:** Modbus, REST.

## Phase 7 — MES Core

- **Objective:** Production execution and manufacturing information, including **Resource Management** so an order is scheduled only when required resources are capable *and* available (ADR-024).
- **Major components (none in repo — all PLANNED):**
  1. Production order management  
  2. Product / process definitions  
  3. Routing and operations  
  4. Resource Management — equipment, work centers, machines, lines, personnel, tools, material readiness; capability vs availability; allocation; reservation  
  5. Scheduling  
  6. Dispatching  
  7. Production execution tracking  
  8. Material management  
  9. Quality management  
  10. Sampling and test results  
  11. Traceability / genealogy  
  12. Downtime  
  13. OEE  
  14. Maintenance integration (constraints, not a full CMMS)  
  15. Production reporting  
- **Dependencies:** Phase 5 equipment contract; realistically Phase 6 for live equipment. Do not put this logic in `Equipment` or `IndustrialAdapter`.
- **Status:** **NOT STARTED** / **NOT IMPLEMENTED**. Do not implement until explicitly instructed.

## Phase 8 — SCADA / Operational HMI

- **Objective:** Live state, alarms, trends, commands, role-specific operational screens.
- **Major components:** SCADA services; later Blazor views.
- **Dependencies:** Phases 5–6; GUI may land with this phase.
- **Status:** **PLANNED**.

## Phase 9 — Security & Authorization

- **Objective:** Authentication, RBAC, audit, least privilege.
- **Major components:** identity, roles, permissions, audit log.
- **Dependencies:** API/GUI surface.
- **Status:** **PLANNED**.

## Phase 10 — Real Factory Integration

- **Objective:** Real PLCs/sensors/machines through adapters without rewriting MES.
- **Major components:** production adapter configs, device mapping.
- **Dependencies:** Phase 6 production adapters, Phase 7.
- **Status:** **PLANNED**.

## Phase 11 — Commercial Hardening

- **Objective:** Configuration, deployment, observability, testing, hardening, backups, support.
- **Major components:** CI, packaging, monitoring, operational docs.
- **Dependencies:** working platform as applicable.
- **Status:** **PLANNED**.

---

## Capability backlog (not extra phases)

| Capability | Maps toward | Status |
| --- | --- | --- |
| Product sensor SEN-001 | Equipment after Phase 5 | **DEFERRED** |
| OpenPLC / interlocks / auto-manual | Behind adapters / PLC | **DEFERRED** |
| OPC UA nodes, Modbus maps, UAExpert | Phase 6 | OPC UA adapter **DONE** (ADR-025). Modbus **NOT IMPLEMENTED**. UAExpert diagnostic only. |
| Multi-machine line via GenericEquipment | After contract | **DEFERRED** |
| Resource Management (capability vs availability, work centers, allocation/reservation) | MES Phase 7 | **PLANNED** (ADR-024; not implemented) |
| OEE, downtime, scheduling, event catalog | MES Phase 7+ | **PLANNED** |
| Automated MES tests | Phase 11 / test debt | **PLANNED** |
| Containerized demo | Phase 11 | **PLANNED** |

---

## Out of scope until named

- Plugin timers as SCADA
- Gazebo as the industrial adapter
- Product GUI in C++
- RBAC as scattered if-statements
- Implementing Phase 7 without an explicit instruction
