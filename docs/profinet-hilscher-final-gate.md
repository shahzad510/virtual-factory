# Hilscher — FINAL product / platform compatibility gate (ICP native PROFINET)

**Status:** Evaluation **COMPLETE** (2026-08-29). Native PROFINET remains **NOT IMPLEMENTED**. Gateway path remains **SUPPORTED VIA GATEWAY**.

**Scope:** Commercial + platform gate only. **No** `ProfinetIndustrialAdapter`, **no** `profinet_session`, **no** CMake/PROFINET code, **no** Equipment/IndustrialAdapter/Gazebo/MES/ICP Designer changes.

**Prior docs:** [`profinet-hilscher-evaluation.md`](profinet-hilscher-evaluation.md) (technical feasibility), [`profinet-native-evaluation.md`](profinet-native-evaluation.md), [`profinet-native-implementation-plan.md`](profinet-native-implementation-plan.md), [`kepware-profinet-architecture-research.md`](kepware-profinet-architecture-research.md), ADR-040.

**Smoke test:** **NOT RUN** — no cifX hardware, master license, or NXLFW-PNM package in this environment.

---

## Executive verdict

**Recommendation: C — PROCEED ONLY FOR A SPECIFIC PLATFORM / SKU**

| Platform | Native Hilscher PROFINET |
| --- | --- |
| Windows 10 | **PROCEED** (vendor-supported driver path) |
| Windows 11 | **PROCEED** (driver **verified** by Hilscher) |
| Windows Server | **DO NOT** claim production support |
| Ubuntu 24.04 LTS | **TECHNICALLY POSSIBLE — NOT VENDOR VERIFIED** |
| Docker Linux (native PN inside container) | **DO NOT** as default; prefer host driver + optional host PN agent |
| Docker Desktop (Windows) | **✗ Unsupported** — native PN as Windows service/app only |

**SKU lock for first procurement:** **CIFX 50E-RE** (part **1251.100**) + **NXLIC-MASTER** (**8211.000**) + **NXLFW-PNM** (**7428.840**) + **NXDRV-WIN** / **NXDRV-LINUX**.

**Product principle (unchanged):** Native PROFINET is **optional**. Gateway deployment remains a first-class ICP model with **no** Hilscher hardware.

---

## 1. Exact recommended Hilscher SKU

| Role | Product | Part number | Why |
| --- | --- | --- | --- |
| **Primary hardware** | **CIFX 50E-RE** | **1251.100** | Active PCIe x1 Real-Time Ethernet card; **netX 100**; 2×RJ45; Controller **or** Device by loadable firmware; industrial-PC default form factor; used in Hilscher’s own Windows Server one-off FAQ test |
| **Master license** | **NXLIC-MASTER** | **8211.000** | Required for PROFINET (and other RTE) **master/controller** operation; remanently bound to the card (serial); ordered separately / via Hilscher license request |
| **PROFINET firmware** | **NXLFW-PNM** | **7428.840** | Loadable Firmware **PROFINET IO-Controller** (RT Class 1 path; precertified stack marketed by Hilscher) |
| **Windows driver** | **NXDRV-WIN** | (driver package) | Kernel driver + cifX API DLL; Win10 current line; **verified Win11** |
| **Linux driver** | **NXDRV-LINUX** / GitHub `HilscherAutomation/nxdrvlinux` | (driver/toolkit) | `uio_netx` (GPL) + `libcifx` (MIT) + cifX Toolkit sources |
| **Config tooling** | **SYCON.net** and/or **Communication Studio** + Device Library | (tool SKUs) | Import GSDML; export **`config.nxd`** (+ related DBM) |

**Compact alternate (same stack, different form factor):** **CIFX M.2** Real-Time Ethernet module (e.g. **CIFX M3042100BM-RE\F**, part **1456.101** per prior Hilscher catalog references) — use when industrial PC has **M.2** but no full-height PCIe. Same **NXLIC-MASTER** + **NXLFW-PNM** + same driver/API family. Prefer **50E-RE** for first lab unless chassis dictates M.2.

**Do not accept “cifX” alone.** The product is the **exact card + master license + PNM firmware + driver package**.

---

## 2. netX generation

**netX 100** on **CIFX 50E-RE** (official product table: Communication controller type = netX 100).

Protocol stack runs **on the card** (companion-chip / LFW model). Host exchanges process data via Dual-Port Memory (DPM) or DMA (DMA from hardware revision 4+ on this card).

---

## 3. PROFINET firmware

| Item | Value |
| --- | --- |
| Product | **NXLFW-PNM** — Loadable Firmware PROFINET IO-Controller |
| Part | **7428.840** |
| Role | IO-Controller (not IO-Device) |
| Delivery | Loadable firmware package transferred to netX at startup / via driver config |
| Notes | Hilscher also sells **NXLFW-PNM-IRT** (**7428.650**) for IRT — **out of first ICP slice** (RT Class 1 only) |

---

## 4. SDK / API

| Layer | Product |
| --- | --- |
| Host application API | **cifX API** (C): `cifXDriverInit`, `xChannelOpen`, `xChannelIORead`, `xChannelIOWrite`, mailbox / packet services |
| Protocol services | PROFINET IO-Controller **Protocol API** (packet mailbox) — DCP, AR, diagnosis, etc. |
| Porting / sources | **NXDRV-TKIT** cifX Toolkit (C sources for DPM/API) |
| Language for ICP | C++17 wrapper behind **private** `profinet_session` — **no** Hilscher types in public headers |

---

## 5. Driver

| OS | Package | Components |
| --- | --- | --- |
| Windows 10 / 11 | **NXDRV-WIN** (current Win10+ line; e.g. V2.8.x per Hilscher version pages) | Kernel PnP driver + user API DLL (x86/x64) |
| Linux | **NXDRV-LINUX** / `nxdrvlinux` | Kernel UIO module **`uio_netx`**, user library **`libcifx`**, firmware/config under configured paths (commonly `/opt/cifx` or driver-documented paths) |

---

## 6. Hardware requirements

| Requirement | Detail |
| --- | --- |
| Host slot | **PCI Express x1** (3.3 V) for CIFX 50E-RE |
| Host CPU | **x86 / x64 (AMD64)** — Hilscher Windows driver: **no IA64 / ARM host** |
| Card Ethernet | **2 × RJ45** 10/100 isolated — **these** ports are the PROFINET wire (not the host NIC) |
| Power | ~800 mA @ 3.3 V typical (card) |
| Network | Industrial Ethernet switch / line topology to IO-Devices |
| Permissions (Win) | Admin for driver install; service rights for production ICP |
| Permissions (Linux) | Access to UIO/cifX device nodes; udev rules; FW load paths |

**Ordinary host NICs alone are not sufficient** for this native path.

---

## 7. Windows 10

| Item | Status |
| --- | --- |
| Official driver support | **Yes** — NXDRV-WIN current release line targets Windows 10+ |
| Productive IO-Controller | **Yes** — intended industrial PC deployment path |
| ICP stance | **Supported target** for native PN after smoke test on our builds |

---

## 8. Windows 11

| Item | Status |
| --- | --- |
| Official verification | Hilscher documents NXDRV-WIN as **verified on Windows 11** |
| Productive IO-Controller | **Yes** — same card/FW/API as Win10 |
| ICP stance | **Supported target** after smoke test |

---

## 9. Windows Server

| Item | Finding |
| --- | --- |
| General official support | **No** — Hilscher FAQ: *by default does not test cifX drivers under Windows Server* (Server has special driver-install requirements) |
| Documented one-off | **Windows Server 2019** Standard Desktop Experience, driver **V2.5.1.0**, CIFX 50E-RE + M.2 card, Secure Boot off, PCIe ASPM disabled — basic install/cifXTest/NDIS only |
| Server 2022 / 2025 | **Not** listed as tested |
| Productive PN IO-Controller | **Not vendor-guaranteed** |
| Dev/test only? | Treat any Server use as **unsupported / customer-at-risk** until Hilscher confirms in writing |
| ICP stance | **Do not mark Windows Server as supported** for native PROFINET. Gateway + non-PN ICP Core may still run on Server |

---

## 10. Ubuntu 24.04 LTS

| Item | Finding |
| --- | --- |
| Linux driver existence | **Yes** — NXDRV-LINUX / GitHub `HilscherAutomation/nxdrvlinux` |
| Kernel module | **`uio_netx`** (UIO) |
| User library | **`libcifx`** |
| Ubuntu 24.04 (kernel ~6.8) official matrix | **Not** confirmed as a named supported distro release in materials reviewed for this gate |
| Assessment | **TECHNICALLY POSSIBLE — NOT VENDOR VERIFIED** |
| Production claim | **Do not** call Ubuntu 24.04 production-supported for native PN until Hilscher confirms or ICP runs a formal validation bench |
| Firmware / enumeration | Driver loads LFW + `config.nxd`; device enumeration via UIO/sysfs — must be validated on target kernel |

---

## 11. Docker Linux

**Distinguish carefully:**

| Deployment | Assessment |
| --- | --- |
| **Dockerized ICP Core** (OPC UA, Modbus, MQTT, REST, EIP, gateway PN) | **OK** — platform-independent Core |
| **Dockerized native PROFINET controller** (cifX inside container) | **Not recommended as default** |

Native PN inside Linux Docker requires roughly:

- Host installs **uio_netx** / cifX driver
- Container gets **device nodes** (`--device` / device cgroup) or VFIO PCIe passthrough
- Often **privileged** or carefully capped capabilities
- Firmware/config mounts
- **Host networking** may help coexistence; **cyclic PN still uses cifX RJ45**, not Docker bridge `eth0`
- Lifecycle: container restart ≠ card FW reload semantics — must design explicit connect/reset

**Preferred architecture (supportable):**

```text
Docker ICP Core  ──(IPC/CIC/local API)──►  Host Hilscher PN agent / native ICP PN service
                                                    │
                                                    ▼
                                              NXDRV-LINUX + cifX
                                                    │
                                                    ▼
                                              PROFINET IO-Devices
```

This keeps Core containerizable while native PN stays on the host at the infrastructure boundary.

---

## 12. Docker Desktop Windows

**Native PROFINET through Hilscher cannot reliably operate under Docker Desktop on Windows.**

Reasons: VM isolation, no trustworthy PCIe/cifX plant path, no equivalent to bare-metal NXDRV-WIN on the physical card from inside the Desktop VM for production RT IO.

**Required product statement:**

> Native PROFINET should run as a native Windows ICP service/application, not inside Docker Desktop.

Docker Desktop may still host **ICP Core** talking to gateways or to a **native Windows PN service** on the host — that is not “native PN inside Docker Desktop.”

---

## 13–21. Protocol / topology capabilities (NXLFW-PNM + cifX)

| # | Topic | Status |
| --- | --- | --- |
| 13 | RT Class 1 | **Yes** (RTC cyclic; ms-class send clocks documented for RT) |
| 14 | Cyclic IO | **Yes** — `xChannelIORead` / `xChannelIOWrite` |
| 15 | DCP | **Yes** — Set Name/IP/Signal/Reset, Identify, Get |
| 16 | AR | **Yes** — up to **128** ARs (FW V3 class docs) |
| 17 | Slots / subslots | **Yes** — submodule model (≤2048 submodules globally per FW datasheet class) |
| 18 | Diagnostics | **Yes** — alarms / diagnosis via Protocol API + tools |
| 19 | Multi-device | **Yes** — **one controller → many IO-Devices** (not one adapter per PLC) |
| 20 | Multi-controller | **Yes** — multiple cifX boards / channels → multiple `ProfinetIndustrialAdapter` instances |
| 21 | GSDML / configuration | **Vendor tools** import GSDML → export **`config.nxd`**; ICP maps process-image offsets → `GenericEquipment`. **No** custom GSDML parser in first slice |

---

## 22. Licensing (technical availability ≠ redistribution rights)

| Topic | Public / catalog finding | Redistribution / commercial |
| --- | --- | --- |
| Development | Card + master license + FW + drivers for lab | Purchase through Hilscher / distributors |
| Master / controller | **NXLIC-MASTER 8211.000** required for master FW; serial-bound to hardware | Per **physical card** — not a free host-OS license |
| Runtime | Stack runs on netX after licensed FW load | Runtime tied to licensed hardware |
| Firmware download | Packages obtainable from Hilscher after product relationship | **Do not assume** ICP may redistribute FW binaries in our installer without written permission |
| Driver download | NXDRV-WIN / Linux publicly documented / GitHub for Linux toolkit pieces | Windows driver redistribution in commercial installer needs **Hilscher approval**; Linux `uio_netx` GPL + `libcifx` MIT — still confirm packaging obligations |
| Activation | Master license request / remanent license on card (Hilscher process; historically `license@hilscher.com` / license request flow) | OEM must document customer activation if cards sold separately |
| Support contract | Commercial Hilscher support available | Recommend support contract for production ICP Industrial SKU |
| Certification | FW marketed as precertified stack | **ICP-as-controller product** may still need own PI conformance if claiming certified controller product |

**Separation:** Drivers/API being **downloadable** does **not** equal right to **redistribute** them inside ICP media.

---

## 23. OEM redistribution

| Model | Allowed? |
| --- | --- |
| Sell **ICP Standard** without Hilscher HW (gateway PN only) | **Yes** — no Hilscher dependency |
| Sell **ICP Industrial / PROFINET Edition** that **requires** customer-installed CIFX 50E-RE + NXLIC-MASTER + NXLFW-PNM | **Yes** — commercially coherent; ICP ships software + install guide |
| Bundle / resell Hilscher cards + licenses as kit | **Possible** via Hilscher distributor / OEM agreement — **QUOTE + contract required** |
| Ship Hilscher FW/drivers inside ICP installer without agreement | **Assume NO** until written OEM terms |
| Soft-NIC-only native PN via Hilscher | **No** — hardware mandatory |

---

## 24. Runtime licensing

- **Per-card master license** model (NXLIC-MASTER), not an unlimited soft seat.
- Additional protocol FW packages as required.
- No public evidence of a free unlimited controller runtime for cifX PNM.
- Softing-style soft-NIC OEM runtime remains a **separate** (still blocked) path.

---

## 25. Estimated cost / quote requirement

**QUOTE REQUIRED.**

No authoritative public OEM price list for CIFX 50E-RE + NXLIC-MASTER + NXLFW-PNM + tooling + support was used as a pricing basis for this gate. Distributor list prices vary and are not treated as Hilscher OEM terms.

Procurement should request written quote covering: hardware unit price, master license, firmware package, driver redistribution rights, Communication Studio / SYCON.net seats, support SLA, and any OEM kit terms.

---

## 26. Required test hardware (lab)

1. Industrial PC or workstation with **PCIe x1** (or approved M.2 alternate)
2. **CIFX 50E-RE (1251.100)**
3. **NXLIC-MASTER (8211.000)** activated on that card
4. **NXLFW-PNM (7428.840)** package
5. **NXDRV-WIN** and/or **NXDRV-LINUX** matching OS
6. Managed/unmanaged industrial Ethernet switch (as needed)
7. Cabling to device Ethernet ports (card RJ45 — not host NIC)

---

## 27. Required test IO-Device

At least one real **PROFINET IO-Device** with GSDML, e.g.:

- Compact PN remote IO (any major vendor with public GSDML), **or**
- PN IO-Device development board / Hilscher PN Device demo hardware

Engineer topology in SYCON.net / Communication Studio → export `config.nxd` → load to card → verify cyclic IO + DCP + diagnosis before any ICP adapter coding.

Vendor-approved simulators may supplement but **do not** replace at least one real IO-Device for the smoke gate.

---

## 28. Technical risks

| Risk | Severity |
| --- | --- |
| No hardware in CI — cannot prove IO until lab kit arrives | High (blocks VALIDATED) |
| Windows Server unsupported → some enterprise ICP deployments cannot use native PN on Server | High (product messaging) |
| Ubuntu 24.04 unverified kernel 6.x | Medium |
| Docker Desktop Win false expectations | High if sales claims it |
| GSDML/config.nxd workflow outside ICP Designer initially | Medium (ops friction) |
| Host Windows not hard RT OS (Hilscher notes nondeterministic host call latency) | Medium (mitigated by netX on-card stack) |
| Multi-AR fault isolation bugs if adapter maps poorly | Medium (design discipline) |
| Softing still preferred for soft-NIC SKU — dual path complexity | Medium |

---

## 29. Commercial risks

| Risk | Severity |
| --- | --- |
| Driver/FW redistribution rights unclear without OEM contract | High |
| Per-card BOM cost may limit “PROFINET Edition” price point | Medium–High — **QUOTE REQUIRED** |
| Customers expect soft-NIC PN (Kepware research also did not prove soft-NIC PN IO-Controller) | Medium — mitigate with honest SKU docs |
| Master license logistics (serial, activation) in field | Medium |
| PI conformance cost if claiming certified controller product | Medium |
| Dual Softing+Hilscher strategy if Softing later opens | Medium (keep gateway + optional Softing) |

---

## 30. Final recommendation

### **C — PROCEED ONLY FOR A SPECIFIC PLATFORM / SKU**

**Proceed with Hilscher procurement for:**

- Hardware: **CIFX 50E-RE 1251.100** (+ optional M.2 alternate for compact IPC)
- License: **NXLIC-MASTER 8211.000**
- Firmware: **NXLFW-PNM 7428.840**
- Drivers: **NXDRV-WIN** (primary), **NXDRV-LINUX** (secondary / lab)
- Host OS for **production native PN claims:** **Windows 10** and **Windows 11** industrial PCs only (until Server/Ubuntu are vendor-verified)

**Do not proceed as:**

- Universal native PN for all ICP installs
- Windows Server production-supported native PN
- Ubuntu 24.04 production-supported native PN (label only: **TECHNICALLY POSSIBLE — NOT VENDOR VERIFIED**)
- Native PN inside Docker Desktop Windows
- Default “PN inside Linux Docker” without host agent architecture

**Commercial SKU model (permitted by Hilscher HW+license pattern):**

```text
ICP Standard
  OPC UA, Modbus, MQTT, REST, EtherNet/IP, PROFINET Gateway
  — no Hilscher hardware required

ICP Industrial / PROFINET Edition
  everything in Standard
  + Hilscher cifX hardware (customer or kit)
  + NXLIC-MASTER + NXLFW-PNM
  + native ProfinetIndustrialAdapter (when implemented)
```

Both deployment models remain legitimate.

**Not recommendation A** (blanket proceed) because Server / Ubuntu 24.04 / Docker Desktop fail the product matrix.  
**Not recommendation B** (do not proceed) because Win10/11 + CIFX 50E-RE + PNM is a credible industrial path.

**Coding remains blocked** until explicit user approval **after** this gate + hardware/license in hand + smoke test.

---

## 31. Proposed native PROFINET implementation plan (PLAN ONLY)

*Do not execute until explicitly approved.*

### Phase 0 — Procurement & legal (blocking)

1. Order CIFX 50E-RE + NXLIC-MASTER + NXLFW-PNM + NXDRV-WIN (+ Linux driver for lab).
2. Obtain written OEM terms: driver/FW redistribution, kit resale, support.
3. Quote Industrial SKU BOM.

### Phase 1 — Hardware smoke (no ICP adapter yet)

1. Install NXDRV-WIN on Win10 or Win11 industrial PC.
2. Load NXLFW-PNM + `config.nxd` against one real IO-Device.
3. Prove cyclic IO, DCP name/IP, AR up, diagnosis with Hilscher tools / sample apps.
4. Optionally repeat Linux smoke → document Ubuntu 24.04 as vendor-verified or remain “not verified.”

### Phase 2 — Private session library (isolated)

1. Add **optional** CMake feature `VF_ENABLE_PROFINET_HILSCHER` (off by default).
2. Implement **private** `profinet_session` wrapping cifX API only in `.cc`.
3. Unit/integration tests behind the feature flag with hardware or recorded fixtures — **no** public Hilscher types.

### Phase 3 — `ProfinetIndustrialAdapter`

1. One adapter instance = one cifX channel / PN controller.
2. Map many IO-Devices / submodules → many `GenericEquipment`.
3. Integrate with existing `AdapterManager` / `PollScheduler` / `LiveStateCache` (ICP-1A).
4. Fault isolation: AR failure ≠ unrelated equipment process fault; `ConnectionState::Faulted` ≠ `Equipment::fault()`.

### Phase 4 — Packaging

1. Document ICP Standard vs Industrial.
2. Windows service install path for native PN.
3. Docker: Core container + **host PN agent** pattern; explicitly forbid Docker Desktop native PN.
4. Keep gateway path in all editions.

### Architecture (unchanged unless smoke proves otherwise)

```text
ICP
 │
 ├── AdapterManager / PollScheduler / LiveStateCache
 └── Industrial adapters
       ├── … existing …
       ├── PROFINET Gateway (always)
       └── Native PROFINET
              → ProfinetIndustrialAdapter
              → private profinet_session
              → Hilscher cifX API
              → cifX / netX (NXLFW-PNM)
              → many IO-Devices
```

---

## 32. Decision checklist (quick matrix)

| # | Item | Result |
| --- | --- | --- |
| 1 | Exact SKU | **CIFX 50E-RE 1251.100** (+ NXLIC-MASTER 8211.000) |
| 2 | netX | **netX 100** |
| 3 | Firmware | **NXLFW-PNM 7428.840** |
| 4 | SDK/API | **cifX API** (+ Protocol API / Toolkit) |
| 5 | Driver | **NXDRV-WIN** / **NXDRV-LINUX** |
| 6 | Hardware | PCIe x1, 2×RJ45, x86/x64 host |
| 7 | Windows 10 | **Supported path** |
| 8 | Windows 11 | **Supported path** (verified) |
| 9 | Windows Server | **Not production-supported** |
| 10 | Ubuntu 24.04 | **TECHNICALLY POSSIBLE — NOT VENDOR VERIFIED** |
| 11 | Docker Linux | Host driver + agent preferred; in-container not default |
| 12 | Docker Desktop Win | **✗** — native Windows service only |
| 13–21 | RT1 / cyclic / DCP / AR / slots / diag / multi-dev / multi-ctrl / GSDML | **Capable** (see §13–21) |
| 22–24 | Licensing / OEM / runtime | Master-per-card; OEM contract required; download ≠ redistribute |
| 25 | Cost | **QUOTE REQUIRED** |
| 26–27 | Test HW / IO-Device | Card + license + FW + real PN device |
| 28–29 | Risks | See §28–29 |
| 30 | Recommendation | **C** |
| 31 | Impl plan | §31 — **not started** |
| 32 | Git | See commit on branch `cursor/icp-6h-hilscher-final-gate-a88d` |

---

## References (public)

- Hilscher CIFX 50E-RE product page (part 1251.100, netX 100, NXLIC-MASTER 8211.000)
- Hilscher NXLFW-PNM product page (part 7428.840)
- Hilscher Confluence: NXDRV-WIN features / versions; FAQ “Does the driver run on Windows Server?”
- GitHub: `HilscherAutomation/nxdrvlinux`
- Prior ICP eval: `docs/profinet-hilscher-evaluation.md`

**No proprietary Hilscher SDK binaries or confidential EULAs are stored in this repository.**
