# Implementation Status

> What actually exists in the repository right now.
> Planned architecture lives in the SoT PDF and `architecture.md`.
> Do not mark planned functionality as implemented.

Authoritative architecture: [`MES_SCADA_Virtual_Factory_Source_of_Truth.pdf`](MES_SCADA_Virtual_Factory_Source_of_Truth.pdf).

---

## Audit Date

2026-08-22 (documentation system established; Phase 4 `dt` fix runtime-verified).

---

## Current Phase

**SoT Phase 4 — Product Motion: COMPLETE / RUNTIME VERIFIED.**

Phases 1–3: COMPLETE.

**Next phase:** SoT Phase 5 — Industrial Equipment Abstraction (not started).

Official phase numbers are SoT Phases 1–11. Older “Stage 0–25” and handoff sequences are not used as implementation phase numbers.

---

## Overall Completion

The repository contains a Gazebo Sim 8 virtual plant and one C++ System plugin that starts/stops a conveyor and translates PRODUCT-001 in +X.

Nothing above the plant exists: no equipment contract library, no adapters, no MES, no SCADA, no API, no GUI, no auth, no database, no tests.

Approximate scope: **plant simulation foundation only**.

---

## Environment

| Item | Value | How verified |
| --- | --- | --- |
| OS | Ubuntu 24.04 (Noble packages) | `dpkg` package suffixes `~noble` |
| Gazebo | Gazebo Sim **8.15.0** | `gz sim --version` |
| gz-sim | **gz-sim8** 8.15.0 (`libgz-sim8-dev`) | `find_package(gz-sim8)` / headers `/usr/include/gz/sim8` |
| gz-plugin | gz-plugin2 2.0.4 | `libgz-plugin2-dev` |
| gz-cmake | gz-cmake3 3.6.0 | `libgz-cmake3-dev` |
| CMake | **3.28.3** | `cmake --version` |
| Compiler | g++ **13.3.0** (`/usr/bin/c++`) | `g++ --version` |
| C++ standard | **C++17** | `CXX_STANDARD 17` in `gazebo/plugins/conveyor/CMakeLists.txt` |

`std::chrono::steady_clock::duration` on this platform: period `1/1e9` (nanoseconds). Confirmed with a small C++17 program: `num=1 den=1000000000`.

---

## Repository Structure

```text
virtual-factory/
  README.md
  .gitignore
  docs/
    README.md
    MES_SCADA_Virtual_Factory_Source_of_Truth.pdf
    implementation-status.md
    architecture.md
    decisions.md
    roadmap.md
    CHANGELOG.md
  gazebo/
    worlds/phase1/factory.sdf
    models/conveyor/
    models/product/
    plugins/conveyor/
      CMakeLists.txt
      ConveyorSystem.hh
      ConveyorSystem.cc
      build/                  gitignored build tree
  tests/                      empty
```

There is no `src/` MES/SCADA tree, no .NET solution, and no adapter packages.

---

## Implemented Components

### Factory world

- **Path:** `gazebo/worlds/phase1/factory.sdf`
- **Purpose:** Gazebo world `virtual_factory`; ODE physics `max_step_size` 0.001 s (1000 Hz); floor; includes CV-001 and PRODUCT-001
- **State:** COMPLETE
- **Committed:** yes (`master`, through `dd4e3f2` and later)
- **Tested:** RUNTIME VERIFIED 2026-08-22 via `gz sim -s -r --iterations 6000`

### Conveyor model CV-001

- **Path:** `gazebo/models/conveyor/model.sdf`, `model.config`
- **Purpose:** Static conveyor: frame 4.0 × 1.2 × 1.0 m (center Z=0.5), belt 4.0 × 1.0 × 0.1 m (center Z=1.05, top Z=1.10); plugin `libConveyorSystem.so` / `virtual_factory::ConveyorSystem`
- **State:** COMPLETE (`<static>true</static>` is intentional)
- **Committed:** yes
- **Tested:** loaded in the headless run (belt entity 12)

### Product model PRODUCT-001

- **Path:** `gazebo/models/product/model.sdf`, `model.config`
- **Purpose:** 0.4 × 0.4 × 0.3 m box; internal pose `0 0 0 0 0 0`; world include pose `-1.5 0 1.25 0 0 0`
- **State:** COMPLETE
- **Committed:** yes
- **Tested:** discovered as entity 15; pose translated in Phase 4 run

### ConveyorSystem plugin

- **Path:** `gazebo/plugins/conveyor/ConveyorSystem.hh`, `ConveyorSystem.cc`, `CMakeLists.txt`
- **Purpose:** gz-sim 8 System plugin: `ISystemConfigure`, `ISystemPreUpdate`; Start/Stop/SetSpeed; belt velocity command; PRODUCT-001 ECM discovery; pose X += speed × dt_seconds
- **State:** COMPLETE for SoT Phases 2–4
- **Committed:** Phase 4 motion + dt fix included with this documentation pass
- **Tested:** COMPILES; RUNTIME VERIFIED 2026-08-22 (see Runtime Verification)

Interfaces implemented on the class (not an independent library):

- `Start()` — `running_=true`; default speed 0.5 m/s if speed was ≤ 0
- `Stop()` — `running_=false`; speed 0
- `SetSpeed(double)` — rejects negative speeds; otherwise stores m/s
- `Configure()` — model name, belt link `belt`
- `PreUpdate()` — pause guard, product lookup, development Start/Stop, pose integration, belt `SetLinearVelocity`, heartbeat

**Development-only:** Start at `updateCount_ == 1000`, Stop at `updateCount_ == 5000`. Not the future SCADA/MES command path.

`fault_` is declared and unused.

### Plugin build artifact

- **Path:** `gazebo/plugins/conveyor/build/libConveyorSystem.so` (gitignored)
- **Purpose:** Shared library loaded by Gazebo
- **State:** produced by `cmake --build .`
- **Tested:** loaded in the headless run

---

## Partially Implemented Components

| Item | Reality |
| --- | --- |
| Belt visual/physical motion | `Link::SetLinearVelocity` is called. CV-001 is static, so a moving belt appearance is not expected. Product pose is the motion mechanism. |
| Equipment identity/state | `name_`, `running_`, `speed_`, `fault_` live only inside the plugin. No shared equipment contract (Phase 5). |
| `tests/` | Directory exists; no test sources or CMake test target. |
| Documentation vs SoT PDF | Markdown set now matches the SoT. The PDF itself was not regenerated (architecture unchanged). |

---

## Not Implemented

These do not exist in the tree:

- Industrial equipment abstraction / equipment contract (Phase 5)
- Industrial adapters
- OPC UA / open62541
- Modbus TCP / RTU
- REST fallback adapter
- MQTT, EtherNet/IP, vendor SDKs
- OpenPLC / virtual PLC
- Industrial core
- MES (orders, materials, quality, maintenance, traceability, …)
- SCADA / operational HMI
- Application/API layer
- Database / persistence
- Authentication
- RBAC
- .NET / Blazor GUI
- Automated tests
- Sensor SEN-001 (old roadmap item; not in SoT Phases 1–4)
- Multi-machine factory (robots, inspection, packaging)

---

## Current Git State

Recorded as part of the 2026-08-22 documentation and Phase 4 verification pass.

- **Branch:** `master` (tracks `origin/master`)
- **Remote:** `git@github.com:shahzad510/virtual-factory.git`
- **Prior HEAD:** `dd4e3f2` — `feat: add product to conveyor world`
- **This pass adds:** plugin product motion + dt-seconds fix; documentation hierarchy; SoT PDF

Use `git status` and `git log -1` after pull; do not trust a stale hash in this paragraph if later commits exist.

---

## Build Status

Command (2026-08-22, after dt fix):

```bash
cd ~/Documents/virtual-factory/gazebo/plugins/conveyor/build
cmake --build .
```

Result:

```text
[ 50%] Building CXX object CMakeFiles/ConveyorSystem.dir/ConveyorSystem.cc.o
[100%] Linking CXX shared library libConveyorSystem.so
[100%] Built target ConveyorSystem
```

**COMPILES.**

---

## Runtime Verification

| Claim | Status |
| --- | --- |
| Plugin compiles | COMPILES — verified |
| Headless world loads plugin, CV-001, PRODUCT-001 | RUNTIME VERIFIED — 2026-08-22 |
| `dt` is 0.001 s (not nanosecond tick count) | RUNTIME VERIFIED — heartbeat `dt=0.001 s` |
| Product held at X=-1.5 m while stopped | RUNTIME VERIFIED |
| START at update 1000, speed 0.5 m/s | RUNTIME VERIFIED |
| Product X increases ~0.05 m per 100 running updates | RUNTIME VERIFIED |
| STOP at update 5000; X ≈ +0.5 m (2.0 m travel) | RUNTIME VERIFIED |
| Interactive GUI session | NOT TESTED in this audit (headless server only) |
| Automated unit tests | NOT TESTED (none exist) |

Command:

```bash
export GZ_SIM_RESOURCE_PATH="$HOME/Documents/virtual-factory/gazebo/models"
export GZ_SIM_SYSTEM_PLUGIN_PATH="$HOME/Documents/virtual-factory/gazebo/plugins/conveyor/build"
gz sim -s -r --iterations 6000 -v 3 \
  "$HOME/Documents/virtual-factory/gazebo/worlds/phase1/factory.sdf"
```

Observed (selected):

| Update | running | speed | dt | product_x |
| --- | --- | --- | --- | --- |
| 100 | false | 0 | 0.001 s | -1.5 m |
| 1000 | true | 0.5 m/s | 0.001 s | -1.4995 m |
| 2000 | true | 0.5 m/s | 0.001 s | -1.01201 m |
| 4000 | true | 0.5 m/s | 0.001 s | -0.0120052 m |
| 5000 | false | 0 | 0.001 s | 0.487495 m |
| 5100–6000 | false | 0 | 0.001 s | 0.5 m |

Kinematic expectation: 4000 running steps × 0.5 m/s × 0.001 s = **2.0 m**; start **-1.5 m** → rest **+0.5 m**. Matches.

If `_info.dt.count()` had been used, each step would have applied ~500 km of travel. That did not occur.

---

## Known Problems

1. **Temporary Start/Stop timers** (updates 1000 / 5000) are development-only. Replace in a later control/SCADA phase.
2. **Product motion is a pose integration**, not a physically simulated belt. The product remains a dynamic body; a small physics residual was visible at Stop (0.487 m then settle at 0.5 m). Acceptable for Phase 4.
3. **Static conveyor + `SetLinearVelocity`** does not produce a visually scrolling belt.
4. **`fault_` unused.**
5. **No automated tests.**
6. **Plugin/model paths** must be exported (`GZ_SIM_RESOURCE_PATH`, `GZ_SIM_SYSTEM_PLUGIN_PATH`); not installed system-wide.
7. **SoT PDF is not generated from Markdown.** If architecture changes, update Markdown first, then regenerate the PDF intentionally.
8. **Role catalog** in SoT §14 vs later briefing lists (warehouse, viewer) is not reconciled; irrelevant until Phase 9.

---

## Current Decisions

See [`decisions.md`](decisions.md). In force for current work: ADR-001 (Gazebo is the plant), ADR-008 (phase gates), ADR-011 (GUI not C++ desktop), ADR-012 (adapter boundary), ADR-015 (SoT phase numbers), ADR-016 (static conveyor / kinematic product), ADR-018 (`dt` in seconds).

---

## Next Step

**SoT Phase 5 — Industrial Equipment Abstraction.**

Introduce a Gazebo-independent equipment contract (identity, running/stopped, speed, fault, start/stop/set-speed) that `ConveyorSystem` can implement. Do not add OPC UA, MES, or Blazor in that phase.

Do not start Phase 5 until the project owner instructs it.

---

## Resume Instructions

1. Read `docs/README.md` → SoT PDF → this file → `architecture.md` → `decisions.md` → `roadmap.md` → `CHANGELOG.md`.
2. `git status` and `git log --oneline --decorate -10`.
3. Build:

```bash
cd ~/Documents/virtual-factory/gazebo/plugins/conveyor/build
cmake --build .
```

4. Optional runtime check: the headless command in [Runtime Verification](#runtime-verification).
5. Continue from **Next Step** (Phase 5), not from old Stage numbers and not from SEN-001.
