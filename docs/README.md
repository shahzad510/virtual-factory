# Virtual Factory documentation

This is the first document a new developer or AI agent should read.

MES + SCADA + industrial adapters, with Gazebo Sim as a **simulation plant**. Gazebo is not the MES. The C++ plugin is not the GUI. Adapters are not the MES.

**Do not trust archived documents for current architecture.**

**Do not infer implementation from the roadmap alone.**

---

## Active documentation hierarchy

| Order | Document | Role |
| --- | --- | --- |
| 1 | [MES_SCADA_Virtual_Factory_Source_of_Truth.pdf](MES_SCADA_Virtual_Factory_Source_of_Truth.pdf) | **Architectural authority.** Only active Source of Truth. |
| 2 | [implementation-status.md](implementation-status.md) | **What is actually implemented** in the repository. |
| 3 | [architecture.md](architecture.md) | Current technical architecture (layers, paths, phase gates). |
| 4 | [decisions.md](decisions.md) | Why decisions were made (ADRs). |
| 5 | [roadmap.md](roadmap.md) | Future phases. Planned ≠ implemented. |
| 6 | [CHANGELOG.md](CHANGELOG.md) | What changed over time. |
| 7 | This file | Resume procedure, commands, index. |
| — | [archive/](archive/) | Historical/legacy documents only. Not authoritative. |
| — | Git history | Historical implementation record. |

When they disagree:

- **Current source code** → implementation reality  
- **SoT PDF** → architectural authority  
- **implementation-status.md** → verified implemented state  
- **decisions.md** → documented rationale  

Do not create a second source of truth. If architecture must change, update `decisions.md` and the SoT PDF **intentionally**.

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

Continue from the **Next Step** in `implementation-status.md`. Do **not** start Phase 7 (MES) unless that work is explicitly instructed.

---

## Current state (summary)

| Item | Status |
| --- | --- |
| Phases 1–5 | **COMPLETE** |
| Phase 6 | **IN PROGRESS** (architecture + mock + OPC UA **COMPLETE**, including multi-server as multiple adapter instances; Modbus/REST **NOT IMPLEMENTED**) |
| Phase 6 production Modbus / REST | **NOT IMPLEMENTED** |
| Phase 7 MES Core | **NOT STARTED** / **NOT IMPLEMENTED** (Resource Management is **PLANNED** only; ADR-024) |
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
7. MES Core  
8. SCADA / Operational HMI  
9. Security & Authorization  
10. Real Factory Integration  
11. Commercial Hardening  

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

## Environment (last verified 2026-08-22 / 2026-08-23)

Gazebo Sim 8.15.0, gz-sim8, CMake 3.28.3, g++ 13.3.0, C++17, open62541 1.4.0-rc2.
