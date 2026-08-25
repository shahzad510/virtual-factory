# Implementation Status

> What **actually exists** in the repository right now.
> Do not treat planned architecture as implemented.

**Architectural authority:** [`MES_SCADA_Virtual_Factory_Source_of_Truth.pdf`](MES_SCADA_Virtual_Factory_Source_of_Truth.pdf)  
**Do not trust** [`archive/`](archive/) for current architecture.

---

## 1. Project identity

MES + SCADA + industrial adapter platform. Gazebo Sim 8 is a **simulation plant** used to develop and test the same normalized equipment model that real adapters will feed later.

Gazebo is not MES. `ConveyorSystem` is not SCADA. The mock adapter is not a production protocol.

---

## 2. Current Git revision

| Item | Value |
| --- | --- |
| Branch | `master` (tracks `origin/master`) |
| HEAD commit | Use `git log -1` for the hash. This checkpoint includes Phase 5 equipment, Phase 6A–6D (mock, OPC UA, OPC UA scale validation, Modbus TCP), and Phase 6 slice documentation 6A–6H. |
| Working tree | Use `git status`. After this checkpoint, REST/MQTT/EtherNet/IP/PROFINET remain unimplemented. |
| Remote | `origin/master` |
| Audit date | 2026-08-24 |

Use `git status` and `git log -1` when resuming; this file is not a substitute for Git.

---

## 3. Current phase

**Phase 6 — Industrial Adapter Layer — IN PROGRESS**

| Slice | Status |
| --- | --- |
| **6A** Adapter architecture (`IndustrialAdapter`) + mock | **COMPLETE** / **TESTED** |
| **6B** Production OPC UA adapter (`OpcUaIndustrialAdapter`) | **COMPLETE** / **TESTED** |
| **6C** OPC UA multi-server scalability validation (10–200 simulated in-process servers) | **VALIDATED** (those conditions). **Not** production capacity certification |
| **6D** Production Modbus TCP adapter (`ModbusIndustrialAdapter`) | **COMPLETE** / **TESTED** |
| **6E** REST industrial gateway adapter | **NOT IMPLEMENTED** |
| **6F** MQTT industrial adapter | **NOT IMPLEMENTED** |
| **6G** EtherNet/IP industrial adapter | **NOT IMPLEMENTED** |
| **6H** PROFINET | **NOT IMPLEMENTED** / investigation required (ADR-040). Do not fake a stack |

---

## 4. Completed phases

| Phase | Name | Status |
| --- | --- | --- |
| 1 | Factory Foundation | **COMPLETE** |
| 2 | Equipment Plugin Foundation | **COMPLETE** |
| 3 | Conveyor Control | **COMPLETE** |
| 4 | Product Motion | **COMPLETE** (runtime verified) |
| 5 | Industrial Equipment Abstraction | **COMPLETE** (open-ended / capability-driven) |
| 6 | Industrial Adapter Layer | **IN PROGRESS** (slices 6A–6D done; 6E–6H not implemented) |

---

## 5. Current phase status (Phase 6)

Implemented:

- Adapter identity and protocol metadata (`protocol()` → `"mock"`, `"opcua"`, or `"modbus"`)
- Connection state: Disconnected / Connected / Faulted
- Connect / disconnect
- Equipment exposure and lookup (`equipment()`, `equipmentById()`)
- Polling (`poll()`)
- Command path via `Equipment::execute`
- Telemetry via `Equipment::telemetry`
- Communication fault distinct from machine `fault()`
- Arbitrary devices via `GenericEquipment` (no Robot/Pump C++ classes)
- `MockIndustrialAdapter` and unit test `industrial_adapter_test`
- `OpcUaIndustrialAdapter` (open62541 client, C++ node mapping, ADR-025)
- Multiple OPC UA servers via **multiple adapter instances** (one `UA_Client` / endpoint per instance, ADR-026)
- In-process OPC UA test server and unit test `opcua_adapter_test` (single- and multi-server)
- Multi-PLC OPC UA scalability **test** (`opcua_multi_server_scalability_test`): validation only. See `docs/opcua-scalability-test.md`. Validated at 100 and 200 simulated in-process servers under those conditions. **Not** production hardware proof. **Not** “supports unlimited PLCs.”
- `ModbusIndustrialAdapter` (libmodbus 3.1.10 TCP client, C++ register mapping, ADR-036)
- Multiple Modbus TCP endpoints via **multiple adapter instances** (one TCP session per instance)
- In-process libmodbus TCP slave fixture and unit test `modbus_adapter_test` (connect/poll/commands, multi-equipment, isolation, Faulted vs `Equipment::fault()`, explicit reconnect, two endpoints, modest 4-endpoint isolation)

Not implemented: REST (6E), MQTT (6F), EtherNet/IP (6G), PROFINET (6H); Modbus RTU/TLS/batch writes; production OPC UA SignAndEncrypt / certificates; subscriptions/history/alarms.

---

## 6. Implemented files / components

```text
CMakeLists.txt                          equipment + industrial + tests (no Gazebo)
equipment/
  include/virtual_factory/equipment/
    Equipment.hh                        generic contract (no gz includes)
    GenericEquipment.hh                 configurable / arbitrary type
    Conveyor.hh                         Gazebo simulation example
  src/Conveyor.cc
  src/GenericEquipment.cc
industrial/
  include/virtual_factory/industrial/
    IndustrialAdapter.hh                protocol-oriented adapter contract
    MockIndustrialAdapter.hh
    OpcUaIndustrialAdapter.hh           mapping config + OPC UA adapter
    ModbusIndustrialAdapter.hh          mapping config + Modbus TCP adapter
  src/MockIndustrialAdapter.cc
  src/OpcUaIndustrialAdapter.cc
  src/ModbusIndustrialAdapter.cc
  src/modbus_tcp_session.cc             private libmodbus client wrapper
gazebo/
  worlds/phase1/factory.sdf
  models/conveyor/                      CV-001 (static)
  models/product/                       PRODUCT-001
  plugins/conveyor/
    ConveyorSystem.hh/.cc               Gazebo System; uses Conveyor
    CMakeLists.txt                      links virtual_factory_equipment only
tests/equipment_test.cc
tests/industrial_adapter_test.cc
tests/opcua_adapter_test.cc
tests/opcua_test_server.hh/.cc          DEVELOPMENT ONLY: None + anonymous
tests/opcua_scale_plc_server.hh/.cc     TEST ONLY: one simulated PLC per server
tests/opcua_multi_server_scalability_test.cc  validation only; not a production component
tests/modbus_adapter_test.cc
tests/modbus_test_server.hh/.cc         TEST ONLY: libmodbus TCP slave, localhost
```

### Simulation (Gazebo)

- World, floor, static CV-001, PRODUCT-001
- `ConveyorSystem`: Configure / PreUpdate, belt, product pose, development Start@1000 / Stop@5000
- Product X integration using `dt` in **seconds**
- Plugin artifact: `gazebo/plugins/conveyor/build/libConveyorSystem.so` (gitignored)

### Normalized equipment (Phase 5)

- `virtual_factory::Equipment` — id, type metadata, operational state, fault, capabilities, `execute`, telemetry `{name, value, unit}`, `status()`
- `virtual_factory::GenericEquipment` — no new class per machine
- `virtual_factory::Conveyor` — specialized belt example (`speed()`, `setSpeed()`, capability `speed_control`, command `set_speed`, telemetry `"speed"`)
- Supporting types: `OperationalState`, `TelemetryPoint`, `CommandResult`, `EquipmentStatus`

### Industrial adapters (Phase 6)

- `virtual_factory::IndustrialAdapter`
- `virtual_factory::ConnectionState`
- `virtual_factory::MockIndustrialAdapter`
- `virtual_factory::OpcUaIndustrialAdapter` (open62541 1.4.0-rc2)
- `virtual_factory::ModbusIndustrialAdapter` (libmodbus 3.1.10)

Gazebo plugin does **not** link `virtual_factory_industrial`, open62541, or libmodbus. `IndustrialAdapter.hh` does **not** include open62541 or libmodbus. Equipment headers do not include protocol SDKs.

---

## 7. Build status

Last verified 2026-08-24.

```bash
cmake -S . -B build
cmake --build build
```

Equipment and industrial libraries compile. Plugin:

```bash
cmake -S gazebo/plugins/conveyor -B gazebo/plugins/conveyor/build
cmake --build gazebo/plugins/conveyor/build
```

Result: `ConveyorSystem` builds. Gazebo was not modified for Modbus.

---

## 8. Test status

```bash
ctest --test-dir build --output-on-failure
```

| Test | Result |
| --- | --- |
| `equipment_test` | **PASSED** (2026-08-24) |
| `industrial_adapter_test` | **PASSED** (2026-08-24) |
| `opcua_adapter_test` | **PASSED** (2026-08-24) |
| `opcua_multi_server_scalability_test` | **VALIDATED** (2026-08-24, 259 s). See `docs/opcua-scalability-test.md`. In-process simulated servers; **not** production hardware proof. |
| `modbus_adapter_test` | **PASSED** (2026-08-24) |

Short unit tests including Modbus: 4/4 passed. The OPC UA scalability test is a separate long-running validation, not a claim of unlimited PLC scale. The Modbus 4-endpoint loop is modest localhost isolation, **not** a production capacity claim.

---

## 9. Gazebo runtime verification

Headless 6000 iterations (2026-08-24, after Modbus adapter work; Gazebo sources unchanged):

| Claim | Status |
| --- | --- |
| Plugin loads; CV-001 configured | VERIFIED |
| PRODUCT-001 found | VERIFIED |
| Heartbeat `type=belt_conveyor`; speed from telemetry `"speed"` | VERIFIED |
| Product held at X=−1.5 m while stopped | VERIFIED |
| START ~update 1000, 0.5 m/s | VERIFIED |
| STOP ~update 5000; rest X=0.5 m | VERIFIED |
| `dt=0.001 s` | VERIFIED |
| Interactive GUI | **NOT TESTED** (headless only) |

Kinematic result: −1.5 m → 0.5 m at 0.5 m/s (same as Phase 4). Development START/STOP timers unchanged.

---

## 10. Known limitations

1. Development Start/Stop timers (1000 / 5000) remain (ADR-017).
2. Product motion is pose integration, not a physical belt (ADR-016).
3. Belt visual motion still uses linear velocity on a static model.
4. Mock adapter is exercised in unit tests only; no MES consumer.
5. OPC UA test/server path is unsecured localhost (SecurityPolicy#None, anonymous) — **DEVELOPMENT ONLY**.
6. Modbus test slave is unsecured localhost TCP (no TLS, no authentication) — **DEVELOPMENT ONLY**.
7. Command `parameter` is a single `double`. Holding-register writes clip to uint16.
8. Plugin/model paths require `GZ_SIM_*` environment variables.
9. Intended Modbus dependency is Ubuntu `libmodbus-dev` / `libmodbus5` 3.1.10-1ubuntu1. This environment could not `sudo apt install` (password required); the same debs were extracted to gitignored `.deps/libmodbus` for the build. Developers should install the packages with apt.
10. SoT PDF is generated from `docs/source/MES_SCADA_Virtual_Factory_Source_of_Truth.md` (`docs/source/generate-sot-pdf.sh`). Previous PDFs are in `docs/archive/` (including `…legacy_2026-08-22.pdf` and `…legacy_2026-08-24.pdf`) and are not authoritative. The Markdown source is how the PDF is maintained, not a second live SoT.

---

## 11. Not implemented

Label: **NOT IMPLEMENTED** / **PLANNED**.

- Production REST (6E), MQTT (6F), EtherNet/IP (6G) adapters
- Production PROFINET (6H) — investigation required; a TCP mock is not PROFINET
- Modbus RTU, TLS, FC 15/16 batch writes, background reconnect
- OPC UA SignAndEncrypt, certificates, subscriptions, history, alarms/conditions
- MES (orders, Resource Management, plant hierarchy, dynamic PLC onboarding, scheduler, OEE engine, materials, scrap, quality, genealogy, analytics)
- Dynamic PLC management UI/API
- SCADA / HMI
- Application API
- Database
- Authentication / authorization / RBAC
- .NET / C# services
- Blazor / web GUI
- Real PLC / real factory equipment
- OpenPLC
- SEN-001, multi-machine line
- `Robot.hh`, `Pump.hh`, `Oven.hh`, `Mixer.hh` — **intentionally not created**

---

## 12. Next phase

**Phase 7 — MES Core + Resource Management — NOT IMPLEMENTED / NOT STARTED.**

Intended scope (all **PLANNED**, none in code): configurable plant hierarchy; dynamic PLC/equipment onboarding; production orders; routing/BOM/BOP; Resource Management (capability vs availability; work centers; allocation/reservation; capacity); resource readiness with specific hold reasons; scheduling/dispatch; execution tracking; materials; scrap; quality; genealogy; downtime; OEE (distinct from broader efficiency); personnel/tools; maintenance availability; contextualized events and operational analytics. See `architecture.md` §10, SoT PDF §§8–23, ADR-024 and ADR-027–035.

Do not implement Phase 7 until explicitly instructed. Do not put scheduling logic in `Equipment` or `IndustrialAdapter`.

Remaining Phase 6 slices (not started): **6E REST** (next, after separate approval), then 6F MQTT, 6G EtherNet/IP, 6H PROFINET investigation. OPC UA is done (ADR-025). Modbus TCP is done (ADR-036). Slices: ADR-041. **Phase 6 remains IN PROGRESS.** Phase 7 remains **NOT STARTED**. Do **not** start 6E in this documentation-only increment.

---

## 13. Resume instructions

1. Read `docs/README.md` → SoT PDF → this file → `architecture.md` → `decisions.md` → `roadmap.md` → `CHANGELOG.md`.
2. `git status` and `git log --oneline --decorate -10`.
3. Inspect `equipment/`, `industrial/`, `gazebo/plugins/conveyor/`, `tests/`.
4. `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`
5. `cmake --build gazebo/plugins/conveyor/build`
6. Do **not** start MES (including Resource Management), SCADA, GUI, auth, or database. Do **not** start 6E REST until explicitly instructed. Do **not** infer those from the roadmap.
