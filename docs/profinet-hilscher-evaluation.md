# Hilscher cifX — native PROFINET feasibility evaluation (ICP)

**Status:** Evaluation **COMPLETE** (2026-08-29). Native PROFINET remains **NOT IMPLEMENTED**. Gateway path remains **SUPPORTED VIA GATEWAY**.

**Follow-up:** Final commercial/platform gate — [`profinet-hilscher-final-gate.md`](profinet-hilscher-final-gate.md) (SKU lock **CIFX 50E-RE 1251.100**; recommendation **C**).

**Context:** Softing PROFINET Controller Stack remains the **primary software-centric candidate**, but the Softing **SDK/EULA/redistribution gate FAILED** in this environment (no SDK, no signed commercial terms). This document evaluates **Hilscher** as the **alternate** path — **not** an automatic selection.

**Authority:** ADR-040, [`profinet-native-evaluation.md`](profinet-native-evaluation.md), [`profinet-native-implementation-plan.md`](profinet-native-implementation-plan.md).

**Smoke test:** **NOT RUN** — no cifX hardware, firmware package, or licensed master stack present in this environment. No fabricated success.

---

## 1. Executive recommendation

**Is Hilscher a commercially viable native PROFINET engine for ICP?**

**YES — with mandatory product constraints.**

| Verdict | Meaning |
| --- | --- |
| Technically suitable | cifX + **NXLFW-PNM** (PROFINET IO-Controller LFW) + cifX C API is a real IO-Controller path with RT cyclic IO, DCP, multi-AR, diagnostics |
| Commercially suitable | Hardware + **master license** SKU model is well-understood for industrial PC products; redistribution is typically “ship ICP + require/include Hilscher card & licenses” |
| Product honesty required | **Native PROFINET deployment requires Hilscher communication hardware** (cifX / netX). This is not a soft-NIC-only Softing-style software stack |
| Softing relationship | Softing remains preferred **if/when** SDK + OEM redistribution are procured. Hilscher is the **first implementable alternate** once card + master license + drivers are available |

**Do not implement** until: (1) explicit user approval of Hilscher path, (2) cifX hardware + NXLFW-PNM + master license in hand, (3) isolated smoke test passes against real IO-Device or vendor-approved sim.

---

## 2. Exact product stack evaluated

| Layer | Product | Evidence |
| --- | --- | --- |
| Hardware | **cifX** PC cards / modules (PCI, PCIe, M.2, etc.) with Real-Time Ethernet interface | Hilscher cifX portfolio |
| SoC | **netX** (protocol stack runs **on the card**, not on host soft-NIC) | Hilscher netX architecture |
| Firmware | **NXLFW-PNM** — Loadable Firmware PROFINET IO-Controller (part **7428.840**) | Hilscher product page |
| Host API | **cifX API** (C): `cifXDriverInit`, `xChannelOpen`, `xChannelIORead`, `xChannelIOWrite`, packet mailbox services | cifX driver docs + PN Controller Protocol API |
| Drivers | **NXDRV-WIN** (Windows); **NXDRV-LINUX** / GitHub `HilscherAutomation/nxdrvlinux` (Linux: `uio_netx` GPL + `libcifx` MIT) | Hilscher confluence / GitHub |
| Config tools | **SYCON.net** and/or **Communication Studio** + Device Library; export **`config.nxd`** (+ related DBM) | Hilscher support docs |
| Toolkit | **NXDRV-TKIT** cifX Toolkit (C sources for DPM/API porting) | Hilscher |

**Critical product statement (must appear in ICP packaging/docs if chosen):**

> Native PROFINET deployment requires Hilscher communication hardware (cifX/netX) with PROFINET IO-Controller firmware and a **master license**. Ordinary host NICs alone are **not** sufficient for this path.

---

## 3. Capability checklist (from public Hilscher docs)

| # | Topic | Finding |
| --- | --- | --- |
| 1 | Suitable cifX for PN Controller | Any cifX Real-Time Ethernet form factor that loads **PNM** firmware (e.g. CIFX 50E-RE, PCIe, M.2 RE variants) — confirm exact SKU with Hilscher for master |
| 2 | Firmware | **NXLFW-PNM** (PROFINET IO-Controller); Protocol API docs describe V3.x features (≤128 ARs) |
| 3 | SDK/API | cifX Device Driver + cifX API DLL/lib; Protocol packet API for PN services |
| 4 | Driver package | NXDRV-WIN / NXDRV-LINUX |
| 5 | Headers/libs | Available with driver/toolkit install (**not** present in this CI/cloud environment) |
| 6 | C/C++ | **C API**; C++17 ICP can wrap privately |
| 7–11 | OS | See platform matrix §5 |
| 12 | CPU | Host **x86/x64**; netX on card. Windows driver: no IA64/ARM host |
| 13 | Hardware | **PCIe / PCI / M.2 / etc. cifX module required** |
| 14 | NIC | Card’s own Ethernet port(s) to PN switch; host NIC not the PN wire |
| 15 | Who does RT? | **netX on cifX** executes the stack / cyclic frames — host exchanges DPM IO |
| 16 | RT Class 1 | **Yes** (RTC cyclic; send clocks 1/2/4 ms documented for RT) |
| 17 | Cyclic process data | **Yes** — `xChannelIORead` / `xChannelIOWrite` |
| 18 | DCP | **Yes** — DCP Set Name/IP/Signal/Reset, Identify, Get via API |
| 19 | Device naming | **Yes** (DCP + config; auto name assignment feature in controller FW) |
| 20 | AR | **Yes** — up to **128** ARs (RT); IRT subset also in FW (out of first ICP slice) |
| 21 | Slots/subslots | **Yes** — submodule handles; ≤2048 submodules globally (FW V3 datasheet) |
| 22 | Multi IO-Device | **Yes** — one controller, many devices/ARs |
| 23 | Diagnostics/alarms | **Yes** — alarm handling (auto and/or application); diagnosis via tools/API |
| 24 | GSDML | Import device GSDML in SYCON.net / Communication Studio; **not** a custom ICP parser |
| 25 | Config API | Offline DB (`config.nxd`) download and/or packet configure services |
| 26 | Process-data map | Engineering defines IO image; ICP maps byte/bit offsets → GenericEquipment |
| 27 | Start/stop | Driver loads FW+config; `xSysdeviceReset` / channel open-close patterns |
| 28 | Device failure | Per-AR / diagnosis; must isolate in adapter mapping |
| 29 | Reconnect | Explicit host reconnect / re-download / channel reopen — align with ICP explicit `connect()` |
| 30 | Multi cards | Windows driver: unlimited boards; multiple adapters = multiple controllers |
| 31 | Multi networks | One card/IF per PN segment; multiple cards for multiple networks |
| 32 | Threading | Host app threads + FW cyclic on netX; Windows **not** deterministic RT OS (Hilscher note) |
| 33 | Poll/read | `xChannelIORead` (poll-friendly for PollScheduler) |
| 34 | Event/callback | Packet indications / mailbox; optional beyond first slice |
| 35 | Latency | Cycle times ms-class RT; host IO sync to green phase; measure on hardware |
| 36–38 | Docker | See §6 |
| 39–44 | License | **Master license required** for IO-Controller; slave FW free of master license. OEM = buy/resell cards + licenses — **confirm redistribution of drivers/FW in ICP installer with Hilscher legal** |
| 45 | Support | Hilscher commercial support / confluence |
| 46 | Certification | FW marketed precertified stack; **ICP-as-controller product** still needs own conformance path if claimed |

---

## 4. Softing SDK gate vs Hilscher readiness (this environment)

| Gate | Softing | Hilscher |
| --- | --- | --- |
| SDK/headers/libs in environment | **Missing** | **Missing** |
| Hardware in environment | N/A (soft NIC path) | **No cifX device** |
| Signed OEM/redistribution | **Unavailable** | **Not signed here** — model is clearer (HW+master license) but still needs contract |
| Smoke test | Blocked | **Blocked** (no card) |
| Public API/docs sufficiency to design wrapper | Partial | **Strong** (cifX API + Protocol API PDF) |

---

## 5. Platform matrix (evidence-based — no unsupported ✓)

| | Win10 | Win11 | WinServer | Ubuntu 24.04 | Docker Linux | Docker Desktop Win |
| --- | --- | --- | --- | --- | --- | --- |
| **ICP Core** (non-PN) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **PROFINET gateway** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Hilscher native PN** | **✓*** | **✓*** | **?**† | **?**‡ | **?**§ | **✗** |

\* NXDRV-WIN documents Windows 10 support and **verified on Windows 11** (driver datasheet / confluence). Still requires our ICP smoke test on target builds.  
† Hilscher FAQ: Server **not** generally tested; one-off Server **2019** demo only — treat as **unsupported until validated**.  
‡ Linux driver on GitHub supports modern kernels; **Ubuntu 24.04 specifically unvalidated here**.  
§ Feasible only with **PCIe/device passthrough**, host drivers, dedicated card — not a default ICP container profile.  
**✗** Docker Desktop on Windows does **not** provide equivalent plant L2 / cifX passthrough for production native PN.

**Deployment distinction:** Prefer native Windows/Linux **service or desktop IPC process** with physical cifX. Do not promise Docker Desktop Win native PN.

---

## 6. Docker detail

| Scenario | Assessment |
| --- | --- |
| Linux Docker + cifX | Possible only if container sees the PCIe device (`--device` / VFIO), host has uio/libcifx, and FW/config under `/opt/cifx` (or mounted). Privileged or carefully capped capabilities. RT quality depends on host. |
| Host networking | Helps IP coexistence; **PN cyclic still on cifX ports**, not Docker bridge eth0 |
| Docker Desktop (Win) | VM isolation; **no reliable cifX/PCIe plant PN** — document as **unsupported** for native PN |
| Default ICP Docker image | Keep **gateway-only** PN; optional “native PN host agent” beside containers |

---

## 7. GSDML / configuration workflow (preferred)

```text
Device vendor GSDML
        ↓
SYCON.net / Communication Studio (import GSDML, build topology)
        ↓
Export config.nxd (+ related)
        ↓
cifX driver loads NXLFW-PNM + config.nxd to card
        ↓
ICP ProfinetIndustrialAdapter maps process-image offsets → GenericEquipment
```

**Do not** invent a custom GSDML parser for the first slice.

Future ICP Designer: wrap this workflow (import GSDML → invoke/vendor-export → map signals → deploy).

---

## 8. Target ICP architecture (if approved later)

```text
ProfinetIndustrialAdapter   // one instance = one cifX channel / PN controller
        ↓
private profinet_session    // cifX API only in .cc — no public Hilscher types
        ↓
libcifx / NXDRV
        ↓
cifX / netX (NXLFW-PNM)
        ↓
PROFINET IO-Devices (many ARs)
```

Topology: **one controller → many IO-Devices** (≠ EtherNet/IP one-session model).

Fault isolation: AR/device A failure must not mark unrelated GenericEquipment as process-faulted; `ConnectionState::Faulted` ≠ `Equipment::fault()`.

---

## 9. Softing vs Hilscher comparison

| Criterion | Softing Controller Stack | Hilscher cifX + NXLFW-PNM |
| --- | --- | --- |
| Controller capability | Yes (public datasheet) | **Yes** (Protocol API) |
| RT Class 1 | Yes | **Yes** |
| Cyclic IO | Yes | **Yes** (`IORead`/`IOWrite`) |
| Multi-device | ≤255 (datasheet) | **≤128 ARs** (FW V3) |
| DCP | Yes | **Yes** |
| Diagnostics | Yes | **Yes** |
| GSDML | Communication Configurator | **SYCON.net / Communication Studio** |
| Windows 10 | Product SKU exists | **Driver supported** |
| Windows 11 | Claimed/needs verify | **Driver verified** (Hilscher) |
| Windows Server | Needs Softing confirm | **?** (one-off 2019 only) |
| Ubuntu 24.04 | Needs Softing port | **?** (Linux driver exists; 24.04 untested here) |
| Docker Linux | Soft L2 hard | **Hardware passthrough** |
| Docker Windows | ✗ plant L2 | **✗** |
| Hardware requirement | Soft NIC (+ privileges) | **Mandatory cifX card** |
| NIC requirement | Host IF L2 | **Card Ethernet ports** |
| API | PNAK / SCAI (unseen here) | **cifX C API** (documented) |
| C++ integration | Via C wrapper | Via C wrapper |
| License | Commercial OEM (unavailable here) | **Master license + HW** |
| OEM redistribution | Unclear without contract | Ship/require Hilscher HW+licenses — **confirm legal** |
| Runtime licensing | Softing quote | Per-card master license model |
| Cost model | Soft+runtime fees | HW + master license (+ tooling) |
| Driver complexity | Softing porting layer | cifX driver mature |
| Deployment complexity | Soft NIC privileges | Install card + driver + FW |
| Industrial suitability | High if licensed | **High** (common IPC pattern) |
| ICP suitability now | **Blocked** (no SDK) | **Viable when HW+license acquired** |

---

## 10. Remaining blockers before implementation

1. No cifX hardware in evaluation environment.  
2. No NXLFW-PNM / config package / master license on disk.  
3. No signed Hilscher OEM terms for bundling drivers/FW in ICP installer.  
4. No real IO-Device or Hilscher-approved simulator exercised here.  
5. Ubuntu 24.04 and Windows Server not validated by us.  
6. Softing still blocked — do not guess Softing APIs.

**Official simulator:** Hilscher tooling diagnoses controllers/devices; a full substitute for a real IO-Device is **not** established as sufficient alone. Prefer **real PN IO-Device** for acceptance.

---

## 11. Proposed next steps (awaiting approval — **not started**)

1. Procure: cifX RE card + **master license** + NXLFW-PNM + Communication Studio/SYCON.net access.  
2. Legal: redistribution of NXDRV + FW in ICP packages.  
3. Isolated throwaway smoke test (headers link, detect card, load FW, IO read/write, one device).  
4. Only then: `ProfinetIndustrialAdapter` + private session + ICP-1A integration.  
5. Keep Softing as parallel software-SKU track when Softing SDK arrives.

---

## 12. Status labels (authoritative)

| Item | Status |
| --- | --- |
| Softing | PRIMARY candidate — **SDK gate FAILED** |
| Hilscher | ALTERNATE — **FEASIBLE**; **NOT SELECTED FOR CODE** until approval + HW |
| Native PROFINET | **NOT IMPLEMENTED** |
| Gateway PROFINET | **SUPPORTED VIA GATEWAY** |
| `ProfinetIndustrialAdapter` | **NOT STARTED** |
