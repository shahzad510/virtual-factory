# Hilscher hardware validation procedure

**Branch:** `cursor/icp-hilscher-native-development-a88d` (isolated; **do not merge to master**)  
**Status:** PROCEDURE ONLY until physical CIFX hardware is connected.  
**Labels:** steps below are **HARDWARE VALIDATION** when executed on real cards. Repository `ctest` remains **SOFTWARE-INTEGRATION**.

Do **not** claim native PROFINET or PROFIBUS support from software tests alone.

Related:

- [`hilscher-hardware-smoke-test.md`](hilscher-hardware-smoke-test.md)
- [`hilscher-sdk-license.md`](hilscher-sdk-license.md)
- [`hilscher-platform-and-docker.md`](hilscher-platform-and-docker.md)
- [`hilscher-test-peer-options.md`](hilscher-test-peer-options.md)
- [`templates/hilscher-hardware-test-report.md`](templates/hilscher-hardware-test-report.md)
- Example ICP-1B JSON: `icp/examples/native-fieldbus/`

---

## PREPARATION

1. **Install driver**
   - Windows: NXDRV-WIN (vendor package for the CIFX family).
   - Linux: NXDRV-LINUX / `uio_netx` per Hilscher docs for the target kernel.
2. **Install SDK/runtime**
   - Headers: `cifXUser.h` (+ `cifxlinux.h` on Linux).
   - Library: `libcifx` / Windows import library.
   - Set `HILSCHER_CIFX_ROOT` for CMake.
3. **Install firmware**
   - PROFINET: NXLFW-PNM on CIFX 50E-RE.
   - PROFIBUS: CIFXDPM / NXLFW-DPM on CIFX 50E-DP.
4. **Apply license**
   - NXLIC-MASTER (or equivalent). Confirm in SYCON.net / Communication Studio.
   - **Do not commit license keys.**
5. **Connect CIFX**
   - Insert CIFX 50E-RE and/or CIFX 50E-DP. Power / PCIe as required.
6. **Verify board (ICP preflight)**
   ```bash
   cmake -S . -B build-cifx \
     -DVF_ENABLE_HILSCHER_PROFINET=ON \
     -DVF_ENABLE_HILSCHER_PROFIBUS=ON \
     -DHILSCHER_CIFX_ROOT=/path/to/libcifx
   cmake --build build-cifx -j
   ctest --test-dir build-cifx -R hilscher_hardware_readiness_test --output-on-failure
   ```
   Expect readiness state progressing toward `READY_FOR_TEST` (host cifX preflight only).  
   `NO_BOARD` / `DRIVER_MISSING` / `WRONG_FIRMWARE` means stop and fix the host stack first.
7. **Connect industrial switch / RS-485**
   - PROFINET: managed industrial Ethernet as engineered.
   - PROFIBUS: correct RS-485 cabling, termination, baud.
8. **Connect IO-Device / DP slave**
   - Real device or legitimate vendor/software peer (see test-peer doc). Not a homemade fake stack.

---

## PROFINET TEST

Hardware: CIFX 50E-RE + NXLFW-PNM + NXLIC-MASTER + GSDML + IO-Device.

| Step | Action | Expected | Failure interpretation |
| --- | --- | --- | --- |
| 1 | Configure GSDML in SYCON | Device modules/slots accepted | Wrong GSDML / device identity |
| 2 | Configure device + export `config.nxd` | Artifact path set in ICP-1B | Path missing → adapter Faulted |
| 3 | Load example / plant ICP-1B JSON | Catalog validates | Validation paths in `ConfigurationValidator` |
| 4 | Start controller (`connect()`) | `ConnectionState::Connected` | Driver/board/firmware/config download errors in `lastError()` |
| 5 | Verify DCP | Station discoverable | **Blocked until Protocol API**; do not invent packets |
| 6 | Establish AR | IO-Device in data exchange | Firmware/Protocol API + correct SYCON config |
| 7 | Verify cyclic data | Inputs change with plant; outputs affect device | Mapping offsets wrong vs process image |
| 8 | Verify GenericEquipment / LiveStateCache | Telemetry/state update; stale=false when Connected | Mapper/offset bugs |
| 9 | Disconnect device | Adapter `Faulted`; **machineFault unchanged** unless mapped bit says so | Comm fault leaked into equipment fault |
| 10 | Reconnect (explicit `connect()`) | Recovers without auto-reconnect magic | Auto-reconnect would violate ICP-1A |
| 11 | Multiple IO-Devices | One adapter, many equipment IDs | Collisions rejected by AdapterManager |

Fill [`templates/hilscher-hardware-test-report.md`](templates/hilscher-hardware-test-report.md) for each run.

---

## PROFIBUS TEST

Hardware: CIFX 50E-DP + DPM firmware + master license + GSD + DP slave.

| Step | Action | Expected | Failure interpretation |
| --- | --- | --- | --- |
| 1 | Configure GSD in SYCON | Slave modules accepted | Wrong GSD / address |
| 2 | Export master `config.nxd` | Path in ICP-1B | Missing artifact |
| 3 | Start master (`connect()`) | Connected | Driver/channel/firmware |
| 4 | Verify slave | Slave in cyclic exchange | Cabling, baud, address, termination |
| 5 | Verify cyclic data + mapping | Cache updates | Offset / image size mismatch |
| 6 | Disconnect slave | Faulted communication; no invented machineFault | Same invariant as PN |
| 7 | Reconnect explicitly | Recovery | |
| 8 | Multiple slaves | One master adapter, many equipment | |

---

## Combined PN + PB

Requires **two cards** (50E-RE + 50E-DP). Independent adapters; PN fault must not remove PB equipment.

---

## What SOFTWARE-INTEGRATION already proved

- Catalog load without hardware
- Mapper → AdapterFactory
- PollScheduler disconnected/faulted behavior
- Process-image encode/decode + GenericEquipment mapping
- Readiness classification without inventing boards

## What remains HARDWARE VALIDATION

- DCP, AR, RT Class 1 on the wire
- Live DP cyclic IO and slave diagnostics IDs
- License/runtime proof under load
- Recovery with a real peer
