# Hardware smoke-test checklist — native PROFINET + PROFIBUS (Hilscher)

**Status:** Procedure **PREPARED**. Execution **HARDWARE VALIDATION PENDING**.

Do not mark native fieldbus **SUPPORTED** or **HARDWARE VALIDATED** until this checklist is executed on real cards and recorded with dates, firmware versions, and results.

Software-only tests (`native_fieldbus_scaffolding_test`) are **SOFTWARE-INTEGRATION TESTED** only.

---

## Prerequisites (procurement)

### PROFINET

| Item | SKU / note |
| --- | --- |
| Card | CIFX 50E-RE part **1251.100** |
| Firmware | NXLFW-PNM part **7428.840** (RT Class 1) |
| Master license | NXLIC-MASTER **8211.000** (on card serial) |
| Driver | NXDRV-LINUX (this Linux host) or NXDRV-WIN |
| Config | SYCON.net / Communication Studio export (`.nxd` / vendor artifact) |
| Device | ≥1 certified PROFINET IO-Device + GSDML |
| Network | 100 Mbit full-duplex switched segment on **card** RJ45 (not host NIC) |

### PROFIBUS

| Item | SKU / note |
| --- | --- |
| Card | CIFX 50E-DP part **1251.410** |
| Firmware | CIFXDPM.NXF / NXLFW-DPM **7428.410** |
| Master license | NXLIC-MASTER **8211.000** |
| Config | SYCON.net GSD-derived master/slave config |
| Slave | ≥1 DP slave + GSD |
| Wiring | RS-485, termination, baud matching config |

### Simultaneous

Two cards in one host (RE + DP). **Do not claim simultaneous support until section C passes.**

---

## A. PROFINET (CIFX 50E-RE)

Record pass/fail, exact error codes from `xDriverGetErrorDescription`, firmware name from `xChannelInfo`.

1. PCI enumeration (`lspci` / Device Manager) — card visible  
2. Driver loaded (`uio_netx` / NXDRV-WIN)  
3. `cifXDriverInit` + `xDriverOpen`  
4. `xDriverEnumBoards` lists `cifX0` (or configured alias)  
5. Firmware name indicates PROFINET IO-Controller (NXLFW-PNM)  
6. Channel open (`xChannelOpen`)  
7. `xChannelHostState(READY)`  
8. Config download `xChannelDownload(DOWNLOAD_MODE_CONFIG)`  
9. Ethernet link on card ports  
10. `xChannelBusState(ON)`  
11. DCP / device identification — **requires NXLFW-PNM Protocol API packets; execute only with official PNM headers**  
12. AR establishment — same  
13. RT Class 1 cyclic `xChannelIORead` / `xChannelIOWrite`  
14. Mapped telemetry appears in `GenericEquipment` (not `Equipment::fault` on comms)  
15. Diagnostics / extended status block  
16. Disconnect / `xChannelBusState(OFF)` / host not ready  
17. Explicit reconnect (`connect()` after `disconnect()`)  
18. Device failure isolation (second IO-Device remains mapped)  
19. Recovery after cable pull  

---

## B. PROFIBUS (CIFX 50E-DP)

1. PCI enumeration  
2. Driver loaded  
3. Driver/channel open on **second** board id (e.g. `cifX1`)  
4. Firmware name indicates DP Master  
5. Master config load (GSD-derived artifact)  
6. `xChannelBusState(ON)`  
7. Slave cyclic input  
8. Slave cyclic output  
9. Diagnostics  
10. Slave timeout  
11. Recovery  
12. Explicit reconnect  
13. Isolation between slaves  

---

## C. Simultaneous PN + PB

1. Both boards enumerated  
2. Two `IndustrialAdapter` instances in `AdapterManager`  
3. Both buses cyclic  
4. Fault PN (`BusState OFF` or unplug) — PB equipment not `Faulted`  
5. Fault PB — PN remains  
6. Independent reconnect  
7. Thread/safety: `PollScheduler` poll of both  
8. `LiveStateCache` ids isolated  

**Status until executed:** **UNVERIFIED** / **HARDWARE VALIDATION PENDING**

---

## Platform labels (do not upgrade without evidence)

| Platform | Native PN/PB |
| --- | --- |
| Ubuntu 24.04 + NXDRV-LINUX | **TECHNICALLY POSSIBLE** — libcifx **SOFTWARE-INTEGRATION TESTED** here; **HARDWARE VALIDATION PENDING** |
| Windows 10/11 + NXDRV-WIN | **TECHNICALLY POSSIBLE** (vendor driver exists) — **UNVERIFIED** in this environment |
| Windows Server | **UNVERIFIED** — Hilscher does not default-test Server |
| Docker Linux default | **UNSUPPORTED** as default; prefer host driver + passthrough/agent |
| Docker Desktop Windows | **UNSUPPORTED** for plant fieldbus until proven |

---

## Pass criteria for “HARDWARE VALIDATED”

All of A (items 1–10 and 13–17) on real IO-Device, **or** documented deviations. DCP/AR (11–12) when PNM protocol headers are legally available.

Do **not** use a fake TCP/UDP PROFINET or fake serial PROFIBUS as a substitute.
