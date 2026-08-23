# Architecture Decision Records

Format: ID, title, status, date, context, decision, consequences, alternatives.

**Status values:** Accepted | Superseded | Deprecated.

Architectural authority remains the [SoT PDF](MES_SCADA_Virtual_Factory_Source_of_Truth.pdf). ADRs record *why* a choice was made so it is not silently replaced.

---

## ADR-001 — Gazebo represents the physical factory

- **Status:** Accepted
- **Date:** 2026-08-16

**Context:** The project needs a plant that can generate manufacturing-like events before real equipment is available.

**Decision:** Gazebo Sim is the **simulation** plant (machines, conveyors, products, sensors, interactions) used before real equipment is available. Gazebo is not the production architecture, not the PLC, not SCADA, and not MES. Real factories connect through industrial adapters (ADR-012, ADR-022).

**Consequences:** Plant changes live under `gazebo/`. Higher layers must not `#include` gz-sim once an equipment contract exists. Do not treat `ConveyorSystem` as an industrial adapter.

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

**Consequences:** Phase 6 is the correct place to adopt open62541. Realized in Phase 6C (ADR-025).

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

---

## ADR-019 — Equipment contract is Gazebo-independent

- **Status:** Accepted
- **Date:** 2026-08-23

**Context:** After Phase 4, conveyor identity, running, speed, and fault lived only inside `ConveyorSystem`, mixed with ECM, SDF, and development timers. MES/SCADA could not consume that state without depending on Gazebo.

**Decision:** Introduce a C++ equipment contract with no `gz` includes. `Conveyor` is one implementation; `ConveyorSystem` is Gazebo integration only.

Future OPC UA / Modbus / REST adapters (Phase 6) implement `Equipment`. They do not subclass `ConveyorSystem`.

**Consequences:** Plugin build links `virtual_factory_equipment`. Higher layers must include `virtual_factory/equipment/…`, never gz-sim, for equipment state. Production protocol adapters remain Phase 6 follow-on (mock adapter exists; OPC UA/Modbus/REST are not implemented).

**Note:** The first Phase 5 cut put `speed`/`setSpeed` on `Equipment`. That was too conveyor-specific. **ADR-020** corrects it.

**Alternatives:** Keep state only in the plugin (rejected: Gazebo leak); introduce a DI framework or large hierarchy (rejected: ADR-009); implement OPC UA in the same step (rejected: that is Phase 6).

---

## ADR-020 — Equipment is generic; type-specific quantities are telemetry

- **Status:** Accepted
- **Date:** 2026-08-23

**Context:** A factory has conveyors, pumps, mixers, ovens, robots, CNC, inspection cells, and more. Putting belt `speed` on `Equipment` would force every machine to pretend it has a conveyor speed. SoT §11 lists a normalized contract of state, commands, telemetry, and faults — not a conveyor API.

**Decision:**

- `Equipment` exposes identity, operational state (stopped/running), start/stop, fault, and `telemetry()` as `{name, value, unit}`.
- `Conveyor` keeps `speed()` / `setSpeed()` and publishes `"speed"` in m/s via telemetry.
- Type-specific quantities belong in telemetry or specialized APIs, not on `Equipment`. A new C++ class is **not** required for every machine type (see ADR-021).
- `ConveyorSystem` may call conveyor-specific speed because it is a conveyor simulator, not because `Equipment` requires speed.

**Consequences:** MES/SCADA can call `equipment.telemetry()` without knowing the machine type. Do not add `flow_rate` or `temperature` to `Equipment` either. ADR-021 extends this: capabilities and `execute` are the open-ended command path; `GenericEquipment` covers unknown machines.

**Alternatives:** Keep speed on the base (rejected: conveyor-centric); a full tag/information-model database (rejected: over-engineered for Phase 5).

---

## ADR-021 — Equipment is open-ended and capability-driven

- **Status:** Accepted
- **Date:** 2026-08-23

**Context:** After ADR-020, docs still implied a future class catalog (`Pump`, `Robot`, `Mixer`, …). A real plant also has unknown and vendor-specific machines. A C++ class per machine type does not scale. A giant `Equipment` interface (`setPressure`, `move`, `loadRecipe`, …) is equally brittle.

**Decision:**

- `Equipment` is a **generic contract**: identity, type metadata, operational state, fault, capabilities, named commands (`execute`), telemetry.
- `type()` is metadata, not a closed enum. Core software must not `if (type == conveyor)`.
- **GenericEquipment** represents arbitrary machines without a new class.
- **Conveyor** remains a specialized implementation for the Gazebo example (custom start/speed rules).
- Specialized C++ is used only when behavior cannot be expressed as capabilities + telemetry.
- Phase 6 adapters map vendor/protocol data into this model. MES/SCADA consume `Equipment`, not Gazebo or vendor APIs.

**Consequences:** Adding a furnace, AGV, or unknown machine does not require changing the MES core or `Equipment.hh`. Do not ship unused Robot/Pump/Oven classes.

**Alternatives:** Inheritance tree of every machine (rejected: catalog); one interface with every command (rejected: brittle); plugin/DLL discovery framework (rejected: too early).

---

## ADR-022 — Industrial adapters are protocol-oriented and MES-independent

- **Status:** Accepted
- **Date:** 2026-08-23

**Context:** SoT Phase 6 requires an industrial adapter layer so MES/SCADA do not depend on PLC vendors or field protocols. A factory may contain Siemens, Allen-Bradley, Schneider, Omron, Beckhoff, VFDs, robots, and proprietary machines. One C++ class per vendor or per machine type does not scale (ADR-021). Binding MES to OPC UA node APIs, Modbus registers, REST JSON, or Gazebo ECM would freeze those details into the core.

**Decision:**

- Introduce `IndustrialAdapter` as the protocol boundary: identity, protocol metadata, connection lifecycle, communication fault, `poll()`, and bound `Equipment` objects.
- MES/SCADA consume `Equipment` (and adapter connection state). They must not include protocol or Gazebo headers.
- Adapters are **protocol/capability-oriented** (`opcua`, `modbus`, `rest`, `mock`), not `SiemensAdapter` / `RobotAdapter` catalogs.
- Phase 6 ships the contract plus `MockIndustrialAdapter` (in-process external source) so the architecture is testable without hardware.
- Production protocol adapters are added incrementally. OPC UA is ADR-025. Modbus and REST are **not** implemented in this increment. Empty protocol placeholders are not created.
- Layout: `industrial/` next to `equipment/`, library `virtual_factory_industrial` links `virtual_factory_equipment`. Gazebo `ConveyorSystem` does not depend on `industrial/`.
- `ConnectionState::Faulted` is a **communication** fault, distinct from `Equipment::fault()` (machine fault).
- Specialized C++ equipment classes remain allowed only when simulation or protocol mapping truly requires them (Conveyor today).

**Consequences:** A pump, furnace, or unknown machine can appear through a mock or OPC UA adapter as `GenericEquipment` without a new machine class. Adding another protocol adapter later should not change the MES-facing `Equipment` API. Do not treat the mock as a production protocol. OPC UA is implemented (ADR-025); Modbus and REST are not.

**Alternatives:** Skip the adapter interface and have MES call open62541 (rejected: protocol leak); one adapter class per PLC vendor (rejected: catalog); implement OPC UA in the same step as the interface (rejected: hardware/library scope beyond proving the architecture — later done in ADR-025); make `ConveyorSystem` an `IndustrialAdapter` (rejected: Gazebo is the plant, not the industrial adapter layer).

---

## ADR-023 — Single active Source of Truth and documentation hierarchy

- **Status:** Accepted
- **Date:** 2026-08-23

**Context:** The repository accumulated implementation Markdown, ADRs, and a SoT PDF. Older conversation or Git history still mentions Stage 0–25, sensor-first, or gateway-only sketches. Agents and developers must not treat those as the live plan, or treat the roadmap as proof of implementation.

**Decision:**

- The only **active** architectural Source of Truth is `docs/MES_SCADA_Virtual_Factory_Source_of_Truth.pdf`.
- Live supporting docs: `implementation-status.md` (what exists), `architecture.md` (how it is structured), `decisions.md` (why), `roadmap.md` (what is next), `CHANGELOG.md` (what changed), `docs/README.md` (how to resume).
- Git history is the historical implementation record.
- `docs/archive/` holds superseded documents only. **Do not trust archived documents for current architecture.**
- Official phases are SoT 1–11 only. Stage 0–25 and other retired numbering are not the live plan.
- Roadmap items are **PLANNED** until `implementation-status.md` and the code say otherwise.
- The SoT PDF is not silently rewritten. If it contradicts agreed architecture, stop and revise it explicitly.

**Consequences:** New work starts from the SoT PDF + implementation-status, not from chat history or archived files. No second live SoT.

**Alternatives:** Multiple competing PDFs (rejected: ambiguity); keep retired stage numbers as a parallel plan (rejected: ADR-015).

---

## ADR-024 — Resource Management is a first-class MES responsibility

- **Status:** Accepted
- **Date:** 2026-08-23

**Context:** A production order cannot be scheduled from machine running-state alone. A machine may be *capable* of a product but *unavailable* (fault, maintenance, already allocated). Another machine may be free but *incapable*. Materials, qualified personnel, tools, fixtures, inspection resources, and work-center capacity also constrain dispatch. Putting scheduling, allocation, or reservation logic inside `Equipment` or `IndustrialAdapter` would mix the technical asset model with production planning.

This refines SoT Phase 7 (MES Core: orders, work centers/equipment, material state, execution, quality, traceability). It does **not** change the SoT PDF. **NOT IMPLEMENTED.**

**Decision:**

- Phase 7 MES will include a **Resource Management** layer as a first-class concern, alongside order management, routing, scheduling, dispatch, execution tracking, materials, quality, and traceability.
- **Capability** and **availability** are distinct. Capability: can this resource perform the operation / make the product? Availability: is it free, qualified, maintained, and not already reserved?
- Resource types include equipment, machines, work centers, production lines, personnel, materials, tools, fixtures, inspection resources, and other production resources.
- **Work Centers** are first-class MES concepts (Plant → Area → Work Center → equipment, operators, tools, inspection).
- A physical asset appears as `Equipment` (identity, type, operational state, commands, telemetry, fault). The same asset may also appear as an **MES resource** (capability, availability, allocation, reservation, scheduling and maintenance constraints, relationships). Do not overload `Equipment` with MES scheduling.
- Production-order **resource readiness** is an MES function: required operations → required resources → READY (schedule/dispatch) or NOT READY (hold).
- Material readiness (required / available / allocated / reserved / expected inbound), personnel qualifications, tool life/compatibility, and maintenance windows belong in Resource Management / related MES modules — not in adapters.
- Authentication/RBAC remains Phase 9 and is separate from personnel-as-production-resource.
- CMMS/full maintenance management is not required in Phase 7; MES must still observe maintenance/fault constraints at the integration boundary.

**Consequences:** MES can assess production readiness and schedule without putting that logic in `Equipment` or `IndustrialAdapter`. Phase 5–6 code is unchanged. No Robot/Pump/Oven classes are implied. Phase 7 remains **NOT IMPLEMENTED** until explicitly started.

**Alternatives:** Schedule from `Equipment::running()` only (rejected: ignores capability vs availability and non-machine resources); put allocation fields on `Equipment` (rejected: mixes asset telemetry with MES planning); implement Resource Management in this documentation pass (rejected: Phase 7 not started).

---

## ADR-025 — OPC UA adapter maps configured nodes into GenericEquipment

- **Status:** Accepted
- **Date:** 2026-08-23

**Context:** SoT Phase 6 and ADR-005 identify OPC UA as a primary information-model interface and open62541 as the initial stack. MES/SCADA must not depend on NodeIds, namespaces, or open62541 APIs. A hard-coded PLC node layout would not scale. A full OPC UA information-model designer, subscription framework, or certificate platform is out of scope for the first production adapter.

**Decision:**

- Implement `OpcUaIndustrialAdapter` as an `IndustrialAdapter` using **open62541** (installed 1.4.0-rc2, amalgamated header). Protocol metadata is `"opcua"`.
- The adapter is a **client**. It connects, reads/writes configured nodes, polls, and reports connection state. It does not become an OPC-UA-specific Equipment class.
- Mapping is a small C++ config (`OpcUaAdapterConfig` / `OpcUaEquipmentMapping`): equipment id and type metadata, command name → node, telemetry name/unit → node, optional Running and Fault nodes. Not a YAML/JSON framework.
- Commands: `execute("start"|"stop")` writes boolean `true`; names starting with `set_` write the double parameter. Telemetry and operational/fault state are populated on `poll()` into `GenericEquipment`.
- `ConnectionState::Faulted` is a communication failure. `Equipment::fault()` is a machine process fault. A dropped OPC UA session does not set machine fault.
- `connect()` after `Faulted` is allowed and recreates the open62541 client (explicit reconnect). Background auto-reconnect is not implemented.
- `TelemetryPoint` is unchanged (`name`, `value`, `unit`). OPC UA source timestamps and StatusCode quality are deferred.
- Tests use an in-process open62541 server (`tests/opcua_test_server.*`) on localhost with multiple mapped test machines (mixer, pump, unknown). **DEVELOPMENT ONLY:** `SecurityPolicy#None`, anonymous access, no certificates. UAExpert is diagnostic only (ADR-006), not a test dependency.
- Production SignAndEncrypt, certificates, subscriptions, history, alarms/conditions, and HA are **not** implemented.
- Modbus and REST adapters are **not** implemented. Gazebo, MES, and SCADA are not dependencies of this adapter.

**Consequences:** Arbitrary machines (mixer, pump, unknown) appear as `GenericEquipment` through mapping. MES-shaped code uses `IndustrialAdapter` / `Equipment` only. Do not present the unsecured localhost test as production industrial security. Do not mark all of Phase 6 complete: Modbus and REST remain unimplemented.

**Alternatives:** Hard-code one PLC node tree (rejected: not open-ended); expose NodeIds on Equipment (rejected: protocol leak); wait for a certificate-management platform before any adapter (rejected: blocks Phase 6); implement subscriptions/history in this milestone (rejected: unnecessary for the first client).

---

## ADR-026 — One OPC UA adapter instance per OPC UA server

- **Status:** Accepted
- **Date:** 2026-08-23

**Context:** A real plant has many PLCs, each typically exposing its own OPC UA server (`opc.tcp://192.168.1.10:4840`, `…11:4840`, …). The first `OpcUaIndustrialAdapter` cut owned one `UA_Client` and one `endpointUrl`. Packing several endpoints into that class would make `IndustrialAdapter::connectionState()` ambiguous: if PLC-2 fails while PLC-1 is healthy, a single adapter-wide Faulted flag would look like the whole factory is down. Equipment has no comms-availability field; communication state lives on the adapter (ADR-022).

**Decision:**

- One `OpcUaIndustrialAdapter` instance = one OPC UA server / one `UA_Client` / one endpoint.
- A factory with N OPC UA servers uses N adapter instances. MES/SCADA later hold a collection of `IndustrialAdapter*` (OPC UA, and later Modbus/REST), not one mega-adapter.
- Independent `connect()`, `poll()`, `disconnect()`, Faulted, and reconnect per server. Equipment on a healthy adapter stays usable when another adapter faults.
- Do not put NodeIds, endpoints, or `UA_Client` on `Equipment`. Do not create `OpcUaPump` / `OpcUaRobot` classes.
- Do not add an adapter-registry or connection-multiplexer class in this increment (MES is not started). Tests compose two adapters directly.
- Nesting multiple connections inside one `OpcUaIndustrialAdapter` was rejected: it would overload `connectionState()` or require a new per-equipment comms API.

**Consequences:** Multi-PLC is composition of protocol adapters, the same shape future Modbus/REST adapters will use. `connectionState()` stays a per-source comms signal, distinct from `Equipment::fault()`.

**Alternatives:** Many `UA_Client`s inside one adapter (rejected: breaks the existing connection-state contract); a factory-wide OPC UA gateway process (rejected: extra runtime, not required); encode comms health on `Equipment` (rejected: mixes process fault with link state).
