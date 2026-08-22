# Virtual Factory documentation

MES + SCADA + industrial adapter platform. Gazebo Sim is the virtual plant. The long-term goal is realistic enough that simulated or real equipment can feed the **same** MES/SCADA core.

Gazebo is not the MES. The C++ plugin is not the GUI. Adapters are not the MES.

---

## Documentation hierarchy

| Level | Document | Authoritative for |
| --- | --- | --- |
| 1 | [MES_SCADA_Virtual_Factory_Source_of_Truth.pdf](MES_SCADA_Virtual_Factory_Source_of_Truth.pdf) | Overall architecture, principles, SoT phases, adapter/GUI/RBAC intent |
| 2 | [implementation-status.md](implementation-status.md) | **What exists in the repo right now** (never planned-as-done) |
| 3 | [architecture.md](architecture.md) | How the architecture is implemented: layers, flows, phase gates |
| 4 | [decisions.md](decisions.md) | Why decisions were made (ADRs) |
| 5 | [roadmap.md](roadmap.md) | What comes next; DONE / NEXT / PLANNED / DEFERRED |
| 6 | [CHANGELOG.md](CHANGELOG.md) | What changed |
| 7 | This file | Index, resume path, commands |

When they disagree:

- **Current code** → implementation reality  
- **SoT PDF** → architectural authority  
- **decisions.md** → documented rationale  
- **implementation-status.md** → verified state  

Do not create a second source of truth. If architecture must change, update `decisions.md` and the SoT **intentionally**.

---

## Current phase

**SoT Phase 4 — Product Motion: COMPLETE / RUNTIME VERIFIED (2026-08-22).**

Phases 1–3: COMPLETE.

**Next:** Phase 5 — Industrial Equipment Abstraction (not started; wait for owner instruction).

Details: [implementation-status.md](implementation-status.md), [roadmap.md](roadmap.md).

---

## How to resume development

A new developer or AI agent should:

1. Read the SoT PDF
2. Read `implementation-status.md`
3. Read `architecture.md`
4. Read `decisions.md`
5. Read `roadmap.md`
6. Read `CHANGELOG.md`
7. Run `git status` and `git log --oneline --decorate -10`
8. Verify the build (commands below)
9. Continue from the documented **Next Step** (Phase 5) — not from old Stage numbers, not SEN-001, not MES/GUI

---

## Important commands

### Build the conveyor plugin

```bash
cd ~/Documents/virtual-factory/gazebo/plugins/conveyor/build
cmake --build .
```

If already inside `build/`, do **not** run `cmake --build build`.

If the CMake cache is missing:

```bash
cd ~/Documents/virtual-factory/gazebo/plugins/conveyor
cmake -S . -B build
cmake --build build
```

### Headless runtime check (Phase 4)

```bash
export GZ_SIM_RESOURCE_PATH="$HOME/Documents/virtual-factory/gazebo/models"
export GZ_SIM_SYSTEM_PLUGIN_PATH="$HOME/Documents/virtual-factory/gazebo/plugins/conveyor/build"
gz sim -s -r --iterations 6000 -v 3 \
  "$HOME/Documents/virtual-factory/gazebo/worlds/phase1/factory.sdf"
```

Expect `[VirtualFactory]` logs: product found, START ~update 1000, `dt=0.001 s`, `product_x` increasing at 0.5 m/s, STOP ~update 5000.

### Interactive Gazebo

Same environment variables, then:

```bash
gz sim "$HOME/Documents/virtual-factory/gazebo/worlds/phase1/factory.sdf"
```

Press play. There are **no** automated tests (`tests/` is empty).

---

## Environment (verified 2026-08-22)

Gazebo Sim 8.15.0, gz-sim8, CMake 3.28.3, g++ 13.3.0, C++17.

---

## Technologies

**In use:** Gazebo Sim 8, C++, CMake.

**Planned:** equipment contract; adapters (OPC UA, Modbus, REST fallback); MES; SCADA; .NET/Blazor GUI; RBAC. See SoT and `architecture.md`.
