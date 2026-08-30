# PROFIBUS native connectivity evaluation (ICP)

**Status:** Evaluation **COMPLETE** (2026-08-30). **No production code.** No SDKs installed. Gateway path remains a required supported fallback.

**Scope:** Architecture, technology, hardware, licensing, and product evaluation only. No `ProfibusIndustrialAdapter`, no CMake changes, no protected header edits.

**Authority:** ADR-042 (ICP product), ADR-040 (fieldbus pattern), [`icp-product-architecture.md`](icp-product-architecture.md), [`profinet-hilscher-final-gate.md`](profinet-hilscher-final-gate.md) (on branch `cursor/icp-6h-hilscher-final-gate-a88d`).

**Evidence tiers:** **CONFIRMED** | **INFERRED** | **THIRD-PARTY** | **UNKNOWN**

---

## Executive summary

| Question | Answer |
| --- | --- |
| Best native PROFIBUS architecture for ICP? | **Hilscher cifX** DP hardware + **CIFXDPM** firmware + **cifX API** — same platform family already selected for native PROFINET |
| Same card as PROFINET (CIFX 50E-RE)? | **No** — RE is **Ethernet**; PROFIBUS needs **RS-485** (e.g. **CIFX 50E-DP** **1251.410**) |
| Same product family / driver / license? | **Yes** — netX 100, **NXDRV-WIN/LINUX**, **NXLIC-MASTER 8211.000**, **SYCON.net / Communication Studio**, **cifX API** |
| Run PN + PB simultaneously on one host? | **Yes** — **two cards** (e.g. **50E-RE** + **50E-DP**) or dedicated dual-channel PB card (**50E-2DP**) for two PB segments — **not** one RE card for both |
| Software-only master on std NIC? | **No** for credible production ICP — PROFIBUS requires **RS-485** and strict bus timing |
| Recommendation | **F — support BOTH native + gateway**; native primary = **Hilscher** (align with PN platform); Softing alternate |

---

## 1. PROFIBUS technology relevant to ICP

| Aspect | PROFIBUS DP | vs PROFINET (do not conflate) |
| --- | --- | --- |
| Physical layer | **RS-485** serial bus (9-pin D-Sub typical) | **Ethernet** 100M (Layer 2 RT) |
| Topology | Line/trunk with terminators; up to 12 Mbit/s | Switched Ethernet; RT Class 1 cyclic |
| Master role | **DP Master Class 1** polls slaves cyclically | **IO-Controller** exchanges cyclic IO with IO-Devices |
| Addressing | Station address 0–125 on bus | IP + station name (DCP) |
| Engineering | **GSD** files (keyword/GSD language for DP) | **GSDML** (XML) |
| Timing | Token/cycle on serial bus — **ASIC/firmware timing critical** | ms-class cycles on netX/Ethernet |
| Discovery | Live list / configuration-driven; not plug-and-play like IP | DCP, LLDP |

**Shared concepts (ICP mapping):** master → many slaves/devices; process image → adapter → `GenericEquipment`; vendor config tooling; comms fault ≠ process fault.

**Not shared:** PHY, stack, firmware files, NIC vs RS-485 interface, Docker passthrough model.

---

## 2. PROFIBUS role required for ICP

**CONFIRMED requirement for native path:** **PROFIBUS DP Master Class 1** (cyclic input/output, diagnostics, parameterization via DP-V0; DP-V1 acyclic as stretch goal).

**Not in first scope:** DP Class 2 master (acyclic engineering while another master runs), MPI-only, PA physical layer on ICP host, PROFIBUS FMS (legacy).

**Topology:**

```text
ProfibusIndustrialAdapter  (= one DP master instance)
        │
        ├── DP Slave (station addr N)
        ├── DP Slave
        └── DP Slave (up to stack limit)
```

This mirrors **PROFINET** (one controller → many IO-Devices), **not** EtherNet/IP (one adapter ≈ one device session). It **partially** mirrors **Modbus** (one TCP endpoint → many logical equipment mappings on one bus).

**ICP use case:** Connect ICP to existing PROFIBUS segments (remote IO, drives, gateways, legacy PLCs as slaves) and normalize to `GenericEquipment` for CIC/MES/SCADA — same strategic role as native PROFINET.

---

## 3. DP Master architecture

```text
ICP Core (platform-independent)
        ↓
ProfibusIndustrialAdapter
        ↓
private profibus_session   // no public vendor types
        ↓
cifX API (or alternate stack API)
        ↓
Host driver (NXDRV-WIN / NXDRV-LINUX)
        ↓
cifX card (netX runs DP Master firmware)
        ↓
RS-485 PROFIBUS cable
        ↓
DP Slaves
```

Stack executes on **netX** (Hilscher path) or interface ASIC (Softing PBpro path) — host is **not** a bit-banging master in production architecture.

---

## 4. Hardware requirements

| Path | Hardware |
| --- | --- |
| **Hilscher native (recommended)** | cifX PCIe card with **isolated RS-485** (e.g. **CIFX 50E-DP 1251.410**) |
| **Softing native (alternate)** | **PBpro PCI/PCIe/USB** with RS-485 |
| **Siemens** | **CP5612 / CP5622** + SIMATIC NET PC software |
| **Software-only / UART** | USB/serial RS-485 converter — **experimental only** (timing/jitter) |
| **Gateway** | No ICP fieldbus hardware — gateway exposes OPC UA/Modbus/MQTT/REST |

**Standard Ethernet NIC is insufficient** for native PROFIBUS DP.

---

## 5–11. Stack options summary

| # | Vendor | Master? | HW | SW-only? | API | Win | Linux | Maturity | ICP fit |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 5 | **Hilscher** | **Yes** DP Master | cifX + RS-485 | No | **cifX C API** | **CONFIRMED** NXDRV-WIN | **INFERRED** nxdrvlinux | High (Kepware Universal pattern) | **Best** — same family as PN |
| 6 | **Softing** | **Yes** DP-V0/V1/V2 | PBpro cards | No (stack+HW) | PROFIBUS API C/C++ | **CONFIRMED** | Selected distros / PCIe **Linux in prep** per datasheet | High | Strong alternate |
| 7 | **Siemens** | **Yes** via CP5622 | CP5612/5622 PCIe | No | SIMATIC NET / OPC | **CONFIRMED** Windows | **Not** ICP-primary | High in Siemens shops | Situational OEM |
| 8 | **HMS/Anybus** | Master products exist; PCI line often **slave** | Anybus PCI/CompactCom | No | Anybus-S API | Legacy Windows focus | Limited | Medium | Not first choice |
| 9 | **OSS profirust** | DP-V0 master (Rust) | UART/RS-485 | Userspace Linux | Rust (not C++) | Via serial | Linux TTY | Experimental | **Reject** for v1 commercial ICP |
| 10 | **Legacy pbmaster/ProfiM** | Abandoned C projects | Serial | Claimed | C | Old Linux/Win | Abandoned | **Reject** |
| 11 | **Gateway** | N/A (not master) | None on ICP | N/A | Existing ICP adapters | All | All | Proven | **Always supported** |

**Cost:** **QUOTE REQUIRED** for all commercial options.

---

## 12. Hilscher — exact hardware compatibility (HIGH PRIORITY)

### Same family as PROFINET — but not the same card

| Item | PROFINET (current ICP selection) | PROFIBUS (this evaluation) |
| --- | --- | --- |
| Card | **CIFX 50E-RE** **1251.100** | **CIFX 50E-DP** **1251.410** |
| PHY | 2× RJ45 Ethernet | 9-pin D-Sub **RS-485** |
| netX | **netX 100** | **netX 100** |
| Firmware file (cifX) | **C010C000.NXF** (PROFINET IO-Controller) per cifX manual rev.59 | **CIFXDPM.NXF** (PROFIBUS DP Master) per same manual |
| LFW product (netX companion) | **NXLFW-PNM 7428.840** | **NXLFW-DPM 7428.410** |
| Master license | **NXLIC-MASTER 8211.000** | **Same** **8211.000** (**CONFIRMED** on CIFX 50E-DP product page) |
| Driver | **NXDRV-WIN** / **NXDRV-LINUX** | **Same** |
| API | **cifX API** + DP Protocol API | **Same** family |
| Config | **SYCON.net** / **Communication Studio** | **Same** (+ PROFIBUS GSD import) |

**CONFIRMED:** Hilscher platform strategy — one driver/tool chain; protocol changed by **loading different firmware** (cifX manual §2.4, product pages).

**CONFIRMED:** **CIFX 50E-RE cannot serve PROFIBUS** in production — no RS-485 transceiver on that SKU (device drawings: RJ45 only).

### Firmware switching vs simultaneous protocols

| Scenario | Supported? | Evidence |
| --- | --- | --- |
| One **50E-RE**: load PNM today, DPM tomorrow | **INFERRED** possible but **pointless** — no RS-485 wire | Manual firmware table lists files; PHY mismatch |
| One **50E-DP**: only PROFIBUS | **CONFIRMED** | SKU design |
| One card: **PN + PB at same time** | **No** on single-channel RE or DP cards | Different PHY; one firmware image per channel |
| **50E-2DP** (1253.410): 2× PROFIBUS masters | **CONFIRMED** | Two RS-485 channels; firmware **CIFX2DPM.NXF**; license **NXLIC-MASTER 2 LIZENZEN 8212.000** |
| Host: **50E-RE + 50E-DP** simultaneous | **CONFIRMED** architecture | Unlimited boards per Windows driver; two adapters in ICP |
| One card: Ch1=PN, Ch2=PB | **No active SKU found** | Dual-channel cards are 2×PB, 2×DN, or EOL mixed PB+CANopen — not PN+PB |

**Answer — same hardware family for both protocols?** **Yes** (cifX + netX + license + driver + API).  
**Answer — same physical card for both?** **No** — need **RE** (Ethernet) + **DP** (RS-485) for simultaneous native PN and PB.

### Standardized SKU for ICP Industrial (PN + PB)

| Component | Part | Qty (typical dual-fieldbus IPC) |
| --- | --- | --- |
| PROFINET | **CIFX 50E-RE 1251.100** + **NXLFW-PNM 7428.840** | 1 |
| PROFIBUS | **CIFX 50E-DP 1251.410** + DPM firmware (driver package) | 1 |
| Master licenses | **NXLIC-MASTER 8211.000** | 2 (one per master channel/card) |
| Alternative: two PB segments, one slot | **CIFX 50E-2DP 1253.410** + **8212.000** (2 master licenses) | 1 |

---

## 13–20. Platform matrix (Hilscher primary path)

| Platform | Native PROFIBUS (Hilscher) | Notes |
| --- | --- | --- |
| **Windows 10** | **Supported path** | Same NXDRV-WIN as PN |
| **Windows 11** | **Supported path** | Driver verified Win11 (PN gate) applies to same driver |
| **Windows Server** | **Not production-supported** | Same Server caveat as PN final gate — one-off FAQ only |
| **Ubuntu 24.04** | **TECHNICALLY POSSIBLE — NOT VENDOR VERIFIED** | nxdrvlinux + uio_netx |
| **Docker Linux** | Host driver + device passthrough or **host PB agent** | Same pattern as PN; not default in-container |
| **Docker Desktop Windows** | **Unsupported** for native fieldbus | Native Windows service only |

---

## 21–25. GSD · configuration · cyclic IO · diagnostics · multi-slave

| Topic | Hilscher path |
| --- | --- |
| **GSD** | Import slave GSD in **SYCON.net / Communication Studio**; export **config.nxd** (or DBM) — **do not build custom GSD parser in v1** |
| **Device configuration** | Station address, baud rate, modules, process data length in vendor tool |
| **Cyclic process data** | **CONFIRMED** — DPM firmware; host **xChannelIORead/Write** |
| **Diagnostics** | DP diagnostics via stack/tools/API; map to adapter health + selective `Equipment::fault()` |
| **Multi-slave** | One master → many slaves (**INFERRED** typical limit 126 stations per PROFIBUS spec; confirm in Protocol API for deployed FW version) |
| **Multi-master** | Separate adapters/cards per master segment — **do not** share one master across adapters |
| **Parameterization** | Acyclic DP-V1 — plan as later slice |

---

## 26. Simultaneous PROFINET + PROFIBUS architectures

| Option | Verdict |
| --- | --- |
| **A** One card, PN firmware only | PB **impossible** on RE hardware |
| **B** One multi-channel PN+PB | **No active Hilscher SKU** |
| **C** **50E-RE (PN) + 50E-DP (PB)** on one ICP host | **RECOMMENDED** — **CONFIRMED** viable |
| **D** Software PB + Hilscher PN | Theoretically possible (Softing PB + Hilscher PN) — **two vendors**, higher support cost |
| **E** Gateway-only PB | **Always valid** — no native PB hardware |

**One ICP host run PN and PB simultaneously?** **Yes**, with **two interfaces** (typically **two cifX cards** or one **50E-2DP** for two PB buses + **50E-RE** for PN).

---

## 27. ICP architecture (future — NOT IMPLEMENTED)

```text
ICP
 ├── AdapterManager / PollScheduler / LiveStateCache
 └── Industrial adapters
       ├── … existing …
       ├── ProfinetIndustrialAdapter  (optional SKU)
       ├── ProfibusIndustrialAdapter  (optional SKU)
       └── Gateway-backed paths (always)
```

```text
ProfibusIndustrialAdapter
        ↓
private profibus_session    // cifX API only in .cc
        ↓
NXDRV + CIFXDPM firmware
        ↓
CIFX 50E-DP (RS-485)
        ↓
many DP Slaves
```

**No** Hilscher/Softing types in `Equipment.hh`, `IndustrialAdapter.hh`, `GenericEquipment`, CIC, or MES.

---

## 28. Configuration model (proposed)

Vendor-tool-first; ICP stores **references**, not a full GSD parser:

```text
ProfibusAdapterConfig
  adapterId
  cifxBoardId / channel     // maps to cifX slot/card
  baudRate                  // 9600 … 12M
  masterAddress             // classically 1 or 2
  configArtifactPath        // exported config.nxd / project from SyCon
  pollIntervalMs

ProfibusSlaveMapping
  equipmentId
  stationAddress
  vendorId / deviceId       // from GSD / config
  inputByteOffset / outputByteOffset / bit map
  diagnosticRules[]         // map DP diag → Equipment::fault()
```

Exact field names to align with cifX Protocol API during implementation slice approval.

---

## 29. ICP Designer implications (future)

Designer workflow parity with PN:

1. Select **Native PROFIBUS (Hilscher)** vs **Gateway**
2. Select **cifX board/channel**
3. Import/configure via **SYCON.net** (or pre-built config artifact)
4. Map process image bytes → equipment telemetry/commands
5. Deploy config + driver prerequisites to target IPC
6. Show **communication status** separately from **equipment fault**

---

## 30. Gateway architecture (always supported)

```text
PROFIBUS DP slaves
        ↓
Field gateway (Anybus X-gateway, PLC proxy, etc.)
        ↓
OPC UA / Modbus / MQTT / REST
        ↓
Existing ICP adapter
        ↓
GenericEquipment → LiveStateCache → CIC
```

**CONFIRMED pattern** from Kepware/industry: connectivity servers often reach PROFIBUS **through gateways or PLC application protocols**, not as host-resident DP master.

---

## 31. Commercial SKU implications

| SKU | Contents |
| --- | --- |
| **ICP Standard** | All protocol adapters except native PN/PB; **gateway** for PN and PB |
| **ICP Industrial** | Optional native **PN** (50E-RE + PNM + license) and/or native **PB** (50E-DP + DPM + license) |
| **Bundle advantage** | Same **NXLIC-MASTER** model, same driver installer, same cifX API wrapper patterns, same SyCon workflow — **reduced OEM complexity** vs picking unrelated PN and PB vendors |

---

## 32. Testing strategy (when approved — not now)

1. Adapter construction / lifecycle  
2. cifX driver + **CIFXDPM.NXF** load  
3. SyCon config + GSD import  
4. Master start / cyclic input read  
5. Cyclic output / command path  
6. Multi-slave topology  
7. Slave timeout / bus fault → `ConnectionState::Faulted` only for affected mappings  
8. Master restart / reconnect  
9. Two adapters isolation (two masters)  
10. Windows 10/11 smoke  
11. Ubuntu smoke (if claimed)  
12. Docker host-agent pattern (if used)  

**Forbidden:** fake TCP/UDP “PROFIBUS”, fake serial without real DP framing/timing.

---

## 33. Scalability (documented limits only — no benchmark)

| Limit | Source |
| --- | --- |
| DP slaves per segment | PROFIBUS spec **126** stations (addresses 0–125; master typically 1–2) — **CONFIRMED** general spec |
| Cycle time | Baud-dependent (e.g. 12M supports shorter cycles with fewer slaves) — **INFERRED** |
| Multiple segments | Multiple cards/adapters (50E-2DP = 2 PB masters in one slot) | **CONFIRMED** |
| ~1,200 device ICP test | **Out of scope** — do not extrapolate |

---

## 34. Risks

| Risk | Severity |
| --- | --- |
| Confusing PN card (RE) with PB need (DP) | **High** — product/doc error |
| Expecting one card for PN+PB | **High** |
| Ubuntu 24.04 unverified | Medium |
| Windows Server unsupported (driver) | Medium |
| GSD engineering friction without Designer | Medium |
| Softing/Hilscher dual-vendor if mixed | Medium |
| OSS Rust stack for commercial ICP | **High** if chosen |
| OEM redistribution of FW/drivers | **High** — contract required (same as PN) |

---

## 35. Recommendation

### **F — support BOTH native + gateway**

### Native primary: **A — Hilscher native PROFIBUS**

Align with native PROFINET platform:

- **ProfibusIndustrialAdapter** + **private profibus_session** + **cifX API**
- Hardware: **CIFX 50E-DP 1251.410** (or **50E-2DP** for dual PB segment)
- Firmware: **CIFXDPM.NXF** (driver-delivered; companion product **NXLFW-DPM 7428.410**)
- License: **NXLIC-MASTER 8211.000** per master
- Config: **SYCON.net / Communication Studio** + GSD

### Alternate: **B — Softing** (PBpro + PROFIBUS API) if Hilscher procurement fails or customer already standardized on Softing.

### Not recommended for ICP v1

- **D — software-only** on USB/UART RS-485 (profirust/experimental)  
- **E — gateway-only** alone (insufficient for Industrial SKU goal)  
- **C — other commercial** as primary (Siemens/HMS too ecosystem-specific)

---

## 36. Explicit answers (MOST IMPORTANT)

| Question | Answer |
| --- | --- |
| **Same Hilscher hardware family as PROFINET for PROFIBUS Master?** | **Yes** — cifX + netX + NXLIC-MASTER + NXDRV + cifX API + SyCon. **Different SKU:** **50E-DP** (RS-485), not **50E-RE** (Ethernet). |
| **One ICP host run PROFINET and PROFIBUS simultaneously?** | **Yes** — **CIFX 50E-RE** (PN) + **CIFX 50E-DP** (PB) is the **standardized** pattern. **Not** one RE card for both. |
| **Hardware to standardize for both?** | **50E-RE 1251.100** + **NXLFW-PNM 7428.840** + **50E-DP 1251.410** + **DPM firmware** + **NXLIC-MASTER ×2** (+ **NXDRV**). |

---

## 37. Impact on ADR / plans (docs only)

| Doc | Change |
| --- | --- |
| **ADR-046** (new) | PROFIBUS gateway supported; native DP Master approved pending implementation gate |
| **Roadmap / ICP slices** | Add PROFIBUS evaluation complete; implementation slice **NOT STARTED** |
| **Native PN plan** | Unchanged — PB shares Hilscher **platform** but **not** PN card SKU |
| **Implementation** | **BLOCKED** until explicit user approval after this evaluation + hardware smoke |

---

## 38. Sources

1. Hilscher **NXLFW-DPM** product page — part **7428.410**  
2. Hilscher **CIFX 50E-DP** product page — part **1251.410**, **NXLIC-MASTER 8211.000**  
3. Hilscher **CIFX 50E-2DP** product page — part **1253.410**, **8212.000**  
4. Hilscher **CIFX 50E-RE** product page — part **1251.100** (Ethernet — not PB)  
5. Hilscher cifX manual **DOC120204UM59EN** rev.59 — firmware tables **CIFXDPM.NXF**, **C010C000.NXF**, two-channel systems  
6. Softing **PBpro PCI/PCIe** datasheets; PROFIBUS Master flyer  
7. Siemens **CP5622** manual — CP5622 DP/MPI, SIMATIC NET PC  
8. Kepware **Hilscher Universal** evaluation — CIF + Profibus master pattern  
9. **profirust** GitHub/crates.io — experimental Rust master  
10. Prior ICP: `profinet-hilscher-final-gate.md`, `kepware-profinet-architecture-research.md`

---

## Status labels

| Item | Status |
| --- | --- |
| This evaluation | **COMPLETE** |
| ProfibusIndustrialAdapter | **NOT IMPLEMENTED** |
| Gateway PROFIBUS | **SUPPORTED** (conceptual — same pattern as PN gateway) |
| Hilscher PB procurement | **RECOMMENDED** — awaiting approval |
| Smoke test | **NOT RUN** |

**STOP** — no implementation until explicit approval.
