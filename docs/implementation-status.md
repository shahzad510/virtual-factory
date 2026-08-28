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
| HEAD commit | `024b0b1` → see `git log -1` after gateway formalization commit |
| Working tree | Use `git status`. |
| Remote | `origin/master` |
| Audit date | 2026-08-28 (Phase 6 final audit) |

Use `git status` and `git log -1` when resuming; this file is not a substitute for Git.

---

## 3. Current phase

**Phase 6 — Industrial Adapter Layer — COMPLETE** (final audit 2026-08-28)

| Slice | Status |
| --- | --- |
| **6A** Adapter architecture (`IndustrialAdapter`) + mock | **COMPLETE** / **TESTED** |
| **6B** Production OPC UA adapter (`OpcUaIndustrialAdapter`) | **COMPLETE** / **TESTED** |
| **6C** OPC UA multi-server scalability validation (10–200 simulated in-process servers) | **VALIDATED** (those conditions). **Not** production capacity certification |
| **6D** Production Modbus TCP adapter (`ModbusIndustrialAdapter`) | **COMPLETE** / **TESTED** |
| **6E** REST industrial gateway adapter (`RestIndustrialAdapter`) | **COMPLETE** / **TESTED** (localhost HTTP fixture; **not** vendor certification) |
| **6F** MQTT industrial adapter (`MqttIndustrialAdapter`) | **COMPLETE** / **TESTED** (localhost Mosquitto; **not** vendor certification). Multi-equipment scale **VALIDATED** (10/50/100/200 + 2×50; see `docs/mqtt-scalability-test.md`) |
| **6G** EtherNet/IP industrial adapter (`EtherNetIpIndustrialAdapter`) | **COMPLETE** / **TESTED** (libplctag explicit messaging; local `ab_server`; **not** hardware certification). Two-device isolation **VALIDATED** under test conditions |
| **6H** PROFINET | **SUPPORTED VIA GATEWAY** (ADR-040). Native IO-Controller **DEFERRED**. See `docs/profinet-gateway-integration.md` |

---

## 4. Completed phases

| Phase | Name | Status |
| --- | --- | --- |
| 1 | Factory Foundation | **COMPLETE** |
| 2 | Equipment Plugin Foundation | **COMPLETE** |
| 3 | Conveyor Control | **COMPLETE** |
| 4 | Product Motion | **COMPLETE** (runtime verified) |
| 5 | Industrial Equipment Abstraction | **COMPLETE** (open-ended / capability-driven) |
| 6 | Industrial Adapter Layer | **COMPLETE** (6A–6G implemented/tested; 6H supported via gateway; native PN deferred) |

---

## 5. Current phase status (Phase 6)

Implemented:

- Adapter identity and protocol metadata (`protocol()` → `"mock"`, `"opcua"`, `"modbus"`, `"rest"`, `"mqtt"`, or `"ethernetip"`)
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
- `RestIndustrialAdapter` (libcurl HTTP client + nlohmann/json mapping, ADR-037)
- Multiple REST origins via **multiple adapter instances** (one HTTP origin per instance)
- In-process HTTP/1.1 fixture and unit test `rest_adapter_test` (connect/poll/commands, multi-telemetry JSON, GenericEquipment, isolation, HTTP error, timeout, Faulted vs `Equipment::fault()`, explicit reconnect, two origins, Basic/Bearer). **DEVELOPMENT/INTEGRATION VALIDATION ONLY** — not vendor certification
- `MqttIndustrialAdapter` (Paho MQTT C MQTTAsync client + nlohmann/json mapping, ADR-038)
- Multiple MQTT brokers via **multiple adapter instances** (one broker/session per instance); several machines on one broker via mappings
- Mosquitto test broker helper and unit test `mqtt_adapter_test` (connect/poll/commands, JSON/numeric/boolean telemetry, GenericEquipment, two adapters on one broker, two brokers, client IDs, QoS 0/1/2, retained telemetry, malformed payload, Faulted vs `Equipment::fault()`, explicit reconnect, bounded poll, username/password, TLS verify default). **DEVELOPMENT/INTEGRATION VALIDATION ONLY** — not vendor/cloud certification
- Multi-equipment MQTT scalability **test** (`mqtt_multi_equipment_scalability_test`): **VALIDATED** at 10/50/100/200 mappings on one broker and 2×50 on two brokers under those conditions. See `docs/mqtt-scalability-test.md`. **Not** production capacity certification. **Not** “supports 200 PLCs.”
- `EtherNetIpIndustrialAdapter` (libplctag 2.7.1 explicit CIP tag messaging, ADR-039)
- Multiple EtherNet/IP devices via **multiple adapter instances** (one device/session per instance); several logical machines on one device via mappings
- libplctag `ab_server` ControlLogix emulator fixture and unit test `eip_adapter_test` (connect/poll/commands, multi-equipment, isolation, timeout/refused/invalid tag, Faulted vs `Equipment::fault()`, explicit reconnect, two devices). **DEVELOPMENT/INTEGRATION VALIDATION ONLY** — not Allen-Bradley/Rockwell hardware certification. Two-device isolation **VALIDATED** under those test conditions — **not** production capacity

- **PROFINET (6H):** supported via industrial gateway → OPC UA / Modbus / REST / MQTT (ADR-040, `docs/profinet-gateway-integration.md`). Native IO-Controller **deferred**.

Not implemented in adapters: native PROFINET IO-Controller; Class 1 implicit/cyclic EtherNet/IP I/O; Sparkplug B; MQTT 5 architecture; MQTT wildcards; REST DELETE; Modbus RTU/TLS/batch writes; production OPC UA SignAndEncrypt / certificates; subscriptions/history/alarms.

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
    RestIndustrialAdapter.hh            mapping config + REST gateway adapter
    MqttIndustrialAdapter.hh            mapping config + MQTT broker adapter
    EtherNetIpIndustrialAdapter.hh      mapping config + EtherNet/IP adapter
  src/MockIndustrialAdapter.cc
  src/OpcUaIndustrialAdapter.cc
  src/ModbusIndustrialAdapter.cc
  src/modbus_tcp_session.cc             private libmodbus client wrapper
  src/RestIndustrialAdapter.cc
  src/http_session.cc                   private libcurl client wrapper
  src/MqttIndustrialAdapter.cc
  src/mqtt_session.cc                   private Paho MQTTAsync client wrapper
  src/EtherNetIpIndustrialAdapter.cc
  src/eip_session.hh/.cc                private libplctag session wrapper
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
tests/rest_adapter_test.cc
tests/rest_test_server.hh/.cc           TEST ONLY: HTTP/1.1 fixture, localhost
tests/mqtt_adapter_test.cc
tests/mqtt_test_broker.hh/.cc           TEST ONLY: Mosquitto process, localhost
tests/mqtt_multi_equipment_scalability_test.cc  VALIDATION ONLY; not a production component
tests/eip_adapter_test.cc
tests/eip_test_server.hh/.cc            TEST ONLY: libplctag ab_server, localhost
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
- `virtual_factory::RestIndustrialAdapter` (libcurl 8.5.0 + nlohmann/json 3.11.3)
- `virtual_factory::MqttIndustrialAdapter` (Paho MQTT C 1.3.13)
- `virtual_factory::EtherNetIpIndustrialAdapter` (libplctag 2.7.1)

Gazebo plugin does **not** link `virtual_factory_industrial`, open62541, libmodbus, libcurl, Paho, or libplctag. `IndustrialAdapter.hh` does **not** include open62541, libmodbus, curl, nlohmann/json, Paho, or libplctag. Equipment headers do not include protocol SDKs.

---

## 7. Build status

Last verified 2026-08-28.

```bash
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

Equipment and industrial libraries compile (including libplctag via `.deps/libplctag` when system package absent). Plugin:

```bash
cmake -S gazebo/plugins/conveyor -B gazebo/plugins/conveyor/build
cmake --build gazebo/plugins/conveyor/build
```

Result: `ConveyorSystem` builds. Gazebo was not modified for EtherNet/IP.

---

## 8. Test status

```bash
ctest --test-dir build --output-on-failure
```

| Test | Result |
| --- | --- |
| `equipment_test` | **PASSED** (2026-08-28) |
| `industrial_adapter_test` | **PASSED** (2026-08-28) |
| `opcua_adapter_test` | **PASSED** (2026-08-28) |
| `opcua_multi_server_scalability_test` | **VALIDATED** (2026-08-28, ~25 s). See `docs/opcua-scalability-test.md`. In-process simulated servers; **not** production hardware proof. |
| `modbus_adapter_test` | **PASSED** (2026-08-28) |
| `rest_adapter_test` | **PASSED** (2026-08-28). Local HTTP fixture; **not** vendor certification |
| `mqtt_adapter_test` | **PASSED** (2026-08-28). Local Mosquitto; **not** vendor/cloud certification |
| `mqtt_multi_equipment_scalability_test` | **VALIDATED** (2026-08-28, ~210 s). See `docs/mqtt-scalability-test.md`. Local Mosquitto; **not** production capacity |
| `eip_adapter_test` | **PASSED** (2026-08-28, ~6 s). Local libplctag `ab_server`; **not** Allen-Bradley/Rockwell hardware certification. Two-device isolation **VALIDATED** under test conditions |

Full suite: **9/9 passed** (see `git log` / latest regression run). Gateway-backed PROFINET path validated by existing multi-equipment adapter tests (see `docs/profinet-gateway-integration.md` §7). No native `profinet_adapter_test`. **Native PROFINET gateway hardware not tested in CI.**

---

## 9. Gazebo runtime verification

Headless 6000 iterations (2026-08-25, after MQTT adapter work; Gazebo sources unchanged for 6G):

| Claim | Status |
| --- | --- |
| Plugin loads; CV-001 configured | **NOT RE-RUN** (2026-08-28 audit; `gz-sim` not available in this CI environment) |
| PRODUCT-001 found | **NOT RE-RUN** (prior 2026-08-25 verification retained; sources unchanged) |
| Heartbeat `type=belt_conveyor`; speed from telemetry `"speed"` | **NOT RE-RUN** |
| Product held at X=−1.5 m while stopped | **NOT RE-RUN** |
| START ~update 1000, 0.5 m/s | **NOT RE-RUN** |
| STOP ~update 5000; rest X=0.5 m | **NOT RE-RUN** |
| `dt=0.001 s` | **NOT RE-RUN** |
| Interactive GUI | **NOT TESTED** (headless only) |

6G did not modify Gazebo plugin sources or `virtual_factory_equipment`. Prior headless verification (2026-08-25) remains the last executed Gazebo regression. Re-run locally when `gz-sim8` is installed.

Kinematic result: −1.5 m → 0.5 m at 0.5 m/s (same as Phase 4). Development START/STOP timers unchanged.

---

## 10. Known limitations

1. Development Start/Stop timers (1000 / 5000) remain (ADR-017).
2. Product motion is pose integration, not a physical belt (ADR-016).
3. Belt visual motion still uses linear velocity on a static model.
4. Mock adapter is exercised in unit tests only; no MES consumer.
5. OPC UA test/server path is unsecured localhost (SecurityPolicy#None, anonymous) — **DEVELOPMENT ONLY**.
6. Modbus test slave is unsecured localhost TCP (no TLS, no authentication) — **DEVELOPMENT ONLY**.
7. REST test fixture is unsecured localhost HTTP (no TLS on the fixture) — **DEVELOPMENT/INTEGRATION VALIDATION ONLY**, not vendor API certification.
8. MQTT tests use a local Mosquitto process — **DEVELOPMENT/INTEGRATION VALIDATION ONLY**, not cloud/vendor MQTT certification. 6F does not implement Sparkplug B, MQTT 5, wildcard subscriptions, or an MES event bus.
9. EtherNet/IP tests use libplctag `ab_server` — **DEVELOPMENT/INTEGRATION VALIDATION ONLY**, not Allen-Bradley/Rockwell hardware certification. 6G implements explicit tag messaging only; Class 1 implicit/cyclic I/O is **NOT IMPLEMENTED**.
10. Command `parameter` is a single `double`. Holding-register writes clip to uint16. REST/MQTT command bodies substitute `{{value}}`.
11. Plugin/model paths require `GZ_SIM_*` environment variables.
12. Intended Modbus dependency is Ubuntu `libmodbus-dev` / `libmodbus5` 3.1.10-1ubuntu1. nlohmann-json intended package is `nlohmann-json3-dev` 3.11.3. Paho intended package is `libpaho-mqtt-dev` 1.3.13. Mosquitto is test-only (`mosquitto` 2.0.18). libplctag intended version is **2.7.1** (commit `bdb10aeaf4f374cec7ae4e66887446dedf952dc1`, MPL-2.0); build to gitignored `.deps/libplctag` when not system-installed. open62541 1.4.0-rc2 may be built to `.deps/open62541` when `libopen62541-dev` is unavailable. libcurl is the system `libcurl4-openssl-dev` 8.5.0.
13. SoT PDF is generated from `docs/source/MES_SCADA_Virtual_Factory_Source_of_Truth.md` (`docs/source/generate-sot-pdf.sh`). Previous PDFs are in `docs/archive/` and are not authoritative. The Markdown source is how the PDF is maintained, not a second live SoT.

---

## 11. Not implemented

Label: **NOT IMPLEMENTED** / **PLANNED**.

- Native PROFINET IO-Controller in MES (**deferred**; gateway path is supported)
- Class 1 implicit/cyclic EtherNet/IP I/O (UDP 2222); EtherNet/IP hardware certification beyond `ab_server` fixture
- REST DELETE, background reconnect, production vendor HTTPS certification
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

Phase 6 is **COMPLETE** (final audit 2026-08-28). Do not implement Phase 7 until explicitly instructed. Do not put scheduling logic in `Equipment` or `IndustrialAdapter`.
