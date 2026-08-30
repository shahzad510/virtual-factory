# Kepware PROFINET architecture research (for ICP)

**Status:** Research **COMPLETE** (2026-08-29). **No production code.** Native PROFINET remains **NOT IMPLEMENTED**. Gateway remains **SUPPORTED**.

**Purpose:** Before locking ICP to Hilscher cifX hardware, determine how Kepware/PTC provides PROFINET-related connectivity — and what that implies for ICP.

**Evidence tiers used throughout:**

| Tag | Meaning |
| --- | --- |
| **CONFIRMED** | From official PTC/Kepware documentation retrieved in this research |
| **INFERRED** | Reasonable reading of official docs, not explicitly stated |
| **THIRD-PARTY** | Partner/blog/distributor claims — not treated as authority |
| **UNKNOWN** | Not publicly established here |

---

## 1. Kepware PROFINET product / driver

### What official PTC documentation shows

**CONFIRMED — Driver Options for KEPServerEX (PTC datasheet J7845, retrieved copy dated ~2018 via ATS Global mirror):**

- Lists **Hilscher Universal** among drivers.
- Does **not** list a driver named **“PROFINET Ethernet”**, **“PROFINET IO Controller”**, or similar in that official list.
- Drivers are licensed individually or in suites; additional drivers can be licensed on demand.

**CONFIRMED — Hilscher Universal Driver Help (Kepware driver manual, distributor-hosted official help PDF):**

- Product/driver name: **Hilscher Universal Driver**.
- Intended for **Hilscher Communications Interface (CIF) cards**.
- Supported protocols on those cards: **DeviceNet Master/Slave** and **PROFIBUS DP Master/Slave**.
- Requires **SyCon** (Hilscher configuration software) on the same machine as the OPC server.
- **Does not claim PROFINET** in that manual.

**CONFIRMED — Kepware Edge (official PTC product page + help, 2025–2026):**

- Linux/container connectivity product.
- Documented device protocols include: EtherNet/IP, Modbus Ethernet, Siemens Industrial Ethernet, Siemens S7 Plus, OPC UA Client, Mitsubishi Ethernet.
- **PROFINET is not listed** among Kepware Edge protocols or supported drivers.
- Individual drivers are **not** sold separately for Edge — platform bundle only.

**CONFIRMED — Kepware Server system requirements (KEPServerEX / Kepware Server manual, PTC, retrieved 2025):**

- Supported host OS includes **Windows 10/11**, **Windows Server 2016/2019/2022/2025** (x64).
- **Windows Server Core deployments are not supported** (manual note).
- This confirms Kepware **Server** runs on Windows Server; it does **not** by itself confirm a PROFINET IO-Controller driver exists or works on Server without special hardware.

**CONFIRMED — Siemens connectivity (official PTC Help / product materials):**

- **Siemens TCP/IP Ethernet Driver** and **Siemens S7 Plus Ethernet Driver** connect to S7 PLCs using **S7 messaging over TCP/IP** (and related Siemens Ethernet options).
- Official overview text for Siemens TCP/IP Ethernet: *“The driver requires no special libraries or hardware. A standard Ethernet card is all that is needed.”*
- That is **S7 protocol access**, **not** a statement that Kepware is a PROFINET IO-Controller.

### THIRD-PARTY claims (not elevated to CONFIRMED)

Partner/blog articles (e.g. TTPSC “Kepware Drivers” list) describe a **“PROFINET IO Controller”** driver in Manufacturing Suite that imports GSDML and reads cyclic data.  

**This research could not corroborate that driver in the official J7845 driver-options PDF retrieved here, nor in Kepware Edge official protocol lists.** Treat as **unverified** until PTC publishes an official PROFINET Ethernet / IO Controller driver help center topic or an updated J7845 listing that explicitly names it.

**kepware.com** path `/device-drivers/profinet-ethernet/` exists as a URL pattern but returned marketing shell content without a technical driver manual in this environment (not usable as CONFIRMED capability evidence).

---

## 2. PROFINET role (what Kepware actually does)

| Path | Role | Evidence |
| --- | --- | --- |
| Siemens TCP/IP / S7 Plus | Client to PLC over **S7/TCP** (or related Siemens Ethernet), **not** PN IO-Controller for arbitrary IO-Devices | **CONFIRMED** official driver overviews |
| Hilscher Universal | Host app over **CIF card** acting as DeviceNet / **PROFIBUS DP** master/slave | **CONFIRMED** Hilscher Universal manual |
| Dedicated soft-NIC PROFINET IO-Controller driver | Claims exist in blogs | **THIRD-PARTY / UNKNOWN** vs official lists |
| Gateway / multiprotocol devices | Common plant pattern: device exposes Modbus/EIP/OPC UA; Kepware uses those drivers | **INFERRED** from Edge/Windows driver set + community practice |

**Topology that is CONFIRMED for Siemens PLCs:**

```text
Siemens PLC (may also have PROFINET ports for its own IO)
        │  S7 messaging / Industrial Ethernet TCP
        ▼
Kepware Siemens TCP/IP or S7 Plus driver
        │
        ▼
OPC UA / OPC DA / MQTT / IoT Gateway clients
```

**Topology that is CONFIRMED for Hilscher Universal (legacy fieldbus):**

```text
PROFIBUS / DeviceNet slaves
        │
Hilscher CIF card (master firmware)
        │
Kepware Hilscher Universal Driver
        │
OPC clients
```

**Not CONFIRMED:** Kepware as soft-NIC **PROFINET IO-Controller** managing GSDML IO-Devices on Layer-2 RT.

---

## 3. Special hardware?

| Scenario | Special hardware? | Evidence |
| --- | --- | --- |
| Siemens S7 via Kepware Siemens drivers | **No** — standard Ethernet NIC | **CONFIRMED** |
| DeviceNet / PROFIBUS via Hilscher Universal | **Yes** — Hilscher **CIF** card + SyCon | **CONFIRMED** |
| Native PROFINET IO-Controller soft stack in Kepware | **UNKNOWN** officially | Official list gap |
| Kepware Edge Linux/container | Standard host networking for listed TCP/Ethernet drivers; **no PROFINET listed** | **CONFIRMED** product page |

**Most important finding for ICP:**  
Where Kepware historically offered a **fieldbus master** (PROFIBUS/DeviceNet), it used **dedicated Hilscher CIF hardware** — not a claim of “ordinary NIC is enough for master RT.”  
Where Kepware connects to Siemens PLCs without special cards, it uses **application protocols over TCP**, not necessarily PROFINET cyclic IO-Controller semantics.

---

## 4. Software stack (as far as public docs allow)

```text
Kepware Server (Windows service / config UI)
        ↓
Protocol driver (Siemens TCP/IP, Modbus, EIP, Hilscher Universal, …)
        ↓
[ driver-specific ]
        ↓
OS networking OR Hilscher CIF driver/SyCon
        ↓
Plant network / fieldbus
```

| Middle layer | Status |
| --- | --- |
| Third-party Softing stack inside Kepware PN driver | **UNKNOWN** — no official disclosure found |
| Siemens stack inside Kepware | **UNKNOWN** for PN; Siemens drivers are S7/TCP oriented |
| Hilscher CIF + SyCon for Universal driver | **CONFIRMED** |
| Proprietary PTC implementation details | **UNKNOWN** (not public) |
| Npcap/raw Ethernet for a soft PN controller | **UNKNOWN** for Kepware specifically |

Do **not** assert Softing/Hilscher/Siemens as Kepware’s internal PROFINET stack without vendor disclosure.

---

## 5. Cyclic process data / DCP / AR / slots / diagnostics / GSDML

| Topic | Official Kepware PN IO-Controller | Official Siemens drivers | Official Hilscher Universal |
| --- | --- | --- | --- |
| RT Class 1 cyclic PN IO | **UNKNOWN** (driver not confirmed in official list) | N/A (S7/TCP model) | PROFIBUS/DeviceNet cyclic I/O via CIF — **CONFIRMED** I/O tags |
| DCP / station naming | **UNKNOWN** | N/A as PN controller | Config via SyCon — **CONFIRMED** tooling dependency |
| AR / slots / GSDML | Blog claims GSDML — **THIRD-PARTY** | TIA/STEP7 tag import for **S7 tags** — **CONFIRMED** (different problem) | SyCon DB → auto tags — **CONFIRMED** |

**Conceptual data path (CONFIRMED Kepware pattern generally):**

```text
Device/protocol data
  → driver
  → Kepware tag address space
  → OPC UA/DA / MQTT / other northbound interfaces
```

**ICP analogue (already designed):**

```text
PROFINET process image (if native)
  → ProfinetIndustrialAdapter
  → GenericEquipment
  → LiveStateCache
  → CIC
  → MES / SCADA / third parties
```

---

## 6–7. Multi-device / multi-network

**CONFIRMED (Kepware platform model generally):** channel + devices tree; many devices under channels; NIC selection common for Ethernet drivers.

**CONFIRMED (Hilscher Universal):** one channel maps to a **board** in SyCon; I/O from that master’s configuration database.

**UNKNOWN:** device-count limits for any official soft-NIC PROFINET IO-Controller driver (because that driver is not CONFIRMED here).

ICP proposed model (one adapter = one IO-Controller = many IO-Devices) remains architecturally sound for **true** PN controllers (Hilscher NXLFW-PNM / Softing Controller Stack), independent of Kepware.

---

## 8–10. Windows / Linux / Docker

| Platform | Kepware position | Evidence |
| --- | --- | --- |
| Windows 10/11/Server | **CONFIRMED** primary footprint for Kepware Server / KEPServerEX (system requirements in server manuals) | Official manuals |
| Linux | **CONFIRMED** via **Kepware Edge** container product — **limited driver set**, **no PROFINET listed** | Official Edge page |
| Docker / containers | **CONFIRMED** for Edge (OCI/Docker-class runtimes) for **listed** drivers | Official Edge page |
| Docker Desktop + native PN L2 | **UNKNOWN** for Kepware; generally **poor** for real PN controllers industry-wide | Industry PN L2 constraints |

**Lesson:** Kepware’s container strategy prioritizes **TCP/Ethernet application protocols** that containerize cleanly — not Layer-2 PROFINET controllers.

---

## 11–13. Failure / security / licensing

| Topic | Finding |
| --- | --- |
| Failure model | Kepware exposes device/comms quality via server diagnostics/tags — details vary by driver (**PARTIAL / UNKNOWN** for PN). Conceptual separation of bad quality vs process alarms exists in OPC ecosystems generally (**INFERRED**). |
| Security | Server certificate/OPC UA security features exist at platform level (**CONFIRMED** for modern Kepware Server releases). PROFINET Security Class features for a PN controller driver: **UNKNOWN**. |
| Licensing | Drivers licensed individually or in suites; Edge is subscription platform with drivers included (**CONFIRMED**). Exact PN driver SKU/pricing: **UNKNOWN** (and driver itself unverified officially). |

---

## 14. Comparison table

| Area | Kepware (official evidence) | ICP current design | Recommendation |
| --- | --- | --- | --- |
| PROFINET role | S7/TCP & other app protocols; Hilscher CIF for Profibus/DN; soft PN IO-Controller **unconfirmed** | Native IO-Controller + gateway | Keep **both** gateway and native; don’t assume soft-NIC from Kepware |
| Controller architecture | Often **not** the PN controller for Siemens data | Explicit PN controller option | Native only where required |
| Special hardware | **Yes** for Hilscher Universal masters; **No** for Siemens TCP drivers | Hilscher path needs cifX | Honest SKU: hardware optional vs software Softing |
| Software stack | Opaque; Hilscher/SyCon for Universal | Softing candidate / Hilscher candidate | Prefer documented stacks we can license |
| Cyclic IO | Confirmed for CIF I/O; PN soft cyclic **unknown** | Required for native | Need Softing or Hilscher proof |
| DCP / GSDML / slots | Unknown for soft PN; SyCon for CIF | Designer+GSDML planned | Vendor tooling first |
| Multi-device | Channel/device model | One controller → many devices | Keep ICP model |
| Windows | Strong | Required | Align |
| Linux | Edge without PROFINET listed | Ubuntu required | Native PN may be host-only / optional SKU |
| Docker | Edge for TCP drivers | Core ✓; native PN ? | Gateway in containers; native on host |
| GUI | Kepware Configuration | ICP Designer (future) | Same drag/configure/deploy vision |
| Licensing | Driver suites / subscription | ICP product license + vendor OEM | Separate ICP SKU for native PN |

---

## 15. Options A–D vs Kepware appearance

| Option | Description | Closest Kepware analogue |
| --- | --- | --- |
| **A** Hilscher cifX native | Hardware PN/fieldbus master | **Hilscher Universal** pattern for Profibus/DN (**CONFIRMED** hardware master style) |
| **B** Commercial soft PN stack + std NIC | Softing-style Controller Stack | **Not CONFIRMED** as Kepware’s published PN approach |
| **C** Vendor stack + vendor NIC/driver | Hybrid | Possible but undocumented for Kepware PN |
| **D** Gateway | PN → OPC UA/Modbus/EIP → connectivity server | **Strongly matches** how plants often use Kepware with multiprotocol devices / S7 TCP (**CONFIRMED** practical pattern) |

**Kepware does not publicly prove Option B.** It **does** prove Option D strongly and Option A historically for non-Ethernet fieldbus masters.

---

## 16. Architectural lessons (do not blindly copy)

1. **Connectivity products often avoid being the PROFINET IO-Controller** when PLC data is available via S7/EIP/Modbus/OPC UA.  
2. **When they are fieldbus masters**, dedicated cards (Hilscher CIF) are a known commercial pattern.  
3. **Container/Linux offerings drop hard protocols first** (Edge has no PROFINET listed).  
4. **Northbound tag/OPC model** maps cleanly to ICP’s GenericEquipment → CIC idea.  
5. **Product modularity** (drivers as SKUs) matches ICP’s modular protocol adapters.

---

## 17–18. Recommendation for commercial ICP

### Final recommendation: **C — SUPPORT BOTH**

**Software-only commercial PROFINET stack (Softing primary when procured)**  
**+**  
**Hilscher cifX hardware option**  
**+**  
**Gateway path always SUPPORTED**

| Choice | Why |
| --- | --- |
| **Not A alone** | Locking ICP to Hilscher-only raises customer hardware cost and hurts Docker/Linux packaging; Kepware Edge shows container products favor TCP protocols |
| **Not B alone** | Softing SDK gate **FAILED** here; Windows soft-NIC RT remains hard; Kepware did **not** publicly validate B for us |
| **Not D alone** | Conflicts with ICP product goal of **direct** industrial connectivity where feasible |
| **C** | Matches market reality: SKU matrix by deployment constraint; preserves Softing software path and Hilscher industrial-PC path; keeps gateway |

### Product model

| SKU / profile | Native PN | Notes |
| --- | --- | --- |
| ICP Core | Gateway PN only | Windows/Linux/Docker |
| ICP Native PN — Software | Softing (when licensed) | Std NIC + privileges; validate Win/Ubuntu |
| ICP Native PN — Hardware | Hilscher cifX + NXLFW-PNM + master license | Best Win industrial PC RT |
| MES Core | Never embeds PN | CIC only |

### ICP Designer impact

Designer must eventually offer **protocol choice**: Gateway vs Softing vs Hilscher, with honest capability badges (Docker/Linux/Windows). Same drag/drop configure/deploy UX; different property panels.

### Windows / Linux / Docker strategy impact

- Do **not** promise native PN in Docker Desktop.  
- Prefer host/service install for native PN.  
- Keep Edge-like container story for OPC UA/Modbus/EIP/REST/MQTT + gateway PN.

---

## 19. Impact on current Hilscher plan

| Action | Guidance |
| --- | --- |
| Implement Hilscher **now**? | **No** — wait for explicit approval of dual-SKU strategy and/or Softing procurement outcome |
| Abandon Hilscher? | **No** — still the strongest **hardware** path with public Protocol API evidence |
| Abandon Softing? | **No** — still the strongest **software** candidate; SDK gate remains the blocker |
| Change ADR-040? | Amend to record: Kepware research does **not** prove soft-NIC PN; dual Softing+Hilscher+gateway strategy preferred |
| Fake Softing/Hilscher? | **Forbidden** |

---

## 20. Sources (primary)

1. PTC **Driver Options for KEPServerEX**, document **J7845** (retrieved ATS Global PDF mirror, ©2017/2018 listing) — driver catalog including Hilscher Universal; **no PROFINET driver name**.  
2. Kepware **Hilscher Universal Driver Help** PDF (Logic Control / Software Toolbox mirrors of Kepware help) — CIF + DeviceNet/PROFIBUS + SyCon.  
3. PTC Help — **Siemens TCP/IP Ethernet Driver Overview** — standard Ethernet NIC; S7 messaging.  
4. PTC Help — **Siemens S7 Plus Ethernet Driver** manual (Software Toolbox mirror, ©2025 PTC) — standard Ethernet NIC; symbolic S7 access; explicitly **not** PROFINET IO-Controller.  
5. PTC **Kepware Edge** product page + Edge help (Welcome, What's New 1.0/1.1) — Linux/container drivers; **no PROFINET**.  
6. PTC **Kepware Server / KEPServerEX** manual — Windows 10/11 + Windows Server 2016–2025 system requirements; Server Core not supported.  
7. PTC **Manufacturing Suite** store page — 100+ drivers; **does not enumerate PROFINET** on the public page body.  
8. Prior ICP docs: `profinet-native-evaluation.md`, `profinet-hilscher-evaluation.md`, `profinet-hilscher-final-gate.md`, Softing Controller Stack public datasheets (for Option B technical existence outside Kepware).

**THIRD-PARTY (not authoritative for claims):** TTPSC Kepware driver blog (lists **PROFINET IO Controller** in Manufacturing Suite with GSDML/RT claims); Allied Solutions driver list articles; Software Toolbox educational article on PROFIBUS/PROFINET vs S7 drivers.

---

## 21. Final output checklist (items 1–32)

| # | Topic | Summary |
| --- | --- | --- |
| 1 | Kepware PROFINET product/driver | **CONFIRMED:** Hilscher Universal (CIF, Profibus/DN). **THIRD-PARTY/UNKNOWN:** “PROFINET IO Controller” in Manufacturing Suite — not in J7845 retrieved here |
| 2 | PROFINET role | **CONFIRMED:** S7 client / gateway patterns; **not** confirmed soft-NIC PN IO-Controller in official catalogs |
| 3 | Hardware | **CONFIRMED:** std NIC for Siemens S7; **CONFIRMED:** Hilscher CIF for Universal master |
| 4 | Software stack | Driver → OS NIC or CIF+SyCon; internal PN stack **UNKNOWN** |
| 5 | Controller architecture | Often **not** the PN controller for Siemens data; fieldbus master = hardware card pattern |
| 6 | Cyclic IO | CIF I/O tags **CONFIRMED**; soft PN cyclic **UNKNOWN** |
| 7 | DCP | SyCon for CIF **CONFIRMED**; soft PN **UNKNOWN** |
| 8 | AR | **UNKNOWN** for soft PN |
| 9 | Slots/subslots | SyCon DB **CONFIRMED** for CIF; GSDML claims **THIRD-PARTY** only |
| 10 | Diagnostics | Platform diagnostics **CONFIRMED**; PN-specific **UNKNOWN** |
| 11 | GSDML | Blog claims import **THIRD-PARTY**; not confirmed in official driver help retrieved |
| 12 | Device configuration | Channel/device tree + driver-specific tools **CONFIRMED** (platform) |
| 13 | Multi-device | Channel/device model **CONFIRMED**; PN IO-Device limits **UNKNOWN** |
| 14 | Multi-network | NIC per channel common **INFERRED** |
| 15 | Windows 10 | **CONFIRMED** (Kepware Server) |
| 16 | Windows 11 | **CONFIRMED** (Kepware Server manual) |
| 17 | Windows Server | **CONFIRMED** for Kepware Server host OS; PN driver on Server **UNKNOWN** |
| 18 | Linux | **CONFIRMED** Kepware Edge — **no PROFINET listed** |
| 19 | Docker | **CONFIRMED** Edge container for listed TCP drivers; native PN **UNKNOWN** / likely unsupported |
| 20 | Licensing | Per-driver or suite; Edge subscription bundle **CONFIRMED**; PN SKU **UNKNOWN** |
| 21 | Commercial model | À la carte drivers + suites; Manufacturing Suite subscription **CONFIRMED** |
| 22 | Comparison with ICP | See §14 table |
| 23 | Architectural lessons | See §16 |
| 24 | Hilscher comparison | Kepware uses Hilscher CIF for **Profibus/DN**, not documented for PN IO-Controller |
| 25 | Softing comparison | No evidence Kepware embeds Softing; Option B remains separate ICP path |
| 26 | Recommended native PN architecture | **Option C:** Softing + Hilscher + gateway |
| 27 | Recommended product model | ICP Standard (gateway) + optional native SKUs |
| 28 | ICP Designer impact | Protocol choice badges; GSDML via vendor tooling first |
| 29 | Win/Linux/Docker strategy | Gateway everywhere; native PN host/service; no Docker Desktop PN |
| 30 | Final recommendation | **C — SUPPORT BOTH** (+ gateway always) |
| 31 | Evidence references | §20 |
| 32 | Git | Branch `cursor/kepware-profinet-research-a88d`; see commit after doc update |

---

## Status labels

| Item | Status |
| --- | --- |
| This research | **COMPLETE** |
| Native PROFINET code | **NOT IMPLEMENTED** |
| Gateway | **SUPPORTED VIA GATEWAY** |
| Softing | PRIMARY software candidate — SDK gate **FAILED** |
| Hilscher | Hardware alternate — **FEASIBLE**, not auto-selected for coding |
| Recommended architecture | **Dual Softing + Hilscher + Gateway (Option C)** — awaiting user decision |
