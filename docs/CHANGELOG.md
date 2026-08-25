# Changelog

Meaningful engineering changes for the Virtual Factory project.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

Phase 6F MQTT industrial adapter (2026-08-25): `MqttIndustrialAdapter` maps configured MQTT topics into `GenericEquipment` via Eclipse Paho MQTT C 1.3.13 (`libpaho-mqtt3as` MQTTAsync, MQTT 3.1.1) and nlohmann/json 3.11.3. One adapter instance = one broker/session. Multiple machines on one broker are mappings. Mosquitto is a development test broker only; unit test passed. Multi-equipment scale **VALIDATED** at 10/50/100/200 mappings and 2×50 brokers (`docs/mqtt-scalability-test.md`) — correctness only, not production capacity, not vendor certification. Isolation checked with two adapters on one broker and two independent brokers. TLS verification on by default; username/password supported; passwords not logged. Bounded `poll()` (default 50 ms; bounded queue with latest-value drop-oldest). **Phase 6 remains IN PROGRESS.** Architecture + mock + OPC UA + Modbus TCP + REST + MQTT **COMPLETE**. EtherNet/IP **NOT IMPLEMENTED**. Phase 7 has **not** started. SoT PDF regenerated to record 6F implemented.

- **6A** mock/architecture, **6B** OPC UA, **6D** Modbus TCP, **6E** REST, **6F** MQTT: **IMPLEMENTED** / **TESTED**.
- **6C** 10–200 simulated OPC UA servers: **VALIDATED** only; not production capacity.
- MQTT multi-equipment scale (10/50/100/200 + 2×50): **VALIDATED** only; not production capacity.
- **6G** EtherNet/IP (explicit CIP first; library ADR before code): **PLANNED**.
- **6H** PROFINET: **PLANNED** / investigation; p-net is IO-Device not controller; no fake TCP stack.

### Added

- `MqttIndustrialAdapter` and C++ topic mapping structs (`MqttAdapterConfig`).
- Private Paho MQTTAsync session wrapper (`industrial/src/mqtt_session.*`). Public headers do not include Paho or nlohmann/json types.
- Mosquitto test broker helper (`tests/mqtt_test_broker.*`) — localhost, **DEVELOPMENT/INTEGRATION VALIDATION ONLY**.
- Unit test `tests/mqtt_adapter_test.cc`.
- Scalability validation `tests/mqtt_multi_equipment_scalability_test.cc` and `docs/mqtt-scalability-test.md`.

### Changed

- ADR-038: MQTT broker client marked **IMPLEMENTED** / **TESTED** (scale VALIDATED separately).
- ADR-037, 041: 6F recorded as done; next slice is 6G.
- Root CMake adds `mqtt_adapter_test` and `mqtt_multi_equipment_scalability_test`.
- `virtual_factory_industrial` links `libpaho-mqtt3as`. Gazebo plugin still does not link industrial or Paho.

---

## Historical (2026-08-25 REST checkpoint)

Phase 6E REST industrial gateway (2026-08-25): `RestIndustrialAdapter` maps configured HTTP paths and JSON Pointers into `GenericEquipment` via libcurl 8.5.0 and nlohmann/json 3.11.3. One adapter instance = one HTTP origin. In-process HTTP/1.1 fixture; unit test passed. Isolation checked at two localhost origins (correctness only, not production capacity, not vendor certification). TLS verification on by default; Basic/Bearer supported; passwords/tokens not logged. **Phase 6 remains IN PROGRESS.** Architecture + mock + OPC UA + Modbus TCP + REST **COMPLETE**. MQTT **NOT IMPLEMENTED**. Phase 7 has **not** started. SoT PDF regenerated to record 6E implemented.

- **6A** mock/architecture, **6B** OPC UA, **6D** Modbus TCP, **6E** REST: **IMPLEMENTED** / **TESTED**.
- **6C** 10–200 simulated OPC UA servers: **VALIDATED** only; not production capacity.
- **6F** MQTT (one broker / Paho C candidate): **PLANNED**.
- **6G** EtherNet/IP (explicit CIP first; library ADR before code): **PLANNED**.
- **6H** PROFINET: **PLANNED** / investigation; p-net is IO-Device not controller; no fake TCP stack.

### Added

- `RestIndustrialAdapter` and C++ HTTP/JSON mapping structs (`RestAdapterConfig`).
- Private libcurl HTTP session wrapper (`industrial/src/http_session.*`). Public headers do not include `<curl/curl.h>` or `<nlohmann/json.hpp>`.
- In-process HTTP/1.1 test fixture exposing multiple generic test machines (mixer, pump, unknown) — localhost, **DEVELOPMENT/INTEGRATION VALIDATION ONLY**.
- Unit test `tests/rest_adapter_test.cc`.

### Changed

- ADR-037: REST gateway client marked **IMPLEMENTED** / **TESTED**.
- ADR-013, 019, 022, 028, 036, 038, 041: 6E recorded as done; next slice is 6F.
- Root CMake adds `rest_adapter_test`.
- `virtual_factory_industrial` links libcurl. nlohmann/json is private include-only. Gazebo plugin still does not link industrial.

---

## Historical (2026-08-24 documentation checkpoint)

Phase 6 **authority / documentation** update (2026-08-24): official Phases remain **1–11**. Phase 6 now has implementation slices **6A–6H** (ADR-041). SoT PDF regenerated. Previous PDF archived as `docs/archive/MES_SCADA_Virtual_Factory_Source_of_Truth_legacy_2026-08-24.pdf`. **No adapter implementation code in this increment.** REST/MQTT/EtherNet/IP/PROFINET remain **NOT IMPLEMENTED**. Phase 7 remains **NOT STARTED**. Do not start 6E until separately approved.

- **6A** mock/architecture, **6B** OPC UA, **6D** Modbus TCP: **IMPLEMENTED** / **TESTED**.
- **6C** 10–200 simulated OPC UA servers: **VALIDATED** only; not production capacity.
- **6E** REST gateway (HTTP client, libcurl candidate): **PLANNED**.
- **6F** MQTT (one broker / Paho C candidate): **PLANNED**.
- **6G** EtherNet/IP (explicit CIP first; library ADR before code): **PLANNED**.
- **6H** PROFINET: **PLANNED** / investigation; p-net is IO-Device not controller; no fake TCP stack.

ADRs: 013 amended; 022/025/026/028/036 refreshed; **037–041** added.

Phase 6 Modbus TCP adapter (2026-08-24): `ModbusIndustrialAdapter` maps configured coils/registers into `GenericEquipment` via libmodbus 3.1.10. One adapter instance = one TCP endpoint. In-process libmodbus slave fixture; unit test passed. Isolation checked at 2 and 4 localhost endpoints (correctness only, not production capacity). **Phase 6 remains IN PROGRESS.** Architecture + mock + OPC UA + Modbus TCP **COMPLETE**. REST **NOT IMPLEMENTED**. Phase 7 has **not** started. SoT PDF not modified (realizes existing SoT Modbus path; ADR-036).

Project implementation governance (2026-08-24): standing Cursor rules added under `.cursor/rules/` (SoT/phase discipline, architecture invariants, plan-then-approve workflow). **No application code. No phase started.**

MES + SCADA + Virtual Factory architecture **extension** (2026-08-24): **documentation / SoT only.** Future MES now includes configurable plant hierarchy, dynamic PLC/equipment onboarding, work centers, resource readiness with specific reasons, materials/scrap, OEE vs broader efficiency, downtime, quality, genealogy, analytics periods, scheduling/bottlenecks, personnel/tools/maintenance availability, and Siemens Opcenter as a **capability benchmark only**. SoT PDF regenerated from `docs/source/MES_SCADA_Virtual_Factory_Source_of_Truth.md`. Previous PDF archived. **No application code added. Phase 7 remains NOT IMPLEMENTED.** Phase 6 remaining work (Modbus/REST) unchanged. ADRs 027–035.

OPC UA multi-PLC scalability **validation** (2026-08-24): dedicated in-process test of many independent `OpcUaIndustrialAdapter` instances (one client / one endpoint each). Not a production feature and not a production-architecture change. Validated at 100 and 200 simulated servers under the conditions in `docs/opcua-scalability-test.md`. **Not** “production proven for hundreds of PLCs.” **Phase 6 remains IN PROGRESS.** Modbus / REST **NOT IMPLEMENTED**. Phase 7 has **not** started.

Phase 6C follow-on (2026-08-23): **one OPC UA adapter instance per OPC UA server** (ADR-026). Multi-PLC is composition of adapters, not multiple `UA_Client`s inside one `connectionState()`. Tests cover two in-process servers, independent fault, and independent reconnect. **Phase 6 remains IN PROGRESS.** Modbus / REST **NOT IMPLEMENTED**. Phase 7 has **not** started.

Phase 6C — production OPC UA adapter (2026-08-23): `OpcUaIndustrialAdapter` maps configured nodes into `GenericEquipment` via open62541. In-process test server; unit test passed. **Phase 6 remains IN PROGRESS.** Architecture + mock + OPC UA **COMPLETE**. Modbus / REST **NOT IMPLEMENTED**. Phase 7 has **not** started. SoT PDF not modified (realizes existing SoT OPC UA path; ADR-025).

### Added

- `ModbusIndustrialAdapter` and C++ register-mapping structs (`ModbusAdapterConfig`).
- Private libmodbus TCP session wrapper (`industrial/src/modbus_tcp_session.*`). Public headers do not include `<modbus.h>`.
- In-process Modbus TCP test slave exposing multiple generic test machines (mixer, pump, unknown) — localhost, no TLS, **DEVELOPMENT ONLY**.
- Unit test `tests/modbus_adapter_test.cc`.
- ADR-036: Modbus TCP adapter maps configured registers into GenericEquipment; libmodbus; one session per endpoint; comms vs machine fault; no addresses on Equipment.
- `OpcUaIndustrialAdapter` and C++ node-mapping structs (`OpcUaAdapterConfig`).
- In-process OPC UA test server exposing multiple generic test machines (mixer, pump, unknown) — localhost, SecurityPolicy#None, anonymous, **DEVELOPMENT ONLY**.
- Unit test `tests/opcua_adapter_test.cc`.
- Test-only multi-server fixture `tests/opcua_scale_plc_server.*` and `tests/opcua_multi_server_scalability_test.cc` (validation; not a production component).
- ADR-025: OPC UA adapter maps configured nodes into GenericEquipment; open62541; comms vs machine fault; no NodeIds on Equipment.
- ADR-027–035: plant hierarchy, dynamic onboarding, readiness reasons, MES vs equipment state, materials/scrap, OEE vs efficiency, events/analytics, genealogy, Opcenter benchmark.
- ADR-037–041: REST gateway client; MQTT one-broker; EtherNet/IP explicit CIP + library-before-code; PROFINET investigation-first; Phase 6 slices 6A–6H inside official Phases 1–11.

### Changed

- `virtual_factory_industrial` links open62541 and libmodbus. Gazebo plugin still does not.
- `GenericEquipment::setOperationalState` for adapter mapping of Running without executing a command.
- Root CMake adds `opcua_adapter_test`.
- Root CMake adds `opcua_multi_server_scalability_test` (long TIMEOUT).
- Root CMake adds `modbus_adapter_test`.

### Documentation

- Phase 6 marked **IN PROGRESS** with slices **6A–6H** (ADR-041): 6A–6D done; 6E–6H not implemented. Official phases remain 1–11.
- Security: unsecured localhost OPC UA and Modbus tests are not production industrial security.
- SoT PDF regenerated (2026-08-24) from `docs/source/`. Previous PDF in `docs/archive/`.
- `docs/opcua-scalability-test.md`: measured multi-PLC OPC UA validation (architectural capability vs measured scale).
- ADR-036 and architecture §4.2 document the Modbus TCP adapter boundary.

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
