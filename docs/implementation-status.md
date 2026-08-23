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
| HEAD commit | Phase 5–6 checkpoint on `master`. Use `git log -1` for the hash. |
| Working tree | **Clean** after this checkpoint commit. |
| Remote | `origin/master` |
| Audit date | 2026-08-23 |

Use `git status` and `git log -1` when resuming; this file is not a substitute for Git.

---

## 3. Current phase

**Phase 6 — Industrial Adapter Layer — IN PROGRESS**

| Slice | Status |
| --- | --- |
| Adapter architecture (`IndustrialAdapter`) | **COMPLETE** |
| Mock adapter (`MockIndustrialAdapter`) | **COMPLETE** |
| Production OPC UA adapter (`OpcUaIndustrialAdapter`) | **COMPLETE** (unit tests passed) |
| Production Modbus adapter | **NOT IMPLEMENTED** |
| Production REST adapter | **NOT IMPLEMENTED** |

---

## 4. Completed phases

| Phase | Name | Status |
| --- | --- | --- |
| 1 | Factory Foundation | **COMPLETE** |
| 2 | Equipment Plugin Foundation | **COMPLETE** |
| 3 | Conveyor Control | **COMPLETE** |
| 4 | Product Motion | **COMPLETE** (runtime verified) |
| 5 | Industrial Equipment Abstraction | **COMPLETE** (open-ended / capability-driven) |
| 6 | Industrial Adapter Layer | **IN PROGRESS** (architecture + mock + OPC UA complete; Modbus/REST not implemented) |

---

## 5. Current phase status (Phase 6)

Implemented:

- Adapter identity and protocol metadata (`protocol()` → `"mock"` or `"opcua"`)
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

Not implemented: Modbus, REST, MQTT, EtherNet/IP; production SignAndEncrypt / certificates; subscriptions/history/alarms.

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
  src/MockIndustrialAdapter.cc
  src/OpcUaIndustrialAdapter.cc
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

Gazebo plugin does **not** link `virtual_factory_industrial` or open62541. `IndustrialAdapter.hh` does **not** include open62541. Equipment headers do not include protocol SDKs.

---

## 7. Build status

Last verified 2026-08-23.

```bash
cmake -S . -B build
cmake --build build
```

Equipment and industrial libraries compile. Plugin:

```bash
cmake --build gazebo/plugins/conveyor/build
```

Result: `ConveyorSystem` builds.

---

## 8. Test status

```bash
ctest --test-dir build --output-on-failure
```

| Test | Result |
| --- | --- |
| `equipment_test` | **PASSED** (2026-08-23) |
| `industrial_adapter_test` | **PASSED** (2026-08-23) |
| `opcua_adapter_test` | **PASSED** (2026-08-23) |

3/3 tests passed.

---

## 9. Gazebo runtime verification

Headless 6000 iterations (2026-08-23):

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

Kinematic result: −1.5 m → 0.5 m at 0.5 m/s (same as Phase 4).

---

## 10. Known limitations

1. Development Start/Stop timers (1000 / 5000) remain (ADR-017).
2. Product motion is pose integration, not a physical belt (ADR-016).
3. Belt visual motion still uses linear velocity on a static model.
4. Mock adapter is exercised in unit tests only; no MES consumer.
5. OPC UA test/server path is unsecured localhost (SecurityPolicy#None, anonymous) — **DEVELOPMENT ONLY**.
6. Command `parameter` is a single `double`.
7. Plugin/model paths require `GZ_SIM_*` environment variables.
8. SoT PDF was not regenerated (Markdown records implementation; architecture matches existing SoT).

---

## 11. Not implemented

Label: **NOT IMPLEMENTED** / **PLANNED**.

- Production Modbus, REST, MQTT, EtherNet/IP adapters
- OPC UA SignAndEncrypt, certificates, subscriptions, history, alarms/conditions
- MES (orders, Resource Management, materials, execution, quality, traceability)
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

**Phase 7 — MES Core — NOT IMPLEMENTED / NOT STARTED.**

Intended scope (all **PLANNED**, none in code): production orders; product/process definitions; routing; **Resource Management** (equipment, work centers, personnel, tools, materials; capability vs availability; allocation; reservation); scheduling; dispatch; execution tracking; material management; quality; sampling; traceability; downtime; OEE; maintenance integration; reporting. See `architecture.md` §10 and ADR-024.

Do not implement Phase 7 until explicitly instructed. Do not put scheduling logic in `Equipment` or `IndustrialAdapter`.

Remaining Phase 6 follow-on (not started): production Modbus and REST adapters implementing `IndustrialAdapter`. OPC UA is done (ADR-025).

---

## 13. Resume instructions

1. Read `docs/README.md` → SoT PDF → this file → `architecture.md` → `decisions.md` → `roadmap.md` → `CHANGELOG.md`.
2. `git status` and `git log --oneline --decorate -10`.
3. Inspect `equipment/`, `industrial/`, `gazebo/plugins/conveyor/`, `tests/`.
4. `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`
5. `cmake --build gazebo/plugins/conveyor/build`
6. Do **not** start MES (including Resource Management), SCADA, GUI, auth, or database. Do **not** infer those from the roadmap.
