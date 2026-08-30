# Native fieldbus implementation status — Hilscher track (ICP)

**Date:** 2026-08-30  
**Branch:** `cursor/icp-hilscher-native-development-a88d` (isolated from `master`)  
**Master baseline:** `d3e557b` — Stage A scaffolding only. **Do not merge this branch until explicitly approved.**

This document tracks **what is actually in the repository** for native PROFINET and PROFIBUS via Hilscher cifX. Gateway paths remain supported and unchanged.

**Tests in this document are SOFTWARE-INTEGRATION TESTS.** They are **not** a REAL PROFINET TEST, REAL PROFIBUS TEST, or HARDWARE VALIDATION.

---

## Product model (unchanged)

| SKU | Connectivity |
| --- | --- |
| **ICP Standard** | Gateway PROFINET/PROFIBUS + software protocols (OPC UA, Modbus, REST, MQTT, EtherNet/IP) — **no Hilscher dependency** |
| **ICP Industrial** | ICP Standard + optional native PROFINET + optional native PROFIBUS (Hilscher backend when SDK/hardware present) |

ICP and MES remain independently deployable. Native fieldbus is **ICP-only**. Hilscher types do not appear in `Equipment.hh`, `IndustrialAdapter.hh`, GenericEquipment, CIC, or MES.

---

## A vs B — what was already on master vs this branch

### A. Already on `master` (Stage A, commit `48b9328` / merge `d3e557b`)

- `ProfinetIndustrialAdapter` / `ProfibusIndustrialAdapter` public scaffolding
- Private `industrial/src/hilscher/` **stubs** (`open()` fails; no cifX calls)
- `FindHilscherCifX.cmake` looking for `cifXAPI.h` (incorrect header name)
- Flags `VF_ENABLE_HILSCHER_PROFINET` / `VF_ENABLE_HILSCHER_PROFIBUS` default **OFF**
- `AdapterFactory::createProfinet` / `createProfibus`
- `tests/native_fieldbus_scaffolding_test.cc`
- Docs: environment audit, native status, PN/PB eval, ADR-040/046

### B. Added on this branch (native software integration)

- Private `cifx_runtime` wrapping official libcifx APIs
- Real session lifecycle (driver init, enumeration, channel, host/bus state, config download, IO, watchdog)
- Process-image codec + GenericEquipment mapping
- Extended in-memory ICP-1B-compatible AdapterConfig (slots/subslots, modules, mappings)
- SOFTWARE-INTEGRATION tests (`process_image_codec_test`, `native_fieldbus_software_integration_test`)
- CMake: find `cifXUser.h`; SDK found does **not** enable the backend unless a flag is ON

---

## Staged implementation

| Stage | Scope | Status |
| --- | --- | --- |
| **A — Vendor foundation** | Private sessions, CMake flags, adapter scaffolding, ICP factory hooks | **IMPLEMENTED** / **TESTED** on master (stubs) |
| **B — Native PROFINET software boundary** | Real cifX host API: init, discovery, lifecycle, process image mapping | **IMPLEMENTED TO SOFTWARE BOUNDARY**. **HARDWARE VALIDATION PENDING** |
| **C — Native PROFIBUS software boundary** | Same host API for DP Master | **IMPLEMENTED TO SOFTWARE BOUNDARY**. **HARDWARE VALIDATION PENDING** |
| **Combined PN+PB hardware** | Simultaneous RE + DP cards | **NOT IMPLEMENTED** — requires hardware |

---

## Honest capability labels

| Capability | Status |
| --- | --- |
| Gateway PROFINET | **SUPPORTED** |
| Gateway PROFIBUS | **SUPPORTED** |
| Native PROFINET | **IMPLEMENTED TO SOFTWARE BOUNDARY** / **HARDWARE VALIDATION PENDING** |
| Native PROFIBUS | **IMPLEMENTED TO SOFTWARE BOUNDARY** / **HARDWARE VALIDATION PENDING** |
| Production native fieldbus claim | **NOT VALID** until hardware smoke tests |

---

## Exact cifX APIs used (from `cifXUser.h` / `cifxlinux.h`)

| API | Use |
| --- | --- |
| `cifXDriverInit` / `cifXDriverDeinit` | Linux driver lifetime (`CIFX_DRIVER_INIT_AUTOSCAN`) |
| `xDriverOpen` / `xDriverClose` | Driver handle |
| `xDriverGetInformation` | Driver version + board count |
| `xDriverGetErrorDescription` | Error text |
| `xDriverEnumBoards` / `xDriverEnumChannels` | Discovery |
| `xChannelOpen` / `xChannelClose` | Channel lifetime |
| `xChannelInfo` | Firmware / mailbox / IO area counts |
| `xChannelIOInfo` | Separate `CIFX_IO_INPUT_AREA` / `CIFX_IO_OUTPUT_AREA` sizes |
| `xChannelHostState` | Host ready / not ready |
| `xChannelBusState` | Bus on / off / get state |
| `xChannelDownload` (`DOWNLOAD_MODE_CONFIG`) | Load SYCON `config.nxd` |
| `xChannelWatchdog` | Start / stop / trigger |
| `xChannelCommonStatusBlock` (`CIFX_CMD_READ_DATA`) | DPM common status bytes (not protocol packets) |
| `xChannelIORead` / `xChannelIOWrite` | Cyclic process image (area 0) |

**Not invented / not called:** homemade DCP, AR, RT Class 1 packets, DP-V1 mailbox IDs, GSD/GSDML parsers, `xChannelPutPacket` with fabricated protocol commands.

When those are required:

> Requires Hilscher Protocol API / firmware / hardware.

---

## Functionality blocked by hardware

- CIFX 50E-RE / CIFX 50E-DP presence
- `uio_netx` / NXDRV kernel driver attaching to a card
- Non-empty `xDriverEnumBoards` result
- `xChannelOpen` against a real channel
- Cyclic IO that actually moves on the wire
- Device/slave loss and recovery on a real bus

## Functionality blocked by unavailable protocol API / firmware

- NXLFW-PNM (PROFINET IO-Controller) / NXLIC-MASTER
- CIFXDPM / NXLFW-DPM (PROFIBUS DP Master)
- DCP Identify/Set, station naming as a host-issued mailbox sequence
- AR establishment as an explicit host command
- Slot/subslot engineering beyond offsets stored in ICP config + SYCON artifact
- DP slave GSD interpretation
- Extended / protocol-specific diagnostics IDs

Controller/master **station name**, baud rate, and slave lists in `AdapterConfig` are **ICP mapping metadata**. The live bus uses the SYCON artifact downloaded via `xChannelDownload`.

---

## ICP integration

- `AdapterFactory::createProfinet` / `createProfibus`
- `AdapterManager` owns instances; PN fault does not remove PB (separate adapters)
- `poll()` maps input image → `GenericEquipment` telemetry/state/fault
- `execute()` maps commands → output image
- `LiveStateCache` consumes normalized Equipment (software-integration covered)
- Explicit `connect()` after Faulted (no auto-reconnect), consistent with ICP-1A

### ICP-1B configuration

This branch **reuses in-memory `AdapterConfig` structs** (the ICP-1A/Stage A configuration objects). It does **not** create a second persistence system.

JSON catalog persistence (`schema=virtual-factory.icp.config`) lives on the **unmerged** branch `cursor/icp-1b-persistent-config-a88d` and is **not** on `master`. Native AdapterConfig fields (controller/interface, station name, device identity, slots/subslots, modules, process/telemetry/command/state/fault mapping) are loadable **without Hilscher hardware**. Wiring those structs from the JSON catalog is deferred until ICP-1B is merged by explicit approval.

---

## CMake / feature flags

| Option | Default |
| --- | --- |
| `VF_ENABLE_HILSCHER_PROFINET` | **OFF** |
| `VF_ENABLE_HILSCHER_PROFIBUS` | **OFF** |

`VF_HILSCHER_CIFX_AVAILABLE=1` only when a flag is **ON** **and** `cifXUser.h` + `libcifx` are found. Finding SDK with flags OFF keeps the stub backend so the rest of ICP builds and tests without Hilscher.

```bash
cmake -S . -B build \
  -DVF_ENABLE_HILSCHER_PROFINET=ON \
  -DVF_ENABLE_HILSCHER_PROFIBUS=ON \
  -DHILSCHER_CIFX_ROOT=/path/to/libcifx
```

---

## Tests (SOFTWARE-INTEGRATION)

| Test | What it proves |
| --- | --- |
| `native_fieldbus_scaffolding_test` | Construction, blocked connect, equipment, AdapterManager |
| `process_image_codec_test` | Encode/decode + GenericEquipment mapping (no hardware) |
| `native_fieldbus_software_integration_test` | Config (slots/modules), factory, manager, cache, missing artifact, missing hardware, explicit reconnect, enumerate |

**Not claimed:** REAL PROFINET, REAL PROFIBUS, HARDWARE VALIDATION.

---

## Protected interfaces

**Not modified:** `Equipment.hh`, `IndustrialAdapter.hh`, existing OPC UA/Modbus/REST/MQTT/EIP adapters, Gazebo, MES.

---

## Rollback

Abandon this branch and continue from `master` @ Stage A. Native Hilscher is optional and not a mandatory ICP dependency. Do not merge until explicit approval.
