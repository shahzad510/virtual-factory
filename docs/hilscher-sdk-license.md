# Hilscher SDK / license / runtime boundary

**Status:** documentation only. Proprietary material is **not** in Git.

## What may be used

| Item | License / notes | In this repo? |
| --- | --- | --- |
| libcifx (cifX user API) | MIT (nxdrvlinux) | **External** — `.deps/libcifx` is gitignored |
| cifX Toolkit sources | Hilscher Software License Agreement | External / not committed |
| `uio_netx` kernel module | GPL-2 | Not committed |
| NXLFW-PNM / CIFXDPM firmware | Proprietary, licensed | **NOT IN GIT** |
| NXLIC-MASTER | Proprietary license code | **NOT IN GIT** |

## Required at plant runtime (not in this branch)

### Native PROFINET

1. CIFX 50E-RE (or equivalent netX PROFINET controller hardware)
2. NXDRV (Windows) or nxdrvlinux + `uio_netx` (Linux)
3. NXLFW-PNM PROFINET IO-Controller firmware
4. NXLIC-MASTER (or equivalent master license)
5. SYCON.net / Communication Studio configuration artifact (`config.nxd`)
6. Real or vendor-supported software PROFINET IO-Device + GSDML

### Native PROFIBUS

1. CIFX 50E-DP
2. CIFXDPM / NXLFW-DPM firmware
3. Master license
4. Real DP slave + GSD
5. SYCON configuration artifact

## Git policy

Do not commit:

- firmware (`.nxf`, `.nxi`, `.nxi` modules)
- license keys / `NXLIC*`
- passwords / credentials
- vendor binaries unless redistribution rights explicitly permit it

`.gitignore` excludes `.deps/`, `build-cifx/`, and common firmware extensions.

## Protocol API boundary

`libcifx` is the **host DPM API**. It is **not** a software PROFINET or PROFIBUS stack. Protocol features (DCP, AR, DP-V1, GSD) run **on the netX** inside licensed firmware.

If a host mailbox command is not present in the public cifX headers used here:

> Requires Hilscher Protocol API / firmware / hardware.

Do not invent packet IDs.
