# Hilscher cifX environment audit — native fieldbus track

**Date:** 2026-08-30  
**Host:** Cloud Agent VM (Ubuntu 24.04, x86_64)  
**Branch:** `cursor/icp-hilscher-native-development-a88d` (isolated)  
**Purpose:** Distinguish gitignored local SDK extracts from plant hardware.

---

## Executive summary

| Item | Result |
| --- | --- |
| Hilscher cifX SDK (`cifXUser.h`, `libcifx`) | **FOUND locally** in gitignored `.deps/libcifx` (nxdrvlinux). **Not in Git.** |
| NXDRV / `uio_netx` loaded | **NOT PRESENT** on this host |
| PROFINET firmware (NXLFW-PNM) | **NOT FOUND** |
| PROFIBUS firmware (NXLFW-DPM / CIFXDPM.NXF) | **NOT FOUND** |
| License files (NXLIC-MASTER) | **NOT FOUND** |
| Hilscher PCI/USB hardware | **NOT FOUND** (`lspci` empty) |
| Native PROFINET plant integration | **HARDWARE VALIDATION PENDING** |
| Native PROFIBUS plant integration | **HARDWARE VALIDATION PENDING** |

Finding libcifx does **not** enable native fieldbus. Flags default OFF. Proprietary firmware was **not** committed.

The earlier Stage A audit looked for `cifXAPI.h` (wrong name). The public header is `cifXUser.h`.

---

## Search performed

### Standard install paths

| Path | Result |
| --- | --- |
| `/opt/cifx` | absent |
| `/usr/local/cifx` | absent |
| `$HOME/cifx` | absent |
| `.deps/hilscher-cifx` (workspace) | absent |

### Headers and libraries

| Artifact | Result |
| --- | --- |
| `cifXAPI.h` | not found under `/usr`, `/opt`, `/home` |
| `libcifx` / `libcifx.so` | not found |
| `ldconfig -p \| grep cifx` | no entries |

### Environment

| Variable | Result |
| --- | --- |
| `HILSCHER_CIFX_ROOT` | unset |
| `CIFX_SDK_ROOT` | unset |

### Hardware

| Check | Result |
| --- | --- |
| `lspci \| grep -iE 'hilscher\|cifx\|netx'` | no devices |
| `lsusb \| grep -i hilscher` | no devices |

---

## CMake feature detection (repository)

`industrial/cmake/FindHilscherCifX.cmake` sets:

| Build flag | Value when SDK absent |
| --- | --- |
| `VF_HILSCHER_CIFX_AVAILABLE` | `0` |
| `VF_ENABLE_HILSCHER_PROFINET` | OFF (default) |
| `VF_ENABLE_HILSCHER_PROFIBUS` | OFF (default) |

When SDK is absent, adapters compile with stub private backends and `connect()` fails with an explicit **BLOCKED BY SDK/HARDWARE** message. No fake TCP/UDP PROFINET or fake serial PROFIBUS is implemented.

---

## Platform matrix (honest labels)

Labels: **SUPPORTED** | **VERIFIED** | **TECHNICALLY POSSIBLE** | **UNKNOWN** | **UNSUPPORTED** | **BLOCKED**

| Platform | Native Hilscher PROFINET | Native Hilscher PROFIBUS | Notes |
| --- | --- | --- | --- |
| Windows 10 | **UNKNOWN** | **UNKNOWN** | Vendor documents NXDRV-WIN; not tested in this environment |
| Windows 11 | **TECHNICALLY POSSIBLE** | **TECHNICALLY POSSIBLE** | Hilscher documents Win11 driver verification; ICP not validated |
| Windows Server | **UNKNOWN** | **UNKNOWN** | Hilscher does not default-test Server; do not mark production-supported |
| Ubuntu 24.04 LTS | **BLOCKED** (this host) | **BLOCKED** (this host) | NXDRV-LINUX exists upstream; not installed here |
| Docker Linux (default) | **UNSUPPORTED** (default) | **UNSUPPORTED** (default) | Requires PCIe/device passthrough; prefer host agent pattern |
| Docker Desktop Windows | **UNSUPPORTED** | **UNSUPPORTED** | No reliable plant fieldbus without nested HW passthrough |

See [`profinet-hilscher-final-gate.md`](profinet-hilscher-final-gate.md) and [`profibus-native-evaluation.md`](profibus-native-evaluation.md) for target SKUs and deployment guidance.

---

## Recommended hardware (procurement — not verified here)

| Protocol | Card | Part | Firmware | License |
| --- | --- | --- | --- | --- |
| PROFINET IO-Controller | CIFX 50E-RE | 1251.100 | NXLFW-PNM 7428.840 | NXLIC-MASTER 8211.000 |
| PROFIBUS DP Master | CIFX 50E-DP | 1251.410 | CIFXDPM.NXF / NXLFW-DPM 7428.410 | NXLIC-MASTER 8211.000 |

Simultaneous PN + PB on one host requires **two cards** (RE + DP), not one RE card for both buses. **Not verified** in this environment.

---

## Next steps when SDK/hardware arrives

1. Install NXDRV + development headers; set `HILSCHER_CIFX_ROOT`.
2. Record exact SDK version, driver version, firmware, and license serial.
3. Build smallest official vendor sample; verify host ↔ card communication.
4. Enable `VF_ENABLE_HILSCHER_PROFINET` / `VF_ENABLE_HILSCHER_PROFIBUS` in CMake.
5. Implement real `profinet_session` / `profibus_session` backends (separate `.cc` files).
6. Run vendor smoke tests per [`native-fieldbus-implementation-status.md`](native-fieldbus-implementation-status.md).
7. Only then mark native connectivity **TESTED** / **VALIDATED** under documented conditions.

---

## Status labels

| Item | Status |
| --- | --- |
| Environment audit | **COMPLETE** |
| SDK present | **NO** |
| Hardware present | **NO** |
| Smoke test | **NOT RUN** |
| Stage A scaffolding | **IMPLEMENTED** / **TESTED** (stub backends) |
| Stage B native PROFINET | **BLOCKED BY SDK/HARDWARE** |
| Stage C native PROFIBUS | **BLOCKED BY SDK/HARDWARE** |
