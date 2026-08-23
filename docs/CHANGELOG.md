# Changelog

Meaningful engineering changes for the Virtual Factory project.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

Phase 6C follow-on (2026-08-23): **one OPC UA adapter instance per OPC UA server** (ADR-026). Multi-PLC is composition of adapters, not multiple `UA_Client`s inside one `connectionState()`. Tests cover two in-process servers, independent fault, and independent reconnect. **Phase 6 remains IN PROGRESS.** Modbus / REST **NOT IMPLEMENTED**. Phase 7 has **not** started.

Phase 6C — production OPC UA adapter (2026-08-23): `OpcUaIndustrialAdapter` maps configured nodes into `GenericEquipment` via open62541. In-process test server; unit test passed. **Phase 6 remains IN PROGRESS.** Architecture + mock + OPC UA **COMPLETE**. Modbus / REST **NOT IMPLEMENTED**. Phase 7 has **not** started. SoT PDF not modified (realizes existing SoT OPC UA path; ADR-025).

### Added

- `OpcUaIndustrialAdapter` and C++ node-mapping structs (`OpcUaAdapterConfig`).
- In-process OPC UA test server exposing multiple generic test machines (mixer, pump, unknown) — localhost, SecurityPolicy#None, anonymous, **DEVELOPMENT ONLY**.
- Unit test `tests/opcua_adapter_test.cc`.
- ADR-025: OPC UA adapter maps configured nodes into GenericEquipment; open62541; comms vs machine fault; no NodeIds on Equipment.
- ADR-026: one `OpcUaIndustrialAdapter` instance per OPC UA server; multi-PLC = multiple adapter instances.

### Changed

- `virtual_factory_industrial` links open62541. Gazebo plugin still does not.
- `GenericEquipment::setOperationalState` for adapter mapping of Running without executing a command.
- Root CMake adds `opcua_adapter_test`.

### Documentation

- Phase 6 marked **IN PROGRESS** with explicit slice status: architecture/mock/OPC UA complete; Modbus/REST not implemented.
- Security: unsecured localhost test is not production industrial security.

Architectural refinement (2026-08-23): **Resource Management** added to the *future* MES (Phase 7) architecture. Work Centers, capability vs availability, materials/personnel/tools/maintenance constraints, and production-order resource readiness are documented. **Phase 7 remains NOT IMPLEMENTED.** **No application behaviour changed.** SoT PDF not modified (compatible refinement of SoT Phase 7; ADR-024).

Documentation consolidation (2026-08-23): one active SoT, supporting Markdown synchronized with the repository, `docs/archive/` for historical material only. No second SoT PDF existed to archive.

Phase 6 — Industrial Adapter Layer (earlier increment): **adapter architecture + mock adapter.** OPC UA followed in Phase 6C (see above). Modbus / REST remain **not** implemented. Phase 7 has **not** started.

### Documentation

- Active hierarchy: SoT PDF → implementation-status → architecture → decisions → roadmap → changelog → docs/README. Root README points at `docs/README.md` and the SoT PDF.
- `docs/archive/README.md` states archived files are not authoritative. Git remains the historical code record.
- ADR-024: Resource Management is a first-class MES responsibility (capability vs availability; Work Centers; do not overload `Equipment`). PLANNED only.
- Phase 6 marked COMPLETE for architecture + mock only at that increment. OPC UA later completed (ADR-025); Modbus/REST still not implemented.
- Phase 5 remains COMPLETE (open-ended Equipment). Conveyor remains a Gazebo example.

### Added

- `IndustrialAdapter` contract (`id`, `protocol`, connection lifecycle, `poll`, bound `Equipment`).
- `MockIndustrialAdapter`: in-process external source for arbitrary machines via `GenericEquipment` (no Pump/Robot classes).
- Unit test `tests/industrial_adapter_test.cc`.
- `industrial/` library `virtual_factory_industrial` (depends on equipment only).
- ADR-022: protocol-oriented adapters; MES/SCADA must not depend on protocols or Gazebo.

### Changed

- Root `CMakeLists.txt` adds `industrial` subdirectory and `industrial_adapter_test`.

### Documentation

- Phase 6 marked COMPLETE for architecture + mock. Explicit: mock implemented; production OPC UA/Modbus/REST not implemented.
- Phase 5 remains COMPLETE (open-ended Equipment). Conveyor Gazebo example unchanged.

### Architecture

- Adapters translate protocols into the Phase 5 `Equipment` model. Gazebo `ConveyorSystem` is not an adapter. SoT PDF not modified (Phase 6 realizes existing SoT adapter architecture).

Phase 5 architectural refinement (still unreleased with Phase 6): **open-ended / capability-driven Equipment model.**

### What was wrong (Phase 5)

- The first Phase 5 cut put conveyor `speed` on the generic `Equipment` contract.
- After that was moved, docs still implied a **fixed class catalog** (Conveyor / Pump / Robot / …) as the architecture.

### What was corrected (Phase 5)

- `Equipment` is a generic contract: identity, type metadata, state, fault, capabilities, named commands (`execute`), telemetry `{name, value, unit}`.
- `GenericEquipment` represents arbitrary machines without a new C++ class.
- `Conveyor` remains a specialized Gazebo example (`speed_control` / `set_speed`); not the architecture.
- ADR-021 records the decision. No Pump/Robot/Oven classes were added.

### What remains unchanged

- Phases 1–4 simulation behavior (Start@1000, Stop@5000, 0.5 m/s, product −1.5 m → 0.5 m).
- Gazebo-independent `equipment/` library; plugin still owns ECM/pose.
- No production OPC UA, Modbus, REST, MQTT, MES, SCADA, API, GUI, or auth.

### Added

- SoT Phase 5 Gazebo-independent equipment contract: `Equipment`, `GenericEquipment`, `Conveyor`.
- Unit test `tests/equipment_test.cc` (CMake `ctest` target `equipment_test`).
- Top-level `CMakeLists.txt` for the equipment library and tests.

### Changed

- Phase 5 correction: `speed` / `setSpeed` live on `Conveyor`, not `Equipment`.
- Phase 5 refinement: capability-driven / open-ended model. Named `execute(command, parameter)`.
- Heartbeat reports conveyor speed via `telemetry()`, not `EquipmentStatus.speed`. Conveyor `type()` is `belt_conveyor`.

### Documentation

- Phase 5 marked COMPLETE after this refinement.
- ADR-019: Gazebo-independent contract. ADR-020: speed is not a base property. ADR-021: no fixed equipment catalog.

### Architecture

- Normalized contract is open-ended and capability-driven. Conveyor is a simulation example, not a catalog. Phase 6 adapter architecture + mock added in the same unreleased window. SoT PDF not regenerated (consistent with SoT §11).

---

## 2026-08-22

### Added

- PRODUCT-001 discovery through the Gazebo ECM `Name` component and kinematic +X pose integration while the conveyor is running (`ConveyorSystem`).
- Heartbeat fields `dt` (seconds) and `product_x` (metres) for runtime diagnosis.
- Documentation set: `docs/README.md`, `docs/CHANGELOG.md`; SoT PDF placed under `docs/`.

### Changed

- Official implementation numbering is SoT Phases 1–11 (ADR-015). Retired competing Stage 0–25 / handoff sequences as live phase labels.
- `docs/architecture.md`, `docs/decisions.md`, `docs/roadmap.md`, `docs/implementation-status.md`, and root `README.md` rewritten to a single authority model.

### Fixed

- Product motion used `_info.dt.count()` (nanosecond ticks). Now `std::chrono::duration<double>(_info.dt).count()` (seconds). Headless verification: `dt=0.001 s`; 4.0 s at 0.5 m/s moved PRODUCT-001 from −1.5 m to 0.5 m.

### Documentation

- Established Levels 1–7: SoT PDF → implementation-status → architecture → decisions → roadmap → changelog → docs index.
- Discarded stale Stage 0 README content and the old gateway-only architecture sketch as live architecture.

### Architecture

- No SoT change. Markdown now matches the existing SoT (adapter layer, REST fallback, .NET/Blazor GUI, Gazebo-as-plant). PDF not regenerated.

---

## 2026-08-19

### Added

- PRODUCT-001 model and world include (`feat: add product to conveyor world`).
- Belt linear velocity command on the `belt` link.

---

## 2026-08-18

### Added

- Conveyor Start/Stop/SetSpeed and development update 1000/5000 control.
- PreUpdate heartbeat.

### Changed

- Factory visual materials.

---

## 2026-08-17

### Added

- Static conveyor CV-001.
- `ConveyorSystem` plugin loaded from `libConveyorSystem.so`.

---

## 2026-08-16

### Added

- Repository, initial documentation, Gazebo world and factory floor.
