# Architecture Decision Records

Format: ID, title, status, date, context, decision, consequences, alternatives.

**Status values:** Accepted | Superseded | Deprecated.

Architectural authority remains the [SoT PDF](MES_SCADA_Virtual_Factory_Source_of_Truth.pdf). ADRs record *why* a choice was made so it is not silently replaced.

---

## ADR-001 — Gazebo represents the physical factory

- **Status:** Accepted
- **Date:** 2026-08-16

**Context:** The project needs a plant that can generate manufacturing-like events before real equipment is available.

**Decision:** Gazebo Sim is the virtual physical plant (machines, conveyors, products, sensors, interactions). Gazebo is not the PLC, SCADA, or MES.

**Consequences:** Plant changes live under `gazebo/`. Higher layers must not `#include` gz-sim once an equipment contract exists.

**Alternatives:** Unity/Unreal (rejected: not aligned with robotics/industrial sim tooling already in use); skip simulation (rejected: unsafe/expensive for early MES work).

---

## ADR-002 — C++ for simulation and industrial/performance-sensitive code

- **Status:** Accepted
- **Date:** 2026-08-16

**Context:** Gazebo systems and many industrial stacks are C++. The team needs to own those interfaces.

**Decision:** C++ is the language for Gazebo System plugins and for high-performance or hardware-facing industrial components. It is not the language of the main MES/SCADA GUI (see ADR-011).

**Consequences:** Plugin code targets C++17 and gz-sim 8. GUI/application services use .NET later.

---

## ADR-003 — PLC remains separate from Gazebo

- **Status:** Accepted
- **Date:** 2026-08-16

**Context:** Real factories put control in PLCs, not in the 3D engine.

**Decision:** Treat PLC as machine controller and Gazebo as plant. OpenPLC (or a real PLC) may appear later *behind* adapters, not inside `ConveyorSystem` as the MES.

**Consequences:** Plugin Start/Stop is temporary equipment behavior, not the long-term PLC program.

---

## ADR-004 — Protocols are separated from machine logic

- **Status:** Accepted
- **Date:** 2026-08-16

**Context:** Binding conveyor logic to Modbus or OPC UA would freeze vendor choice into the core.

**Decision:** Machine/equipment behavior must not depend directly on Modbus, OPC UA, or REST. An abstraction (equipment contract + adapters) sits in between.

**Consequences:** Phase 5 before protocol adapters (Phase 6).

---

## ADR-005 — OPC UA is a primary information-model interface

- **Status:** Accepted
- **Date:** 2026-08-16

**Context:** Need a structured industrial information model for equipment, states, faults, measurements, commands.

**Decision:** OPC UA will be a main interface. Initial implementation: **open62541**. OPC UA is not assumed to be the only protocol (see ADR-012, ADR-013).

**Consequences:** Do not start open62541 until Phase 6.

---

## ADR-006 — UAExpert is diagnostic only

- **Status:** Accepted
- **Date:** 2026-08-16

**Context:** Need a way to browse/read/write OPC UA during development.

**Decision:** UAExpert is a diagnostic tool, not part of the runtime architecture.

---

## ADR-007 — SCADA and MES have different responsibilities

- **Status:** Accepted
- **Date:** 2026-08-16

**Context:** Combining HMI and manufacturing execution into one blob produces unmaintainable software.

**Decision:** SCADA = operator monitoring and control. MES = manufacturing management and production information. They share industrial core/contract; they are not interchangeable.

---

## ADR-008 — Incremental implementation with phase gates

- **Status:** Accepted
- **Date:** 2026-08-16

**Context:** Skipping to GUI/auth/MES before the plant and contract exist creates fake progress.

**Decision:** Implement SoT phases in order. A phase must meet its “done” criteria (see `architecture.md`) before the next major layer is introduced.

**Consequences:** No Blazor, RBAC, or adapters before Phase 5 unless the owner explicitly overrides.

---

## ADR-009 — Avoid unnecessary dependencies

- **Status:** Accepted
- **Date:** 2026-08-16

**Context:** Framework fashion is a common failure mode.

**Decision:** Introduce a technology only when it solves a stated requirement.

---

## ADR-010 — Virtual factory should generate realistic manufacturing events

- **Status:** Accepted
- **Date:** 2026-08-16

**Context:** Pretty 3D is not the product.

**Decision:** The plant’s value is events: states, faults, downtime, quality, movement, traceability inputs for MES testing.

**Consequences:** Phase 4 kinematic motion is an acceptable first event source; physics-perfect belts are not the gate.

---

## ADR-011 — Main GUI is web / .NET (Blazor candidate), not a large C++ desktop

- **Status:** Accepted
- **Date:** 2026-08-22 (captured from SoT §12)

**Context:** MES/SCADA UIs need tables, dashboards, forms, RBAC, reporting.

**Decision:** Application GUI uses a modern web stack. Preferred direction: **ASP.NET Core + Blazor** (or equivalent .NET web UI). C++ remains for simulation/industrial/performance code.

**Consequences:** Do not start a Qt/wx/custom C++ HMI as the product GUI.

**Alternatives:** Pure C++ GUI (rejected for enterprise UI cost); unspecified JS SPA (not chosen; .NET/Blazor is the documented candidate). Changing this requires an explicit ADR and SoT update.

---

## ADR-012 — Industrial adapter layer is the equipment boundary

- **Status:** Accepted
- **Date:** 2026-08-22 (SoT §§9–11)

**Context:** Thousands of vendors/protocols. MES must not know Siemens vs Allen-Bradley.

**Decision:** Adapters sit immediately below equipment. They translate protocol/vendor data into the normalized contract. The adapter layer is not the MES.

**Consequences:** Phase 6 implements adapters against the Phase 5 contract.

---

## ADR-013 — REST is a fallback integration path

- **Status:** Accepted
- **Date:** 2026-08-22 (SoT §10)

**Context:** Some devices will never speak OPC UA or Modbus cleanly.

**Decision:** Unsupported device → vendor/gateway → REST → adapter boundary → normalized model. REST does not replace industrial protocols.

---

## ADR-014 — Supervisor is the floor-management role name

- **Status:** Accepted
- **Date:** 2026-08-22 (SoT §14)

**Context:** Role naming drifts (floor manager vs supervisor).

**Decision:** Use **Supervisor** for the floor-management role. Full RBAC is Phase 9. Do not scatter permission ifs.

---

## ADR-015 — SoT Phases 1–11 are the official implementation sequence

- **Status:** Accepted
- **Date:** 2026-08-22

**Context:** Docs used three numberings: SoT Phases 1–11, roadmap Stages 0–25, and a 14-step handoff list.

**Decision:** Implementation phase = SoT Phase 1–11. Older stages become a **capability backlog** on `roadmap.md` without competing phase numbers.

**Consequences:** “Current phase” in status/README uses SoT numbers only.

---

## ADR-016 — Static conveyor; kinematic product motion

- **Status:** Accepted
- **Date:** 2026-08-17 / 2026-08-22

**Context:** A dynamic conveyor body fell/reacted undesirably. Need a stationary machine structure.

**Decision:** CV-001 is `<static>true</static>`. Product remains dynamic. Phase 4 moves PRODUCT-001 by ECM Pose integration (`X += speed * dt_s`), not a physically accurate belt. Belt `SetLinearVelocity` is a control hook, not a visual belt texture.

**Consequences:** Do not “fix” static by making the whole conveyor dynamic without a new ADR. Physics-based transport is optional later.

---

## ADR-017 — Update 1000/5000 Start/Stop is a development test

- **Status:** Accepted
- **Date:** 2026-08-18

**Context:** Need to demonstrate conveyor lifecycle before SCADA/API exist.

**Decision:** `PreUpdate` calls `Start()` at 1000 updates and `Stop()` at 5000. This is temporary. Production commands will come from control/SCADA/MES/adapters.

**Consequences:** Must not be treated as the industrial control architecture.

---

## ADR-018 — Simulation `dt` is converted to seconds

- **Status:** Accepted
- **Date:** 2026-08-22

**Context:** `gz::sim::UpdateInfo::dt` is `std::chrono::steady_clock::duration` (nanoseconds on this platform). `_info.dt.count()` is tick count, not seconds. At 0.5 m/s and 1 ms steps that would teleport the product.

**Decision:** Always use `std::chrono::duration<double>(_info.dt).count()` for pose integration. Verified headless: `dt=0.001 s`, 2.0 m travel in 4.0 s at 0.5 m/s.

**Consequences:** Do not reintroduce `.count()` as a time in seconds.
