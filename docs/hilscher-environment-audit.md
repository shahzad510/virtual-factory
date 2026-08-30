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

Labels: **SUPPORTED** | **TECHNICALLY POSSIBLE** | **NOT VERIFIED** | **UNSUPPORTED**

See [`hilscher-platform-and-docker.md`](hilscher-platform-and-docker.md) for the authoritative matrix.

| Platform | Native Hilscher PROFINET | Native Hilscher PROFIBUS | Notes |
| --- | --- | --- | --- |
| Windows 10 | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | NXDRV-WIN |
| Windows 11 | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | Vendor Win11 driver path exists; ICP not validated |
| Windows Server | **NOT VERIFIED** | **NOT VERIFIED** | **Requires vendor verification** |
| Ubuntu 24.04 LTS | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | NXDRV-LINUX exists; **requires vendor verification** for 24.04 |
| Docker Linux (default) | **UNSUPPORTED** (default) | **UNSUPPORTED** (default) | Prefer host-level Hilscher agent |
| Docker Desktop Windows | **UNSUPPORTED** | **UNSUPPORTED** | No plant fieldbus claim |

See [`profinet-hilscher-final-gate.md`](profinet-hilscher-final-gate.md) and [`profibus-native-evaluation.md`](profibus-native-evaluation.md) for target SKUs.

---

## Recommended hardware (procurement — not verified here)

| Protocol | Card | Part | Firmware | License |
| --- | --- | --- | --- | --- |
| PROFINET IO-Controller | CIFX 50E-RE | 1251.100 | NXLFW-PNM 7428.840 | NXLIC-MASTER 8211.000 |
| PROFIBUS DP Master | CIFX 50E-DP | 1251.410 | CIFXDPM.NXF / NXLFW-DPM 7428.410 | NXLIC-MASTER 8211.000 |

Simultaneous PN + PB on one host requires **two cards** (RE + DP), not one RE card for both buses. **Not verified** in this environment.

---

## Next steps when hardware arrives

1. Follow [`hilscher-hardware-validation-procedure.md`](hilscher-hardware-validation-procedure.md).
2. Install NXDRV + firmware + license; set `HILSCHER_CIFX_ROOT`.
3. Enable `VF_ENABLE_HILSCHER_PROFINET` / `VF_ENABLE_HILSCHER_PROFIBUS`.
4. Run `hilscher_hardware_readiness_test` — expect progress toward `READY_FOR_TEST` (host preflight only).
5. Engineer SYCON artifacts; connect real IO-Device / DP slave (see [`hilscher-test-peer-options.md`](hilscher-test-peer-options.md)).
6. Fill [`templates/hilscher-hardware-test-report.md`](templates/hilscher-hardware-test-report.md).
7. Only then mark native connectivity **VALIDATED** under documented conditions.

---

## Status labels

| Item | Status |
| --- | --- |
| Environment audit | **COMPLETE** (updated 2026-08-30) |
| libcifx extract on this agent | **FOUND** (gitignored `.deps`) |
| NXDRV / card on this agent | **NO** |
| Firmware / license in Git | **NO** (correct) |
| Smoke test on hardware | **NOT RUN** |
| Stage A scaffolding on master | **IMPLEMENTED** / **TESTED** (stubs) |
| Software boundary on this branch | **IMPLEMENTED TO SOFTWARE BOUNDARY** |
| Hardware readiness tooling | **IMPLEMENTED** (preflight; not plant proof) |
| Native plant IO | **HARDWARE VALIDATION PENDING** |
