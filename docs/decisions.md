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

**Amendment 2026-08-29 (ADR-042, ADR-044, ADR-045):** The ecosystem has **two product GUIs**, not one shared GUI:

- **ICP Designer** — industrial connectivity topology: drag/drop/configure/connect/deploy (ADR-044). Part of **Industrial Connectivity Platform**.
- **MES GUI** — production execution, resources, orders, quality, OEE, reporting (ADR-045). Part of **MES Core**.

SCADA operational HMI remains **Phase 8** (may share .NET stack but is a distinct surface). Do not build Qt/wx/custom C++ desktop product GUIs.

**Consequences:** Do not start a Qt/wx/custom C++ HMI as the product GUI. Do not merge ICP configuration into MES GUI or vice versa.

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
- **Date:** 2026-08-22 (SoT §7.3); amended 2026-08-24

**Context:** Some devices will never speak OPC UA or Modbus cleanly.

**Decision:** Unsupported device → vendor/gateway → REST → adapter boundary → normalized model. REST does not replace industrial protocols.

**Amendment 2026-08-24:** Phase 6 slice **6E** is a REST **industrial gateway adapter**: an HTTP **client** to one origin (`RestIndustrialAdapter`). It is **not** the future MES REST API (that is a later application surface). Library: libcurl. JSON: nlohmann/json, adapter-private. One instance = one HTTP origin. See ADR-037.

**Amendment 2026-08-25:** Slice 6E is **IMPLEMENTED** / **TESTED** (localhost HTTP fixture). Not vendor certification.

**Consequences:** REST remains a fallback. Do not build MES HTTP APIs inside this adapter.

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

**Consequences:** Plugin build links `virtual_factory_equipment`. Higher layers must include `virtual_factory/equipment/…`, never gz-sim, for equipment state. Production protocol adapters were later added in Phase 6 (OPC UA ADR-025, Modbus TCP ADR-036, REST ADR-037); MQTT/EtherNet/IP/PROFINET remain unimplemented.

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
- Adapters are **protocol/capability-oriented** (`opcua`, `modbus`, `rest`, `mqtt`, `ethernet-ip`, `profinet`, `mock`), not `SiemensAdapter` / `RobotAdapter` catalogs.
- Phase 6 ships the contract plus `MockIndustrialAdapter` (in-process external source) so the architecture is testable without hardware.
- Production protocol adapters are added incrementally as Phase 6 slices 6A–6H (ADR-041). OPC UA is ADR-025. Modbus TCP is ADR-036. REST is ADR-037. MQTT, EtherNet/IP, and PROFINET were **not** implemented in the original increment. Empty protocol placeholders are not created.
- Layout: `industrial/` next to `equipment/`, library `virtual_factory_industrial` links `virtual_factory_equipment`. Gazebo `ConveyorSystem` does not depend on `industrial/`.
- `ConnectionState::Faulted` is a **communication** fault, distinct from `Equipment::fault()` (machine fault).
- Specialized C++ equipment classes remain allowed only when simulation or protocol mapping truly requires them (Conveyor today).

**Consequences:** A pump, furnace, or unknown machine can appear through a mock, OPC UA, Modbus, or REST adapter as `GenericEquipment` without a new machine class. Adding another protocol adapter later should not change the MES-facing `Equipment` API. Do not treat the mock as a production protocol. OPC UA is implemented (ADR-025). Modbus TCP is implemented (ADR-036). REST is implemented (ADR-037). MQTT/EtherNet/IP/PROFINET are not.

**Alternatives:** Skip the adapter interface and have MES call open62541 (rejected: protocol leak); one adapter class per PLC vendor (rejected: catalog); implement OPC UA in the same step as the interface (rejected: hardware/library scope beyond proving the architecture — later done in ADR-025); make `ConveyorSystem` an `IndustrialAdapter` (rejected: Gazebo is the plant, not the industrial adapter layer).

---

## ADR-023 — Single active Source of Truth and documentation hierarchy

- **Status:** Accepted
- **Date:** 2026-08-23

**Context:** The repository accumulated implementation Markdown, ADRs, and a SoT PDF. Older conversation or Git history still mentions Stage 0–25, sensor-first, or gateway-only sketches. Agents and developers must not treat those as the live plan, or treat the roadmap as proof of implementation.

**Decision:**

- The only **active** architectural Source of Truth is `docs/MES_SCADA_Virtual_Factory_Source_of_Truth.pdf`.
- That PDF is generated from `docs/source/MES_SCADA_Virtual_Factory_Source_of_Truth.md` using `docs/source/generate-sot-pdf.sh`. Edit the Markdown, then regenerate the PDF. Do not maintain a second live SoT.
- Live supporting docs: `implementation-status.md` (what exists), `architecture.md` (how it is structured), `decisions.md` (why), `roadmap.md` (what is next), `CHANGELOG.md` (what changed), `docs/README.md` (how to resume).
- Git history is the historical implementation record.
- `docs/archive/` holds superseded documents only. **Do not trust archived documents for current architecture.**
- Official phases are SoT 1–11 only. Stage 0–25 and other retired numbering are not the live plan.
- Roadmap items are **PLANNED** until `implementation-status.md` and the code say otherwise.
- The SoT PDF is not silently rewritten. If architecture changes, update the Markdown source and regenerate the PDF **intentionally**.

**Consequences:** New work starts from the SoT PDF + implementation-status, not from chat history or archived files. No second live SoT.

**Alternatives:** Multiple competing PDFs (rejected: ambiguity); keep retired stage numbers as a parallel plan (rejected: ADR-015).

---

## ADR-024 — Resource Management is a first-class MES responsibility

- **Status:** Accepted
- **Date:** 2026-08-23

**Context:** A production order cannot be scheduled from machine running-state alone. A machine may be *capable* of a product but *unavailable* (fault, maintenance, already allocated). Another machine may be free but *incapable*. Materials, qualified personnel, tools, fixtures, inspection resources, and work-center capacity also constrain dispatch. Putting scheduling, allocation, or reservation logic inside `Equipment` or `IndustrialAdapter` would mix the technical asset model with production planning.

This refines SoT Phase 7 (MES Core + Resource Management: orders, work centers/equipment, material state, execution, quality, traceability). **NOT IMPLEMENTED.**

**Amendment 2026-08-24:** Plant hierarchy, dynamic onboarding, explicit readiness reasons, MES vs equipment state, materials/scrap analytics, OEE vs efficiency, events, genealogy, and Opcenter-as-benchmark are specified in ADR-027–035. This ADR remains the capability-vs-availability foundation. Still **NOT IMPLEMENTED**. The SoT PDF was regenerated from `docs/source/`.

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
- REST, MQTT, EtherNet/IP, and PROFINET adapters are **not** implemented in this increment. Gazebo, MES, and SCADA are not dependencies of this adapter.

**Consequences:** Arbitrary machines (mixer, pump, unknown) appear as `GenericEquipment` through mapping. MES-shaped code uses `IndustrialAdapter` / `Equipment` only. Do not present the unsecured localhost test as production industrial security. Do not mark all of Phase 6 complete: slices 6F–6H remain unimplemented. Modbus TCP is ADR-036. REST is ADR-037.

**Alternatives:** Hard-code one PLC node tree (rejected: not open-ended); expose NodeIds on Equipment (rejected: protocol leak); wait for a certificate-management platform before any adapter (rejected: blocks Phase 6); implement subscriptions/history in this milestone (rejected: unnecessary for the first client).

---

## ADR-026 — One OPC UA adapter instance per OPC UA server

- **Status:** Accepted
- **Date:** 2026-08-23

**Context:** A real plant has many PLCs, each typically exposing its own OPC UA server (`opc.tcp://192.168.1.10:4840`, `…11:4840`, …). The first `OpcUaIndustrialAdapter` cut owned one `UA_Client` and one `endpointUrl`. Packing several endpoints into that class would make `IndustrialAdapter::connectionState()` ambiguous: if PLC-2 fails while PLC-1 is healthy, a single adapter-wide Faulted flag would look like the whole factory is down. Equipment has no comms-availability field; communication state lives on the adapter (ADR-022).

**Decision:**

- One `OpcUaIndustrialAdapter` instance = one OPC UA server / one `UA_Client` / one endpoint.
- A factory with N OPC UA servers uses N adapter instances. MES/SCADA later hold a collection of `IndustrialAdapter*` (OPC UA, Modbus, later REST/MQTT/EtherNet/IP), not one mega-adapter.
- Independent `connect()`, `poll()`, `disconnect()`, Faulted, and reconnect per server. Equipment on a healthy adapter stays usable when another adapter faults.
- Do not put NodeIds, endpoints, or `UA_Client` on `Equipment`. Do not create `OpcUaPump` / `OpcUaRobot` classes.
- Do not add an adapter-registry or connection-multiplexer class in this increment (MES is not started). Tests compose two adapters directly.
- Nesting multiple connections inside one `OpcUaIndustrialAdapter` was rejected: it would overload `connectionState()` or require a new per-equipment comms API.

**Consequences:** Multi-PLC is composition of protocol adapters, the same shape future REST/MQTT/EtherNet/IP adapters will use. `connectionState()` stays a per-source comms signal, distinct from `Equipment::fault()`.

**Alternatives:** Many `UA_Client`s inside one adapter (rejected: breaks the existing connection-state contract); a factory-wide OPC UA gateway process (rejected: extra runtime, not required); encode comms health on `Equipment` (rejected: mixes process fault with link state).

---

## ADR-027 — Plant hierarchy is configurable data

- **Status:** Accepted
- **Date:** 2026-08-24

**Context:** Real plants are organized as enterprise/site/building/floor/area/line/cell/work center, but not always in that order. Hard-coding “assembly line” or encoding hierarchy as C++ inheritance would freeze one plant layout into the software.

**Decision:** MES models organizational/location relationships as configurable entities and assignments. A default tree is Enterprise → Site/Plant → Building → Floor → Area → Line/Cell → Work Center → resources. Other trees are valid. Work Center is a production capability that may group multiple equipment, people, and tools. Responsibility (supervisors/managers) is data on those entities. **PLANNED. NOT IMPLEMENTED.**

**Consequences:** Adding a floor or line is configuration. Do not create `AssemblyLine` subclasses to represent layout.

**Alternatives:** Fixed three-level plant (rejected: too rigid); C++ location class hierarchy (rejected: confuses layout with types).

---

## ADR-028 — PLC and equipment onboarding is configuration

- **Status:** Accepted
- **Date:** 2026-08-24

**Context:** A plant may grow from 100 PLCs to 200 by adding a line. Requiring a new C++ class or a rebuild to add a PLC does not scale. Adapter architecture is already one instance per OPC UA server (ADR-026).

**Decision:** Future MES configuration/UI/API creates protocol adapter instances (`OpcUaIndustrialAdapter`, `ModbusIndustrialAdapter`, `RestIndustrialAdapter`, later MQTT/EtherNet/IP/PROFINET if approved) and mappings, then assigns resulting `Equipment` to plant locations and owners. No `PumpPLCAdapter` / `RobotPLCAdapter`. No new C++ adapter class per PLC. No Phase 6 adapter manager. Current C++ construction in tests is not the onboarding product. **PLANNED. NOT IMPLEMENTED.**

**Amendment 2026-08-29 (ADR-042):** **ICP owns industrial onboarding.** Adapter instances, protocol mappings, connection parameters, and equipment identities are created and stored in the **Industrial Connectivity Platform** (ICP configuration + **ICP Designer GUI**). MES Core references stable `equipmentId` values through the **Connectivity Integration Contract (CIC)** and assigns **MES plant/resource** metadata only. MES does not create `OpcUaIndustrialAdapter` instances or hold NodeId/register/topic maps.

**Consequences:** Onboarding uses Phase 6 protocol adapters inside ICP. MES does not become a protocol stack. Scaling 100→1000+ PLCs is ICP configuration, not a C++ rebuild.

**Alternatives:** Compile-time PLC list (rejected); one mega-adapter with all endpoints (rejected: ADR-026); MES-owned adapter manager (rejected: ADR-042).

---

## ADR-029 — Resource readiness reports specific constraint reasons

- **Status:** Accepted
- **Date:** 2026-08-24

**Context:** A generic “resource unavailable” hold hides whether the problem is a fault, missing lot, expired qualification, or a reserved fixture.

**Decision:** Before dispatch, MES evaluates required resources and returns READY or HOLD with explicit reasons (equipment unavailable/faulted, maintenance, operator, qualification, material shortage/quality hold, tool, capacity, reservation, incompatible setup, …). **PLANNED. NOT IMPLEMENTED.**

**Consequences:** Scheduling and operators can act on the actual constraint.

**Alternatives:** Boolean ready flag only (rejected: not actionable).

---

## ADR-030 — MES availability is not equipment technical state

- **Status:** Accepted
- **Date:** 2026-08-24

**Context:** A machine can be Running and still reserved, under maintenance, or on quality hold. Mixing those axes on `Equipment::fault()` or `operationalState()` would break both control and scheduling.

**Decision:** Keep equipment technical state (Stopped/Running/Faulted) and adapter comms state separate from MES availability (Available, Unavailable, Reserved, Allocated, Maintenance, Blocked, Quality Hold, Scheduled, Decommissioned). **PLANNED. NOT IMPLEMENTED.**

**Consequences:** `Equipment` stays a technical contract. MES owns availability.

**Alternatives:** Encode reservation on `Equipment` (rejected: ADR-024).

---

## ADR-031 — Materials and scrap are first-class production facts

- **Status:** Accepted
- **Date:** 2026-08-24

**Context:** Runtime-only efficiency hides material shortage, yield loss, and scrap. Orders cannot be honestly dispatched without lots/quantities/quality status.

**Decision:** MES tracks raw/component/WIP/finished/consumable/packaging identity, lot/serial, quantity, location, status, quality status, reservations, consumption, production, scrap, transfer. Scrap records reason, operation, resource, lot, person, time, order, product. Material availability participates in readiness. Scrap participates in quality and efficiency analytics. **PLANNED. NOT IMPLEMENTED.**

**Consequences:** Efficiency is not “runtime / available time” alone.

**Alternatives:** Inventory-only in ERP with MES ignoring lots (rejected: blocks execution quality).

---

## ADR-032 — OEE is distinct from broader plant efficiency

- **Status:** Accepted
- **Date:** 2026-08-24

**Context:** Averaging OEE % across unlike machines misleads. Using OEE as a synonym for “how good the factory is” hides yield, labor, and on-time performance.

**Decision:** OEE = Availability × Performance × Quality at a defined resource scope. Roll up with documented count- or time-weighting, not a naive mean of percentages. Keep separate KPIs for material yield, scrap/rework, labor/capacity utilization, throughput, on-time completion. **PLANNED. NOT IMPLEMENTED.**

**Consequences:** Dashboards can show OEE and still tell the truth about scrap and materials.

**Alternatives:** Single “efficiency %” from runtime (rejected).

---

## ADR-033 — Analytics consume contextualized events, not raw tags

- **Status:** Accepted
- **Date:** 2026-08-24

**Context:** OPC UA values without order/operation/work-center context cannot explain scrap, downtime, or genealogy.

**Decision:** Three layers: raw industrial data; MES-contextualized events (state, downtime, production, material, quality, allocation, order lifecycle); aggregated analytics (real-time through yearly, vs target/previous/plan/baseline). Dashboards must not be computed only from raw NodeIds. **PLANNED. NOT IMPLEMENTED.**

**Consequences:** Adapters stay thin. MES owns meaning.

**Alternatives:** SCADA historian as the MES (rejected: ADR-007).

---

## ADR-034 — Genealogy is forward and backward

- **Status:** Accepted
- **Date:** 2026-08-24

**Context:** Recall and root-cause need both “what went into this product” and “where did this lot go.”

**Decision:** MES records genealogy linking product/order/operation to consumed lots, equipment, work center, operators, and inspections, and the reverse from a lot to products. **PLANNED. NOT IMPLEMENTED.**

**Consequences:** Quality investigation is a MES function, not a tag browser.

**Alternatives:** Paper travelers only (rejected).

---

## ADR-035 — Siemens Opcenter is a capability benchmark only

- **Status:** Accepted
- **Date:** 2026-08-24

**Context:** Opcenter is a useful industry checklist (execution, tracking, materials, quality, genealogy, SPC, OEE, scheduling, visibility, enterprise integration). Copying proprietary architecture or claiming parity would be false and legally/architecturally wrong.

**Decision:** Use Opcenter (and similar MES suites) as an **external capability benchmark** for the roadmap. Label those items **PLANNED**. Do not claim feature parity. Do not copy proprietary design. **NOT IMPLEMENTED.**

**Consequences:** Roadmap stays ambitious and honest.

**Alternatives:** Ignore industry MES suites (rejected: gaps); clone Opcenter (rejected).

---

## ADR-036 — Modbus TCP adapter maps configured registers into GenericEquipment

- **Status:** Accepted
- **Date:** 2026-08-24

**Context:** SoT Phase 6 lists Modbus as a production industrial path alongside OPC UA. MES/SCADA must not depend on unit ids, function codes, coil/register addresses, or libmodbus APIs. A C++ class per machine type (`PumpModbusAdapter`, `MixerModbusAdapter`) would not scale. Packing several TCP endpoints into one adapter would make `IndustrialAdapter::connectionState()` ambiguous (same problem as ADR-026). Hand-rolling the Modbus TCP ADU is unnecessary when libmodbus already implements FC 1–6.

**Decision:**

- Implement `ModbusIndustrialAdapter` as an `IndustrialAdapter` using **libmodbus** (Ubuntu package `libmodbus-dev` / `libmodbus5` **3.1.10-1ubuntu1**, pkg-config version **3.1.10**). Protocol metadata is `"modbus"`.
- One adapter instance = one Modbus TCP endpoint/session (`host:port`). A factory with N Modbus TCP devices uses N adapter instances. Independent `connect()`, `poll()`, `disconnect()`, Faulted, and `connect()`-after-Faulted per endpoint.
- Several logical machines on one endpoint are several `GenericEquipment` objects created from `ModbusEquipmentMapping`. Do not add machine-specific Equipment or adapter subclasses.
- Mapping is a small C++ config (`ModbusAdapterConfig`): equipment id/type metadata, command name → coil or holding register, telemetry name/unit → table+address, optional running and fault coils. Not a YAML/JSON framework.
- Commands: `execute("start"|"stop")` writes coil `true` (or holding 1); names starting with `set_` write the double as uint16. Discrete inputs and input registers are read-only.
- Telemetry and optional operational/fault coils are populated on `poll()`. `ConnectionState::Faulted` is communication failure (including Modbus exception on an illegal address). `Equipment::fault()` is a machine process fault. A dropped TCP session does not set machine fault. Last-known equipment remains listed while Faulted.
- `connect()` after `Faulted` recreates the libmodbus client (explicit reconnect). Background auto-reconnect is not implemented.
- Public headers do not include `<modbus.h>`. Equipment remains protocol-independent. Gazebo is not a dependency of this adapter.
- Tests use an in-process libmodbus TCP slave (`tests/modbus_test_server.*`) on localhost. Mixer/pump/unknown names are register-map **labels only**. **DEVELOPMENT ONLY:** no authentication, no TLS.
- Client coverage in this slice: FC 1–6. Batch writes, RTU, TLS, and production capacity claims are **not** implemented.
- Isolation was validated with two independent endpoints and a modest 4-endpoint localhost loop. That is **not** production proof for hundreds of Modbus PLCs.
- REST, MQTT, EtherNet/IP, and PROFINET were **not** implemented in this increment. MES and SCADA are not started.

**Consequences:** Arbitrary machines appear as `GenericEquipment` through mapping. MES-shaped code uses `IndustrialAdapter` / `Equipment` only. Do not present the unsecured localhost slave as a production PLC. Do not mark all of Phase 6 complete: slices 6F–6H remain unimplemented. REST is ADR-037. Phase 7 remains **NOT STARTED**.

**Alternatives:** Hand-rolled TCP framing (rejected: libmodbus provides FC 1–6); one adapter class per machine type (rejected: ADR-021/022); many TCP sessions inside one `connectionState()` (rejected: ADR-026 analogue); background reconnect thread (rejected: reconnect stays explicit `connect()` after Faulted); change `Equipment.hh` / `IndustrialAdapter.hh` for Modbus (rejected: no architectural gap).

---

## ADR-037 — REST industrial adapter is an HTTP client gateway (slice 6E)

- **Status:** Accepted. **IMPLEMENTED** / **TESTED** (localhost HTTP fixture; not vendor certification).
- **Date:** 2026-08-24; implemented 2026-08-25

**Context:** REST is an application HTTP API, not a native PLC fieldbus. Some gateways and vendor systems only expose HTTP. The future MES application API must not be confused with this adapter.

**Decision:**

- Slice **6E** adds `RestIndustrialAdapter` implementing `IndustrialAdapter`. Protocol metadata is `"rest"`.
- Role: HTTP **client** to one industrial gateway/vendor **origin**. Not an HTTP server. Not the MES REST API.
- One instance = one origin (scheme + host + port + optional base path). N systems = N instances. Several logical machines on one API = several `GenericEquipment` mappings.
- `poll()` performs mapped reads (GET of a JSON resource, JSON Pointer extraction of multiple telemetry values). `execute()` performs mapped writes (POST/PUT/PATCH). Mapping stays in adapter config (`RestAdapterConfig`). DELETE is not required.
- Credentials (Basic/Bearer) stay in adapter config, not on `Equipment`. Not Phase 9 RBAC. Passwords and bearer tokens must not be logged or placed in `lastError()`.
- Library: **libcurl** (`libcurl4-openssl-dev`, 8.5.0). JSON: **nlohmann/json** 3.11.3, adapter-private. Public headers must not include curl or nlohmann/json.
- TLS certificate verification is enabled by default. Insecure TLS is an explicit development/testing opt-in.
- Optional health GET on `connect()`. If omitted, origin TCP connectivity is sufficient; mapped requests may then establish useful data.
- Tests: local in-process HTTP fixture (`tests/rest_test_server.*`). Local pass ≠ vendor API certification.
- Explicit `connect()` after `Faulted`. Transport/HTTP failure → `ConnectionState::Faulted`; mapped machine fault → `Equipment::fault()`. No background reconnect thread.

**Consequences:** REST remains a fallback (ADR-013). Do not mix MES API work into this adapter. Slice 6F MQTT is implemented (ADR-038). Do not claim production HTTPS vendor certification from the localhost fixture.

**Alternatives:** REST as MES API (rejected: wrong layer); HTTP server adapter (rejected: not how industrial gateways are consumed here); treat REST as equivalent to OPC UA/Modbus (rejected: different model); one adapter for many origins (rejected: ADR-026 analogue).

---

## ADR-038 — MQTT adapter is one client per broker (slice 6F)

- **Status:** Accepted. Slice 6F **IMPLEMENTED** / **TESTED** (2026-08-25). Local Mosquitto ≠ cloud/vendor certification.
- **Date:** 2026-08-24; implemented 2026-08-25

**Context:** MQTT is broker pub/sub, not PLC request/response. One client per machine would explode connections. Many brokers in one `connectionState()` would hide source faults.

**Decision:**

- Slice **6F** adds `MqttIndustrialAdapter` implementing `IndustrialAdapter`. Protocol metadata is `"mqtt"`.
- Role: MQTT **client**. Do not embed a broker in this application.
- One instance = **one broker connection**. Multiple machines/devices = topic mappings on that adapter (`GenericEquipment`; instance ids such as PLC-001 are configuration identities, not C++ types). Different brokers = different instances.
- Subscribe mapped telemetry/state/fault topics; publish mapped commands. `poll()` waits on a bounded in-process queue (default 50 ms). Do not add an MES event bus. Latest-value telemetry is in scope; durable production events are Phase 7.
- Broker, topic, QoS, retain stay in adapter config. Equipment must not expose MQTT/Paho types.
- Library: Eclipse **Paho MQTT C** (`libpaho-mqtt-dev` / `libpaho-mqtt3as` 1.3.13), MQTT **3.1.1**. JSON payloads reuse nlohmann/json (adapter-private). Public headers must not include Paho or nlohmann/json.
- QoS 0/1/2 where practical; default QoS 1 for telemetry subscriptions and commands; command retain=false. No MQTT 5 architecture. No Sparkplug B. No wildcard `+`/`#` subscriptions in 6F.
- MQTT client IDs unique per broker among simultaneous clients in this process. Empty config generates `vf.<adapterId>.<serial>` (never from secrets).
- Username/password in adapter config. TLS verification on by default; insecure TLS is an explicit development/testing opt-in. Secrets must not appear in `lastError()`.
- Reconnect remains explicit `connect()` after `Faulted`. Do not hide Faulted behind silent auto-reconnect. Paho's internal network thread is used for keepalive; there is no application reconnect thread.
- Tests: local Mosquitto broker fixture (`tests/mqtt_test_broker.*`). Local pass ≠ cloud/production MQTT proof. Multi-equipment scale **VALIDATED** at 10/50/100/200 mappings and 2×50 brokers (`docs/mqtt-scalability-test.md`) — **not** production capacity.

**Consequences:** MQTT fits the existing contract without changing `IndustrialAdapter.hh` / `Equipment.hh`. Do not start 6G EtherNet/IP until separately approved. Do not claim vendor/cloud MQTT certification from localhost Mosquitto.

**Alternatives:** One adapter per topic/machine (rejected: connection explosion); many brokers in one adapter (rejected: ADR-026 analogue); treat MQTT as Modbus-style request/response (rejected: wrong protocol model).

---

## ADR-039 — EtherNet/IP adapter is a CIP scanner/client; libplctag explicit messaging (slice 6G)

- **Status:** Accepted. **IMPLEMENTED** / **TESTED** (2026-08-28).
- **Date:** 2026-08-24 (library amendment 2026-08-28)

**Context:** EtherNet/IP is CIP over Ethernet. It is not Modbus with another library. Implicit/cyclic I/O (Class 1, UDP 2222) is a different subset from explicit messaging.

**Decision:**

- Slice **6G** adds `EtherNetIpIndustrialAdapter` implementing `IndustrialAdapter`. `protocol()` returns `"ethernetip"`.
- Role: **scanner/client** to one device. One instance = one device session (host + port + CIP path + plc type as applicable). N devices ⇒ N adapter instances. Several logical machines on one device are several `GenericEquipment` mappings (`PLC-001` etc. are configuration identities, not C++ classes).
- **Library:** **libplctag v2.7.1**, commit `bdb10aeaf4f374cec7ae4e66887446dedf952dc1`. **License:** Mozilla Public License **2.0** (MPL-2.0; elect MPL-2.0 over LGPL-2+ offered in the same distribution). Built as shared `libplctag.so`; linked **privately** into `virtual_factory_industrial` via pkg-config / `.deps/libplctag` fallback. Public headers do **not** include `libplctag.h`.
- **Private wrapper:** `industrial/src/eip_session.{hh,cc}` owns `plc_tag_create/read/write/destroy`. Adapter public header exposes only mapping config types.
- **Subset implemented:** **explicit messaging / symbolic tag read/write only**. Class 1 implicit/cyclic I/O, UDP 2222, and producer/consumer I/O are **NOT IMPLEMENTED**. Do not claim them without library+test evidence.
- CIP/EtherNet/IP is **not** hand-written fake TCP. Tests use libplctag **`ab_server`** ControlLogix emulator — **DEVELOPMENT / INTEGRATION VALIDATION ONLY**; not Allen-Bradley/Rockwell hardware certification.
- `connect()` after `Faulted` recreates tags and reconnects explicitly. No application-level background reconnect thread.
- `ConnectionState::Faulted` is communication failure. `Equipment::fault()` is machine fault from mapped fault tag only. Credentials/secrets are not exposed through Equipment or `lastError()`.
- `IndustrialAdapter.hh` and `Equipment.hh` unchanged for 6G. OPC UA, Modbus, REST, MQTT, and Gazebo production code unchanged. No adapter manager.

**Consequences:** EtherNet/IP fits the existing multi-adapter contract. Do not start 6H PROFINET until separately approved. Do not claim hardware certification from `ab_server`.

**Alternatives:** Pretend EIP is Modbus (rejected); hand-roll CIP (rejected); EIPScanner (not selected; libplctag chosen for explicit tag API); implement implicit I/O first without tests (rejected); static-only libplctag in production (rejected: prefer shared `.so` with private link).

---

## ADR-040 — PROFINET **supported via gateway integration**; native IO-Controller deferred (slice 6H)

- **Status:** Accepted. Investigation **COMPLETE** (2026-08-28). Final architectural decision **COMPLETE** (2026-08-28). **6H SUPPORTED VIA GATEWAY** — first-class MES integration path. Native `ProfinetIndustrialAdapter` **DEFERRED**.
- **Date:** 2026-08-24 (investigation 2026-08-28; final decision 2026-08-28)

**Context:** PROFINET IO uses Ethernet Layer 2, cyclic real-time process data, DCP, GSDML engineering, slot/submodule addressing, and IO-Controller / IO-Device roles. It is not TCP/UDP request/response like OPC UA, Modbus, REST, or MQTT. Slice **6H** required investigation before any code.

### PROFINET roles (investigation)

| Role | Description | Virtual Factory adapter need |
| --- | --- | --- |
| **IO-Controller** | PLC/master; establishes ARs, cyclic IO to devices | **Required** — adapters must read/write field devices like other protocol clients |
| **IO-Device** | Field module/sensor/drive slave | Target equipment on the network, not our adapter role |
| **IO-Supervisor** | Engineering/diagnostics PC | Out of scope for Phase 6 adapter |
| **Engineering (GSDML)** | Device description, module layout | Phase 7 onboarding / external tools; not hand-coded per PLC |

**Selected role for this project:** **IO-Controller** (one controller instance per adapter connects to one or more IO-Devices on a PROFINET segment). Using an IO-Device stack (p-net) as the controller would be architecturally incorrect (ADR-040 original decision retained).

### What PROFINET requires (subset relevant to IndustrialAdapter)

- **Cyclic process data** (RT Class 1 minimum): periodic input/output image exchange, cycle counters, watchdogs — not `poll()`-on-demand alone.
- **Acyclic RPC** (read/write records, parameters, I&M) over DCE/RPC.
- **DCP** (discovery, set station name/IP).
- **Diagnostics / alarms** (channel diagnosis, qualified diagnosis).
- **Raw Ethernet** (Layer 2), typically `AF_PACKET` / `PF_PACKET`, often **CAP_NET_RAW** or root.
- **GSDML** for real device topology (modules/submodules/slots); engineering tool or pre-parsed config in Phase 7.
- **IRT / RT Class 3 / MRP / ProfiSafe / ProfiDrive:** advanced subsets — **not** required for first legitimate slice but **not** available in most OSS options.

### Stack investigation summary

| Candidate | Role | Version / source | License | Controller? | Linux / Ubuntu | C/C++ embed in `virtual_factory_industrial` | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **RT-Labs p-net** | IO-Device | public eval branch on GitHub; full sources commercial | **GPL-3.0** + commercial | **No** (explicit in README) | Yes (raw Ethernet) | Device-only; GPLv3; public repo evaluation-only without ports | **Rejected** for controller adapter; do not link as controller |
| **PROFINET Community Stack (PI)** | Device (+ controller APIs in CS, not turnkey) | PI GitLab after membership + license | PI community license (not public OSS) | Partial — toolkit, not drop-in | Linux HAL demo | Requires PI membership; integration project; RTA commercial toolkit for controller | **Not available** in this repo without PI membership / commercial path |
| **profinet-py** | IO-Controller | v0.6.3 / `f0rw4rd/profinet-py` | **GPL-3.0** + commercial | Yes (RT Class 1 cyclic) | Yes (AF_PACKET, root) | **Python only**; immature (2026); GPL; not C++ industrial library | **Rejected** for embedded C++ adapter |
| **Siemens PROFINET Driver** | Controller + Device | Commercial product | Commercial | Yes | Linux supported | Commercial license required | **Future commercial path** — not in OSS scope |
| **RAPIDSEA / Hilscher / Softing** | Controller | Commercial | Commercial | Yes | Yes | Commercial | **Future commercial path** |
| **Fake TCP/UDP socket** | — | — | — | — | — | — | **Rejected** (ADR-040) |

**Conclusion:** No credible **open-source C/C++ IO-Controller** stack is available for linking into `virtual_factory_industrial` with a license and maturity comparable to open62541, libmodbus, libcurl, Paho, or libplctag. **6H cannot be legitimately implemented in this increment without faking PROFINET or violating ADR-040.**

### Architecture answers (for a future approved controller stack)

1. **One `ProfinetIndustrialAdapter`** = one **IO-Controller** context on one **Ethernet interface / PROFINET network segment** (not one device per adapter if the stack supports multiple devices per controller).
2. **One controller** can manage **multiple IO-Devices** (multiple ARs) — topology differs from Modbus/EtherNet/IP “one session = one endpoint.”
3. **Multiple adapters** = multiple independent controllers (e.g. separate NICs or isolated networks).
4. **PROFINET device** = IO-Device station (station name, IP, vendor/device ID); **module/submodule** = GSDML slot/subslot IO mapping.
5. **Cyclic process data** lives in controller IO image buffers updated by the stack (typically background cyclic thread); `poll()` copies latest image into `GenericEquipment` (same pattern as MQTT Paho thread + bounded `poll()`).
6. **PLC-001 / PLC-002** = `GenericEquipment` mapping entries pointing to device + slot/subslot + byte/bit offsets in process data — configuration identities, not C++ classes.
7. **Two PROFINET networks** = two adapter instances on two interfaces (or two controller instances if stack allows).

### `IndustrialAdapter` contract (analysis)

The existing contract is **sufficient** for a future PROFINET implementation:

- `connect()` / `disconnect()` — controller start/stop, AR establishment.
- `poll()` — copy latest cyclic IO image + diagnostics into `GenericEquipment` (bounded wait).
- `execute()` — command writes to output process data or acyclic RPC.
- `connectionState()` / `lastError()` — controller/link/AR failures vs `Equipment::fault()` from mapped diagnosis bits.

A **private background cyclic thread** inside the adapter (like Paho MQTT) is acceptable; it must not leak thread/API types through public headers. **No change to `IndustrialAdapter.hh` or `Equipment.hh` required.**

### Linux / laptop requirements (for any future implementation)

- Dedicated or shared **Ethernet NIC**; loopback is **not** sufficient for real PROFINET IO.
- **Raw Ethernet** (`AF_PACKET`), **CAP_NET_RAW** or root for many stacks.
- **PREEMPT_RT** optional for IRT/low jitter; RT Class 1 may work on stock Ubuntu with measured jitter.
- **veth / network namespaces** may help isolated dev tests; not a substitute for stack conformance testing.
- **200 simulated devices** on a laptop: **not measured**; do not claim capacity without benchmark (future 1,200-device system test).

### Test strategy (if controller stack approved later)

- **DEVELOPMENT / INTEGRATION VALIDATION ONLY** — software IO-Device simulators (e.g. p-net device sample, PI virtual IO device when available) + real controller stack; **not** Siemens/vendor certification.
- No fake TCP/UDP PROFINET mocks.
- Separate: unit (mapping), stack integration, multi-device isolation, optional hardware bench.

### Decision (2026-08-28) — investigation

- **6H `ProfinetIndustrialAdapter` is NOT IMPLEMENTED** in this repository.
- **Investigation is COMPLETE.**
- **Rejected:** fake TCP/UDP; p-net as controller; GPLv3 profinet-py subprocess wrapper as “production adapter”; claiming IMPLEMENTED without real cyclic IO stack.

### Final architectural decision (2026-08-28) — **Gateway-first / gateway-supported**

**Phase 6 slice 6H is satisfied by supported gateway integration.** The MES requirement is protocol-independent industrial integration into `GenericEquipment`, not a native PROFINET stack inside the application.

| Path | Status | Meaning |
| --- | --- | --- |
| **Gateway integration** | **APPROVED and SUPPORTED** | PROFINET PLC/device → industrial gateway → OPC UA / Modbus / REST / MQTT → existing `IndustrialAdapter` → `GenericEquipment` → MES |
| **Native IO-Controller** | **DEFERRED** | Future optional `ProfinetIndustrialAdapter`; requires separate ADR + commercial or PI stack |

**Terminology:** Use **“PROFINET supported through gateway integration”** — not “PROFINET unsupported” and not “workaround.”

See [`docs/profinet-gateway-integration.md`](../profinet-gateway-integration.md) for configuration model, capability matrix, multi-PLC topology, and test evidence.

### Does Phase 6 require native PROFINET?

**No.** Phase 6 goal is an **IndustrialAdapter layer** mapping industrial sources into **GenericEquipment**. That goal is met for PROFINET **equipment** when a field device is reached through a **standards-based gateway** already supported by implemented adapters (OPC UA, Modbus TCP, REST).

| Integration model | What it means | Phase 6 status |
| --- | --- | --- |
| **1. Native IO-Controller** | `ProfinetIndustrialAdapter` speaks PN cyclic IO directly | **NOT IMPLEMENTED** — deferred |
| **2. Gateway access** | PN device → gateway → existing adapter | **SUPPORTED VIA GATEWAY (6H)** |
| **3. MES/SCADA interoperability** | Normalized `Equipment` regardless of fieldbus | **Achieved via (2)** — MES never sees PROFINET |

Native PROFINET remains valuable for **low-latency cyclic IO**, **full PN diagnostics/alarms**, and **direct IO-Controller deployments** — but it is **not a prerequisite** to proceed to Phase 7 or to represent PROFINET-origin equipment in the architecture.

### Gateway architecture (approved)

```text
PROFINET IO-Device(s) on plant Ethernet
        │
Industrial gateway / PN proxy  (vendor hardware or appliance; out of repo scope)
        │  OPC UA | Modbus TCP | REST | MQTT
        ▼
OpcUaIndustrialAdapter | ModbusIndustrialAdapter | RestIndustrialAdapter | MqttIndustrialAdapter  (6B–6F)
        ▼
GenericEquipment  (PLC-001, PLC-002, … configuration identities)
        ▼
MES / SCADA (Phase 7+)
```

Gateway configuration, GSDML, and PN engineering stay **outside** this application (gateway vendor tools or Phase 7 onboarding). This repo maps **gateway-exposed** nodes/registers/endpoints only.

### Future ~1,200-device validation (gateway path)

Target composition (~200 per protocol) remains a **future system benchmark**, not a production claim. Under **gateway-only 6H**:

- **PROFINET “200 devices”** in that test means **200 logical `GenericEquipment` mappings** reached via **gateway-backed OPC UA / Modbus / REST origins** — **not** 200 native PN IO-Devices on one IO-Controller.
- Honest labeling: **VALIDATED UNDER THESE TEST CONDITIONS** with metrics (CPU, RSS, FDs, sockets, threads, poll latency, connect time, message rates, failures).
- **16 GB RAM laptop capacity for 1,200 devices is unmeasured** — do not claim until benchmarked.
- Native PN participation would require Option A/B implementation first; gateway path reuses existing scalability experiments (6C MQTT/OPC UA patterns).

### Phase 6 / Phase 7 status after this decision

- **6H:** **SUPPORTED VIA GATEWAY** — native IO-Controller **DEFERRED**.
- **Phase 6:** **COMPLETE** (2026-08-28 final audit: 6A–6G implemented/tested; 6H gateway integration documented and supported; no native PN code required).
- **Phase 7:** **NOT STARTED.**

### If native PROFINET is approved later

Requires **new ADR** choosing Option A or B with: stack/version/license, NIC/privilege requirements, test fixture plan, and explicit rejection of fake TCP/UDP. `IndustrialAdapter` contract remains sufficient (no header change).

**Consequences:** Phase 6 may proceed to final audit without native 6H. PROFINET-heavy factories integrate via documented gateway pattern. No `ProfinetIndustrialAdapter` code until explicit future approval.

**Alternatives:** Fake TCP PROFINET (rejected); p-net as controller (rejected); GPL Python wrapper (rejected); silent PROFINET omission (rejected); mandatory native commercial stack for Phase 6 closure (rejected — gateway suffices).

### Amendment 2026-08-29 — Native IO-Controller approved for ICP implementation

- Native PROFINET IO-Controller **APPROVED FOR IMPLEMENTATION** inside **ICP** (not MES), pending commercial stack procurement and explicit coding slice approval.
- **Primary candidate:** Softing PROFINET Controller Stack. **Alternate:** Hilscher cifX + NXLFW-PNM (CIFX 50E-RE).
- Gateway path remains **first-class**; ICP Standard must not require Hilscher hardware.
- See [`profinet-native-evaluation.md`](../profinet-native-evaluation.md), [`profinet-hilscher-final-gate.md`](../profinet-hilscher-final-gate.md).

### Amendment 2026-08-30 — Stage A scaffolding (Hilscher track)

- `ProfinetIndustrialAdapter` **scaffolding IMPLEMENTED** with private `profinet_session` stub; **no production cyclic IO** until cifX smoke test.
- `VF_ENABLE_HILSCHER_PROFINET` / `VF_HILSCHER_CIFX_AVAILABLE` CMake flags; default OFF when SDK absent.
- Status: **PARTIALLY IMPLEMENTED** / **BLOCKED BY SDK/HARDWARE** — not **TESTED** for plant connectivity.
- See [`native-fieldbus-implementation-status.md`](../native-fieldbus-implementation-status.md), [`hilscher-environment-audit.md`](../hilscher-environment-audit.md).

### Amendment 2026-08-30 — cifX software integration (no hardware)

- Official NXDRV-LINUX libcifx may be linked when `VF_ENABLE_HILSCHER_PROFINET` / `VF_ENABLE_HILSCHER_PROFIBUS` are ON.
- Documented cifX APIs only; PNM/DPM protocol mailbox **NOT IMPLEMENTED** without firmware Protocol API headers.
- Plant connectivity remains **HARDWARE VALIDATION PENDING**. Gateway path unchanged.

---

## ADR-046 — PROFIBUS **supported via gateway**; native DP Master approved (Hilscher track)

- **Status:** Accepted. Investigation **COMPLETE** (2026-08-29). Native `ProfibusIndustrialAdapter` **APPROVED FOR IMPLEMENTATION** pending SDK/hardware smoke test.
- **Date:** 2026-08-29 (Stage A scaffolding 2026-08-30)

**Context:** PROFIBUS DP uses RS-485 cyclic process data, GSD-based slave configuration, and DP Master/Slave roles. Like PROFINET, the MES requirement is protocol-independent `GenericEquipment` — fieldbus specifics stay below the adapter boundary.

### Roles

| Role | Adapter need |
| --- | --- |
| **DP Master** | **Required** — one master per adapter / bus segment |
| **DP Slave** | Target equipment on the bus |
| **Gateway** | PB device → OPC UA/Modbus/REST/MQTT → existing adapters |

**Selected native role:** **DP Master** via Hilscher cifX (**CIFX 50E-DP 1251.410**, CIFXDPM/NXLFW-DPM firmware, NXLIC-MASTER).

### Decision

| Path | Status |
| --- | --- |
| **Gateway integration** | **APPROVED and SUPPORTED** — same pattern as PROFINET gateway (ADR-040) |
| **Native DP Master** | **APPROVED FOR IMPLEMENTATION** — Hilscher cifX primary; Softing PBpro alternate if procurement fails |
| **Fake serial/TCP PROFIBUS** | **Rejected** |

### Simultaneous PROFINET + PROFIBUS

One ICP host may run **CIFX 50E-RE** (PROFINET) and **CIFX 50E-DP** (PROFIBUS) simultaneously — **two cards**, not one RE card for both. **Not verified** until hardware smoke test.

### Stage A scaffolding (2026-08-30)

- `ProfibusIndustrialAdapter` + private `profibus_session` stub compile without SDK.
- Production cyclic IO **BLOCKED BY SDK/HARDWARE**.
- Gateway path unchanged.

**Consequences:** ICP Industrial SKU can add native PB without MES changes. ICP Standard remains gateway-only.

**Alternatives:** Gateway-only forever (rejected for Industrial SKU goal); profirust/experimental software-only master (rejected for v1); custom GSD parser in-repo (deferred — use vendor tooling).

See [`profibus-native-evaluation.md`](../profibus-native-evaluation.md).

---

## ADR-041 — Phase 6 uses slices 6A–6H inside official Phases 1–11

- **Status:** Accepted
- **Date:** 2026-08-24

**Context:** The official roadmap is SoT Phases 1–11. Completing industrial connectivity before MES requires more protocol work than the original “OPC UA + Modbus + REST” remaining list. Creating Phase 12 or renumbering MES would break SoT numbering.

**Decision:**

- Official phase numbering remains **1–11**. Phase 6 remains **Industrial Adapter Layer**.
- Implementation slices inside Phase 6:
  - **6A** architecture + mock — **IMPLEMENTED**
  - **6B** OPC UA / open62541 — **IMPLEMENTED**
  - **6C** OPC UA multi-server validation (10–200 simulated in-process servers) — **VALIDATED** (not production capacity)
  - **6D** Modbus TCP / libmodbus — **IMPLEMENTED** / **TESTED**
  - **6E** REST industrial gateway — **IMPLEMENTED** / **TESTED** (localhost HTTP fixture; not vendor certification)
  - **6F** MQTT / Paho C — **IMPLEMENTED** / **TESTED** (localhost Mosquitto; not vendor certification). Multi-equipment scale **VALIDATED** (10/50/100/200 + 2×50; not production capacity)
  - **6G** EtherNet/IP / libplctag — **IMPLEMENTED** / **TESTED** (explicit messaging only; local `ab_server` fixture ≠ hardware certification)
  - **6H** PROFINET — **SUPPORTED VIA GATEWAY** (ADR-040); native IO-Controller **DEFERRED**
- Order: 6A → 6B → 6C → 6D → 6E → 6F → 6G → 6H → Phase 6 final audit → Phase 7.
- Do not start Phase 7 until Phase 6 scope is completed **or** remaining slices (especially 6H) are explicitly marked by an approved ADR.
- Do not implement an adapter manager in Phase 6 (ADR-028).
- 10–200 OPC UA simulated servers remain **validation only**.

**Consequences:** Documentation and SoT now match the intended connectivity scope without inventing new official phases. 6G EtherNet/IP is the next implementation slice, only after separate approval.

**Alternatives:** New official phases after Phase 6 (rejected: breaks 1–11); start MES after REST (rejected: user decision); claim PROFINET implemented via a stub (rejected: ADR-040).

---

## ADR-042 — Modular two-product ecosystem: ICP and MES Core

- **Status:** Accepted
- **Date:** 2026-08-29

**Context:** The platform must be commercially modular: customers may buy industrial connectivity without MES, MES without our connectivity, or both integrated. Third-party MES, SCADA, and ERP must be able to consume our connectivity; our MES must consume third-party connectivity. Hard dependencies (`MES → our ICP code`, `ICP → our MES code`) would block independent sales and replacement.

**Decision:**

1. **Two independently deployable, sellable, licensable, versioned, upgradeable, configurable, operable products**, each with **its own GUI**:
   - **Product 1: Industrial Connectivity Platform (ICP)**
   - **Product 2: MES Core**
2. Products communicate **only** through the documented, versioned, technology-independent **Connectivity Integration Contract (CIC)** (ADR-043).
3. **Modularity is mandatory:** adding/removing/replacing industrial sources and external integrations is **configuration/onboarding**, not C++ source changes (100→1000+ PLCs).
4. **Phase 6** delivered the **ICP adapter foundation** (`virtual_factory_equipment` + `virtual_factory_industrial`). It is **not** the complete ICP product.
5. **Phase 7 (official SoT)** = **MES Core** product. **ICP product completion** uses **ICP-1** implementation slices (see `docs/icp-product-architecture.md`, `docs/roadmap.md`) — not new official SoT phase numbers.
6. **Deployment models:** (A) ICP only → customer MES/SCADA/ERP; (B) MES only → customer connectivity; (C) integrated platform; (D) mixed third-party pairings via CIC.

**Consequences:** AdapterManager, PollScheduler, ICP config storage, northbound API, and **ICP Designer** belong to ICP — **not** MES Phase 7. ADR-028 amended. MES references `equipmentId` via CIC; plant hierarchy and MES resources stay in MES. Protocol SDKs stay in ICP distribution only.

**Alternatives:** Monolithic single product (rejected: blocks modular sales); MES owns adapters (rejected: protocol leak + coupling); shared single GUI (rejected: ADR-044/045).

---

## ADR-043 — Connectivity Integration Contract (CIC)

- **Status:** Accepted
- **Date:** 2026-08-29

**Context:** Two products need a stable seam. In-process `Equipment*` pointers suffice for Phase 6 tests but not for independent deployment, third-party interchange, or separate versioning.

**Decision:** Define **Connectivity Integration Contract (CIC)** — protocol-neutral, versioned (SemVer) schema for equipment identity, capabilities, telemetry (with observation timestamps), operational state, machine fault, communication health, commands, results, and industrial events.

**Planned transports:** gRPC, REST/OpenAPI, WebSocket/SSE. Optional in-process SDK with **identical semantics**. **Not in CIC:** protocol mappings or vendor address spaces.

See `docs/connectivity-integration-contract.md`. **PLANNED. NOT IMPLEMENTED.**

**Consequences:** Third-party systems integrate with ICP via CIC. MES Core uses `IIndustrialDataProvider`. Southbound `RestIndustrialAdapter` ≠ northbound CIC REST.

**Alternatives:** Direct `virtual_factory_industrial` link in MES (rejected); undocumented API (rejected).

---

## ADR-044 — ICP Designer GUI is a core ICP product component

- **Status:** Accepted
- **Date:** 2026-08-29

**Context:** ICP must be commercially complete without MES. Visual industrial topology configuration is primary product value.

**Decision:** **ICP Designer** is a **major ICP component**. UX: **DRAG → DROP → CONFIGURE → CONNECT → DEPLOY**. Configures sources, equipment, gateways (incl. PROFINET-via-gateway), and northbound CIC targets (MES, SCADA, ERP). Separate from MES GUI. **PLANNED** — slice **ICP-1F**. See `docs/icp-product-architecture.md`.

**Alternatives:** YAML-only (insufficient for commercial ICP); merged with MES GUI (rejected).

---

## ADR-045 — MES Core product boundary and MES GUI

- **Status:** Accepted
- **Date:** 2026-08-29

**Context:** MES Core is independently sellable and must consume industrial data without protocol SDKs.

**Decision:** MES Core owns MES domain + **MES GUI** + MES API. Industrial data via **CIC** only (`IIndustrialDataProvider`). Official **Phase 7** = MES Core. **NOT IMPLEMENTED.** See `docs/mes-core-product-architecture.md`.

**Alternatives:** MES reads fieldbuses directly (rejected); monolith with ICP (rejected: ADR-042).

