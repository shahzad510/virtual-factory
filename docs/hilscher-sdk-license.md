# Hilscher SDK / license inventory (ICP native fieldbus)

**Date:** 2026-08-30  
**Source of driver tree:** official `https://github.com/HilscherAutomation/nxdrvlinux` (NXDRV-LINUX)  
**Install location used in this environment:** gitignored `.deps/nxdrvlinux` (clone) and `.deps/libcifx` (local prefix). **Not committed to Git.**

---

## What was obtained

| Package | Version (this host) | Source | Location |
| --- | --- | --- | --- |
| NXDRV-LINUX / libcifx | git `3.1.0` (`cifxdrv_version.txt`); toolkit notes 2.8.7.0 in CMake | GitHub `HilscherAutomation/nxdrvlinux` | `.deps/nxdrvlinux`, installed to `.deps/libcifx` |
| Headers | `cifXUser.h`, `cifxlinux.h`, `cifXErrors.h` | same | `.deps/libcifx/include/cifx/` |
| Library | `libcifx.so.3.1.0` | built locally | `.deps/libcifx/lib/` |
| Example | `examples/api/cifxlinuxsample.c` | same repo (MIT) | `.deps/nxdrvlinux/examples/api/` |
| NXDRV-WIN | — | **not downloaded** (Windows installer; not this Linux VM) | — |
| NXLFW-PNM / CIFXDPM firmware | — | **not downloaded** (proprietary firmware) | — |
| NXLIC-MASTER | — | **not obtained** | requires hardware serial |
| SYCON.net / GSDML / GSD | — | **not obtained** | engineering tools |

---

## License table

| Component | Version | License | Development use | Runtime use | Redistribution in Git | Hardware dependency | Master/runtime license |
| --- | --- | --- | --- | --- | --- | --- | --- |
| libcifx (user-space driver) | 3.1.0 (this build) | **MIT** (`libcifx/LICENSE.md`) | **Yes** | **Yes** (link `libcifx`) | Source may be used; **do not vendor a copy unless policy allows** — keep external | Card needed for plant IO | No (driver ≠ protocol master) |
| cifX Toolkit (inside libcifx) | 2.8.7.0 (CMake SBOM) | **HSLA** (`Toolkit/LICENSE.md`) — use/copy/modify/distribute granted; military/safety exclusions | **Yes** (as part of libcifx) | Via libcifx | **Do not commit Toolkit sources** unless legal review agrees | Card | No |
| uio_netx kernel module | in nxdrvlinux | **GPL-2.0-only** | Host driver install | Host kernel | GPL obligations if distributed | PCIe cifX | No |
| cifX examples | — | **MIT** | Reference only | No | Not copied into ICP | Demo needs hardware | No |
| NXLFW-PNM firmware | 7428.840 (target SKU) | Hilscher firmware / product license | After purchase/download agreement | On card | **Never commit** | CIFX 50E-RE | **NXLIC-MASTER 8211.000** |
| CIFXDPM / NXLFW-DPM | 7428.410 (target SKU) | Hilscher firmware | After purchase | On card | **Never commit** | CIFX 50E-DP | **NXLIC-MASTER 8211.000** |
| Protocol API headers (PNM/DPM mailbox) | firmware docs | Product documentation | When provided with firmware SDK | Mailbox DCP/AR/DP-V1 | **Do not invent**; do not commit if restricted | Card + FW | Master license on card |

**Free download of NXDRV-LINUX does not mean NXLIC-MASTER or firmware is free.**

---

## Distinction (authoritative)

| Kind | Status here |
| --- | --- |
| SDK/toolkit license | libcifx MIT + Toolkit HSLA — **development use OK** |
| Driver license | Linux user-space MIT; kernel uio **GPL-2** |
| Firmware license | **Not in this repo / this VM** |
| Master/runtime license | **Not present** — bound to hardware serial |
| Hardware | **Not present** |
| OEM/redistribution of firmware | **Not granted** by cloning GitHub |

---

## Official APIs used (no invented names)

From `cifXUser.h` / `cifxlinux.h` (NXDRV-LINUX):

- `cifXDriverInit` / `cifXDriverDeinit` (Linux)
- `xDriverOpen` / `xDriverClose` / `xDriverGetErrorDescription`
- `xChannelOpen` / `xChannelClose` / `xChannelInfo`
- `xChannelHostState` / `xChannelBusState`
- `xChannelDownload` (`DOWNLOAD_MODE_CONFIG`)
- `xChannelIORead` / `xChannelIOWrite` / `xChannelIOInfo`

**Not implemented (headers not in NXDRV-LINUX):** PROFINET DCP/AR mailbox command structs from NXLFW-PNM Protocol API; PROFIBUS DP-V1/GSD packet IDs from NXLFW-DPM Protocol API. Those remain **HARDWARE + FIRMWARE PROTOCOL API PENDING**.

---

## Build flags

```bash
cmake -S . -B build \
  -DVF_ENABLE_HILSCHER_PROFINET=ON \
  -DVF_ENABLE_HILSCHER_PROFIBUS=ON \
  -DHILSCHER_CIFX_ROOT=/path/to/libcifx/prefix
```

Defaults: both flags **OFF**. Finding libcifx does **not** silently enable the backend.
