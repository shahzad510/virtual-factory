# Roadmap

Official implementation sequence: **SoT Phases 1–11** (see [`architecture.md`](architecture.md) §12).

This file is **not** a second architecture. It states what is DONE, IN PROGRESS, NEXT, PLANNED, or DEFERRED.

Older “Stage 0–25” numbering is **retired**. Useful leftover ideas are listed under [Capability backlog](#capability-backlog) without stage numbers.

---

## Implementation phases

| Phase | Name | Status |
| --- | --- | --- |
| 1 | Factory Foundation | **DONE** |
| 2 | Equipment Plugin Foundation | **DONE** |
| 3 | Conveyor Control | **DONE** |
| 4 | Product Motion | **DONE** (runtime verified 2026-08-22) |
| 5 | Industrial Equipment Abstraction | **NEXT** (not started) |
| 6 | Industrial Adapter Layer | **PLANNED** |
| 7 | MES Core | **PLANNED** |
| 8 | SCADA / Operational HMI | **PLANNED** |
| 9 | Security & Authorization | **PLANNED** |
| 10 | Real Factory Integration | **PLANNED** |
| 11 | Commercial Hardening | **PLANNED** |

Nothing is IN PROGRESS after the Phase 4 close-out.

---

## Phase notes

### DONE — Phases 1–4

- World, floor, static CV-001, PRODUCT-001 poses/geometry.
- `ConveyorSystem` plugin loads, configures, heartbeats.
- Start/Stop/SetSpeed; development timers 1000/5000.
- PRODUCT-001 ECM discovery; pose X += speed × dt_seconds.
- `dt` bug fixed and verified (`dt=0.001 s`; travel −1.5 m → 0.5 m).

### NEXT — Phase 5

Gazebo-independent equipment contract. Plugin implements it. No MES, no OPC UA, no Blazor in this phase.

### PLANNED — Phases 6–11

Adapters (OPC UA, Modbus, REST fallback, later MQTT/EtherNet/IP), MES, SCADA, RBAC, real hardware, hardening. Details and “done” gates: `architecture.md`.

---

## Capability backlog

Items that matter later but **must not** be mistaken for current work or for extra phases.

| Capability | Maps toward | Status |
| --- | --- | --- |
| Product sensor SEN-001 | Equipment/sensors after Phase 5 | **DEFERRED** (was old Stage 1; not required to close Phases 1–4) |
| OpenPLC / interlocks / auto-manual | Behind adapters / PLC, not inside Gazebo-as-MES | **DEFERRED** |
| Modbus register map, OPC UA nodes, UAExpert | Phase 6 (+ diagnostics) | **PLANNED** |
| Multi-machine line (robot, process, inspection, pack) | After contract exists | **DEFERRED** |
| Production events catalog, OEE, downtime, maintenance, scheduling, scenarios | MES Phases 7+ | **PLANNED** as MES increments |
| Automated MES/quality/traceability tests | Phase 11 and earlier test debt | **PLANNED** (`tests/` empty today) |
| Containerized demo environment | Phase 11 | **PLANNED** |

---

## Explicitly out of scope until named

- Starting Phase 5 without owner instruction
- Treating plugin timers as SCADA
- Treating Gazebo as the industrial adapter
- Building the product GUI in C++
- Implementing RBAC as scattered if-statements
