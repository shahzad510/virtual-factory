# Hilscher platform and Docker readiness

**Branch:** `cursor/icp-hilscher-native-development-a88d`  
**Honesty rule:** do not claim vendor OS support without evidence. Labels below:

| Label | Meaning |
| --- | --- |
| **SUPPORTED** | Product intent + vendor materials align; still needs our hardware validation |
| **TECHNICALLY POSSIBLE** | Architecture fits; vendor/driver path exists |
| **NOT VERIFIED** | Not tested in this project environment |
| **UNSUPPORTED** | Explicitly out of scope or unsuitable |

---

## Desktop / server OS

| Platform | Native PROFINET (Hilscher) | Native PROFIBUS (Hilscher) | Notes |
| --- | --- | --- | --- |
| **Windows 10** | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | NXDRV-WIN is the usual cifX host driver. Confirm exact CIFX 50E-RE/DP + firmware matrix with Hilscher. |
| **Windows 11** | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | Vendor documents Win11 driver verification for cifX families; ICP has not validated. |
| **Windows Server** | **NOT VERIFIED** | **NOT VERIFIED** | **Requires vendor verification** — Hilscher does not treat Server as a default desktop test matrix. Do not mark production-supported until Hilscher confirms the SKU + driver + firmware combination. |
| **Ubuntu 24.04 LTS** | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | **TECHNICALLY POSSIBLE** / **NOT VERIFIED** | NXDRV-LINUX / `uio_netx` exist upstream. **Requires vendor verification** for 24.04 kernel series, Secure Boot, and the exact CIFX SKU. This cloud agent host has **no** card/`uio_netx`. |

Gateway PROFINET/PROFIBUS (OPC UA/Modbus/REST/MQTT) remains available on these OS targets independent of Hilscher.

---

## Docker

### Not claimed

| Environment | Native fieldbus |
| --- | --- |
| **Docker Desktop Windows** | **UNSUPPORTED** for plant-grade native PN/PB |
| Default Docker Linux without device access | **UNSUPPORTED** |

### Intended architecture (not validated)

```text
┌─────────────────────────────┐
│ Dockerized ICP Core         │  adapters for OPC UA / Modbus / MQTT / REST / EIP
│ (optional northbound later) │  protocol-neutral GenericEquipment / LiveStateCache
└──────────────┬──────────────┘
               │ IPC / CIC / local API (future) — NOT STARTED for CIC
               ▼
┌─────────────────────────────┐
│ Host-level Hilscher agent   │  VF_ENABLE_HILSCHER_* build, NXDRV, libcifx
│ Profinet/Profibus adapters  │
└──────────────┬──────────────┘
               ▼
         CIFX PCIe hardware
```

**Current code support:** adapters and cifX runtime are ordinary host libraries. Nothing requires running cifX **inside** a container. Host-side ICP with Hilscher flags ON is the intended first validation path.

**Passthrough containers** (map `/dev` + load `uio_netx` on Linux) are **TECHNICALLY POSSIBLE** / **NOT VERIFIED**. Prefer host agent until proven.

---

## CMake reminder

```bash
-DVF_ENABLE_HILSCHER_PROFINET=ON
-DVF_ENABLE_HILSCHER_PROFIBUS=ON
-DHILSCHER_CIFX_ROOT=/path/to/libcifx
```

Flags default **OFF**. Finding libcifx must not auto-enable the backend.
