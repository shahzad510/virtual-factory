# Native fieldbus implementation status — Hilscher track (ICP)

**Date:** 2026-08-30  
**Branch:** `cursor/icp-native-fieldbus-hilscher-a88d`  
**Base:** `master` @ `0dffad9`

This document tracks **what is actually in the repository** for native PROFINET and PROFIBUS via Hilscher cifX. Gateway paths remain supported and unchanged.

---

## Product model (unchanged)

| SKU | Connectivity |
| --- | --- |
| **ICP Standard** | Gateway PROFINET/PROFIBUS + software protocols (OPC UA, Modbus, REST, MQTT, EtherNet/IP) — **no Hilscher dependency** |
| **ICP Industrial** | ICP Standard + optional native PROFINET + optional native PROFIBUS (Hilscher backend when SDK/hardware present) |

---

## Staged implementation

| Stage | Scope | Status |
| --- | --- | --- |
| **A — Vendor foundation** | Private sessions, CMake flags, adapter scaffolding, ICP factory hooks, tests | **IMPLEMENTED** / **TESTED** (SDK-absent stubs) |
| **B — Native PROFINET** | Real cifX PN IO-Controller, cyclic RT1, DCP, AR, diagnostics | **BLOCKED BY SDK/HARDWARE** |
| **C — Native PROFIBUS** | Real cifX DP Master, cyclic IO, slave timeout/recovery | **BLOCKED BY SDK/HARDWARE** |
| **Combined PN+PB** | Simultaneous RE + DP cards, failure isolation | **NOT IMPLEMENTED** — requires hardware |

---

## What exists in code (Stage A)

### CMake

- `industrial/cmake/FindHilscherCifX.cmake`
- Options: `VF_ENABLE_HILSCHER_PROFINET`, `VF_ENABLE_HILSCHER_PROFIBUS` (default **OFF**)
- Compile define: `VF_HILSCHER_CIFX_AVAILABLE=0` when SDK not found

### Private Hilscher layer (`industrial/src/hilscher/`)

| File | Role |
| --- | --- |
| `hilscher_availability.hh/.cc` | Runtime SDK availability check |
| `profinet_session.hh/.cc` | Private PROFINET session — stub; `open()` fails without SDK |
| `profibus_session.hh/.cc` | Private PROFIBUS session — stub; `open()` fails without SDK |

No Hilscher types in public headers.

### Public adapters

| Adapter | Protocol ID | Connect without SDK | Cyclic IO |
| --- | --- | --- | --- |
| `ProfinetIndustrialAdapter` | `"profinet"` | **Faulted** — BLOCKED message | **NOT IMPLEMENTED** |
| `ProfibusIndustrialAdapter` | `"profibus"` | **Faulted** — BLOCKED message | **NOT IMPLEMENTED** |

Configuration structs support future ICP Designer fields (board ID, channel, config artifact path, equipment mappings, process offsets). No GUI in this slice.

### ICP integration

- `AdapterFactory::createProfinet()` / `createProfibus()` added
- `AdapterManager` accepts both adapter types (isolation at manager level unchanged)

### Protected interfaces

**Not modified:** `Equipment.hh`, `IndustrialAdapter.hh`, existing OPC UA/Modbus/REST/MQTT/EIP adapters, Gazebo production code.

---

## Tests

| Test | Status | Notes |
| --- | --- | --- |
| `native_fieldbus_scaffolding_test` | **PASSED** | Construction, blocked connect, equipment exposure, dual adapters in manager |
| Full `ctest` suite (11 tests) | **PASSED** | Includes existing adapter + ICP regression |

Tests explicitly assert `hilscherSdkPresent() == false` in CI. No fake protocol servers.

---

## What is explicitly NOT implemented

- Real Hilscher cifX API calls
- DCP, AR establishment, RT Class 1 cyclic IO (PROFINET)
- DP Master cyclic IO, GSD loading, slave recovery (PROFIBUS)
- Custom GSDML/GSD parsers
- IRT, MRP, PROFIsafe, certification
- Simultaneous PN+PB hardware validation
- ICP Designer GUI
- Docker-native fieldbus passthrough product profile

---

## Smoke-test checklist (when SDK/hardware available)

### PROFINET

1. Initialize controller / load NXLFW-PNM firmware  
2. Configure interface (SYCON.net artifact)  
3. DCP / device identification  
4. AR establishment  
5. RT Class 1 cyclic input/output  
6. Process image read/write  
7. Basic diagnostics  
8. Shutdown / restart behavior  

### PROFIBUS

1. Initialize DP Master / load DPM firmware  
2. Configure bus (baud, master address)  
3. Load GSD-derived slave configuration  
4. Cyclic input/output  
5. Diagnostics  
6. Slave timeout and recovery  

### Combined

1. CIFX 50E-RE + CIFX 50E-DP on same host  
2. Independent adapter instances  
3. PN failure does not fault PB equipment (and vice versa)  

---

## Documentation cross-reference

| Document | Role |
| --- | --- |
| [`hilscher-environment-audit.md`](hilscher-environment-audit.md) | SDK/hardware audit on this host |
| [`profinet-native-evaluation.md`](profinet-native-evaluation.md) | Native PN evaluation; Hilscher alternate |
| [`profinet-hilscher-final-gate.md`](profinet-hilscher-final-gate.md) | Hilscher SKU / platform gate |
| [`profibus-native-evaluation.md`](profibus-native-evaluation.md) | Native PB evaluation |
| [`profinet-native-implementation-plan.md`](profinet-native-implementation-plan.md) | PN implementation plan (pre-code) |
| [`profinet-gateway-integration.md`](profinet-gateway-integration.md) | Gateway path (supported) |
| ADR-040, ADR-046 in [`decisions.md`](decisions.md) | Architectural decisions |

---

## Honest status summary

| Capability | Status |
| --- | --- |
| Gateway PROFINET | **SUPPORTED VIA GATEWAY** |
| Gateway PROFIBUS | **SUPPORTED** (same gateway pattern) |
| Native PROFINET (Hilscher) | **PARTIALLY IMPLEMENTED** — scaffolding only; **BLOCKED BY SDK/HARDWARE** |
| Native PROFIBUS (Hilscher) | **PARTIALLY IMPLEMENTED** — scaffolding only; **BLOCKED BY SDK/HARDWARE** |
| Production native fieldbus claim | **NOT VALID** until smoke tests on real SDK + hardware |
