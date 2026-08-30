# Native fieldbus implementation status — Hilscher cifX (ICP)

**Date:** 2026-08-30  
**Branch (this increment):** `cursor/icp-hilscher-sdk-integration-a88d`

Gateway paths remain **SUPPORTED VIA GATEWAY** and are unchanged.

---

## Status summary

| Capability | Status |
| --- | --- |
| Gateway PROFINET | **SUPPORTED VIA GATEWAY** |
| Gateway PROFIBUS | **SUPPORTED VIA GATEWAY** |
| libcifx / cifX API (Linux NXDRV-LINUX) | **SOFTWARE-INTEGRATION TESTED** (no card) |
| Native PROFINET cyclic plant IO | **NOT IMPLEMENTED** as production — **HARDWARE VALIDATION PENDING** |
| Native PROFIBUS DP Master plant IO | **NOT IMPLEMENTED** as production — **HARDWARE VALIDATION PENDING** |
| DCP / AR / DP-V1 protocol mailbox | **NOT IMPLEMENTED** — official PNM/DPM Protocol API headers not in NXDRV-LINUX |
| Simultaneous RE+DP | **UNVERIFIED** (architecture ready; no two-card bench) |
| Production “native supported” claim | **Not valid** |

Default CMake: `VF_ENABLE_HILSCHER_PROFINET=OFF`, `VF_ENABLE_HILSCHER_PROFIBUS=OFF` (stub backend).

With flags ON + `HILSCHER_CIFX_ROOT`: real `cifXDriverInit` / `xChannelOpen` / IO / bus state / config download.

---

## What this increment adds

- Private `cifx_runtime` calling **documented** cifX functions only
- `ProfinetSession` / `ProfibusSession` lifecycle through that runtime
- Process-image mapping into `GenericEquipment` (ready when IO succeeds)
- Designer-oriented config fields (slots/submodules/modules, firmware hint)
- License inventory: [`hilscher-sdk-license.md`](hilscher-sdk-license.md)
- Hardware checklist: [`hilscher-hardware-smoke-test.md`](hilscher-hardware-smoke-test.md)

**TESTED** in CI means: construction, mapping codec, AdapterManager isolation, honest failure without hardware. It does **not** mean cyclic PROFINET or DP Master traffic.

---

## Hardware-dependent remainder

1. CIFX 50E-RE / 50E-DP present  
2. NXLFW-PNM / CIFXDPM loaded + NXLIC-MASTER  
3. SYCON config artifact that matches the plant  
4. Real IO-Device / DP slave  
5. Optional: PNM/DPM protocol headers for DCP/AR/slave diagnostics packets  
6. Windows NXDRV-WIN validation  
7. Dual-card simultaneous traffic  

---

## Protected interfaces

`Equipment.hh`, `IndustrialAdapter.hh`, OPC UA/Modbus/REST/MQTT/EIP, Gazebo: **unchanged**.
