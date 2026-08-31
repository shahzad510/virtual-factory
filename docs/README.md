# Virtual Factory documentation

This is the first document a new developer or AI agent should read.

MES + SCADA + industrial adapters, with Gazebo Sim as a **simulation plant**. Gazebo is not the MES. The C++ plugin is not the GUI. Adapters are not the MES.

**Do not trust archived documents for current architecture.**

**Do not infer implementation from the roadmap alone.**

---

## Active documentation hierarchy

| Order | Document | Role |
| --- | --- | --- |
| 1 | [MES_SCADA_Virtual_Factory_Source_of_Truth.pdf](MES_SCADA_Virtual_Factory_Source_of_Truth.pdf) | **Architectural authority.** Only active Source of Truth. Generated from [source/MES_SCADA_Virtual_Factory_Source_of_Truth.md](source/MES_SCADA_Virtual_Factory_Source_of_Truth.md) via [source/generate-sot-pdf.sh](source/generate-sot-pdf.sh). |
| 2 | [implementation-status.md](implementation-status.md) | **What is actually implemented** in the repository. |
| 3 | [architecture.md](architecture.md) | Current technical architecture (layers, paths, phase gates). |
| 4 | [decisions.md](decisions.md) | Why decisions were made (ADRs). |
| 5 | [roadmap.md](roadmap.md) | Future phases. Planned ≠ implemented. |
| 6 | [CHANGELOG.md](CHANGELOG.md) | What changed over time. |
| 7 | This file | Resume procedure, commands, index. |
| — | [opcua-scalability-test.md](opcua-scalability-test.md) | OPC UA multi-PLC **validation record** (measured scale ≠ production proof). |
| — | [mqtt-scalability-test.md](mqtt-scalability-test.md) | MQTT multi-equipment **validation record** (measured scale ≠ production proof). |
| — | [icp-product-architecture.md](icp-product-architecture.md) | **ICP** product architecture (ADR-042, ADR-044). |
| — | [icp-configuration.md](icp-configuration.md) | **ICP-1B** persistent configuration format and API. |
| — | [icp-gui-architecture.md](icp-gui-architecture.md) | **ICP standalone GUI** + Application API architecture. |
| — | [icp-standalone-acceptance-report.md](icp-standalone-acceptance-report.md) | **ICP standalone acceptance / validation** (validation branch). |
| — | [mes-core-product-architecture.md](mes-core-product-architecture.md) | **MES Core** product architecture (ADR-045). |
| — | [connectivity-integration-contract.md](connectivity-integration-contract.md) | **CIC** boundary between ICP and MES (ADR-043). |
| — | [profinet-gateway-integration.md](profinet-gateway-integration.md) | Phase 6H PROFINET **supported via gateway** (ADR-040). |
| — | [hilscher-environment-audit.md](hilscher-environment-audit.md) | Hilscher SDK/hardware audit — native fieldbus **BLOCKED** on this host. |
| — | [native-fieldbus-implementation-status.md](native-fieldbus-implementation-status.md) | Native PROFINET/PROFIBUS: software boundary vs hardware pending. |
| — | [hilscher-sdk-license.md](hilscher-sdk-license.md) | libcifx vs proprietary firmware/license boundary. |
| — | [hilscher-hardware-smoke-test.md](hilscher-hardware-smoke-test.md) | Hardware validation plan (do not run without cards). |
| — | [hilscher-hardware-validation-procedure.md](hilscher-hardware-validation-procedure.md) | Step-by-step PN/PB hardware procedure. |
| — | [hilscher-platform-and-docker.md](hilscher-platform-and-docker.md) | Windows/Ubuntu/Docker honesty matrix. |
| — | [hilscher-test-peer-options.md](hilscher-test-peer-options.md) | Legitimate IO-Device / DP slave test peers (research). |
| — | [templates/hilscher-hardware-test-report.md](templates/hilscher-hardware-test-report.md) | Hardware test report template. |
| — | [profibus-native-evaluation.md](profibus-native-evaluation.md) | Native PROFIBUS evaluation (ADR-046). |
| — | [profinet-native-evaluation.md](profinet-native-evaluation.md) | Native PROFINET evaluation (ADR-040 amendment). |
| — | [archive/](archive/) | Historical/legacy documents only. Not authoritative. |
| — | Git history | Historical implementation record. |

When they disagree:

- **Current source code** → implementation reality  
- **SoT PDF** → architectural authority  
- **implementation-status.md** → verified implemented state  
- **decisions.md** → documented rationale  

Do not create a second source of truth. If architecture must change, update `decisions.md`, edit the SoT Markdown source, and regenerate the PDF **intentionally**. The Markdown under `docs/source/` is the maintainable SoT source, not a competing live architecture.

---

## Resume procedure

1. Read the active SoT PDF.
2. Read `implementation-status.md`.
3. Read `architecture.md`.
4. Read `decisions.md`.
5. Read `roadmap.md`.
6. Read `CHANGELOG.md`.
7. Inspect Git status and log (`git status`, `git log --oneline --decorate -10`).
8. Inspect the source tree (`equipment/`, `industrial/`, `gazebo/`, `tests/`).
9. Verify build and tests before modifying code (commands below).
10. Never infer implementation from the roadmap alone.

Continue from **Next phase** in `implementation-status.md`. **ICP-1A, ICP-1B, and ICP standalone GUI IMPLEMENTED / TESTED** (GUI on isolated branch). **ICP-1C, ICP Designer, and Phase 7 MES Core are NOT STARTED.** Do not implement without explicit slice approval. Products integrate via **CIC** only (ADR-043).

Implementation governance for agents lives in `.cursor/rules/` (SoT/phase discipline, architecture invariants, plan-then-approve workflow). Those rules do not replace the SoT.

---

## Current state (summary)

| Item | Status |
| --- | --- |
| Phases 1–5 | **COMPLETE** |
| Phase 6 | **COMPLETE** (6A–6G implemented/tested; **6H SUPPORTED VIA GATEWAY**; ADR-040, ADR-041) |
| Phase 6E REST industrial gateway | **IMPLEMENTED** / **TESTED** (localhost HTTP fixture; not vendor certification) |
| Phase 6F MQTT | **IMPLEMENTED** / **TESTED** (localhost Mosquitto; not vendor certification). Multi-equipment scale **VALIDATED** (10/50/100/200 + 2×50; not production capacity) |
| Phase 6G EtherNet/IP | **IMPLEMENTED** / **TESTED** (libplctag explicit messaging; local `ab_server`; not hardware certification). Two-device isolation **VALIDATED** under test conditions |
| Phase 6H PROFINET | **SUPPORTED VIA GATEWAY** (ADR-040); native **IMPLEMENTED TO SOFTWARE BOUNDARY** (isolated Hilscher branch; **HARDWARE VALIDATION PENDING**) |
| Native PROFIBUS | **SUPPORTED VIA GATEWAY**; native **IMPLEMENTED TO SOFTWARE BOUNDARY** (isolated Hilscher branch; **HARDWARE VALIDATION PENDING**) |
| ICP product | **ICP-1A IMPLEMENTED / TESTED**; **ICP-1B IMPLEMENTED / TESTED** (on this isolated branch); **ICP-1C NOT STARTED**; **ICP Designer NOT STARTED** |
| Phase 7 MES Core | **NOT STARTED** (ADR-045; consumes CIC only) |
| SCADA, API, database, auth, Blazor, real PLC | **NOT IMPLEMENTED** |

Equipment is open-ended (`Equipment` / `GenericEquipment`). `Conveyor` is a Gazebo simulation example, not a required machine catalog.

---

## Official phases (SoT only)

1. Factory Foundation  
2. Equipment Plugin Foundation  
3. Conveyor Control  
4. Product Motion  
5. Industrial Equipment Abstraction  
6. Industrial Adapter Layer  
7. MES Core + Resource Management  
8. SCADA / Operational HMI  
9. Security & Authorization  
10. Real Factory Integration  
11. Commercial Hardening & Enterprise Integration  

Do not use Stage 0–25, old Phase 0–10, sensor-first, or gateway-only numbering as the live plan.

---

## Commands

### Equipment and adapter unit tests (no Gazebo)

```bash
cd ~/Documents/virtual-factory
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The OPC UA multi-PLC scalability test is included. It can take several minutes (TIMEOUT 600). To run only the short unit tests:

```bash
ctest --test-dir build --output-on-failure -E opcua_multi_server_scalability_test
```

To run only the scalability validation:

```bash
ctest --test-dir build --output-on-failure -R opcua_multi_server_scalability_test
```

Results and limitations: [`opcua-scalability-test.md`](opcua-scalability-test.md). In-process simulated servers **and** clients; not production PLC hardware proof.

### Conveyor plugin

```bash
cd ~/Documents/virtual-factory/gazebo/plugins/conveyor/build
cmake --build .
```

If the CMake cache is missing:

```bash
cd ~/Documents/virtual-factory/gazebo/plugins/conveyor
cmake -S . -B build
cmake --build build
```

### Headless Gazebo check (Phases 4–5 behaviour)

```bash
export GZ_SIM_RESOURCE_PATH="$HOME/Documents/virtual-factory/gazebo/models"
export GZ_SIM_SYSTEM_PLUGIN_PATH="$HOME/Documents/virtual-factory/gazebo/plugins/conveyor/build"
gz sim -s -r --iterations 6000 -v 3 \
  "$HOME/Documents/virtual-factory/gazebo/worlds/phase1/factory.sdf"
```

Expect CV-001, PRODUCT-001, START ~update 1000, `dt=0.001 s`, product X from about −1.5 m to 0.5 m at 0.5 m/s, STOP ~update 5000.

---

## Environment (last verified 2026-08-25)

Gazebo Sim 8.15.0, gz-sim8, CMake 3.28.3, g++ 13.3.0, C++17, open62541 1.4.0-rc2, libmodbus 3.1.10 (`libmodbus-dev` / `libmodbus5` 3.1.10-1ubuntu1), libcurl 8.5.0 (`libcurl4-openssl-dev`), nlohmann/json 3.11.3 (`nlohmann-json3-dev`), Eclipse Paho MQTT C 1.3.13 (`libpaho-mqtt-dev` / `libpaho-mqtt3as`), Mosquitto 2.0.18 (MQTT test broker only), libplctag 2.7.1 (commit `bdb10aeaf4f374cec7ae4e66887446dedf952dc1`, MPL-2.0; build to `.deps/libplctag` when not system-installed; `ab_server` test fixture only).
