# Native PROFINET evaluation (6H reopen) — ICP product

**Status:** Investigation **COMPLETE** (2026-08-29). Native IO-Controller **APPROVED FOR IMPLEMENTATION**.

**Decision (2026-08-30):** **Hilscher cifX is the primary native PROFINET path for ICP.** Softing is **not** being implemented at this time. Softing remains a **future alternative** if OEM availability, Windows 10/11, Ubuntu 24.04, RT quality, redistribution terms, and a current SDK are all confirmed. Do not add Softing dependencies to the Hilscher branch.

**Authority:** ADR-040 (amended 2026-08-29 and 2026-08-30), ADR-042–045.  
**Gateway path:** remains **SUPPORTED** — see [`profinet-gateway-integration.md`](profinet-gateway-integration.md). Do **not** remove or demote gateway integration.

**Scope of this document:** historical stack research. Implementation status lives in [`native-fieldbus-implementation-status.md`](native-fieldbus-implementation-status.md).

---

## 1. Why native PROFINET is being reconsidered

The 2026-08-28 ADR-040 decision correctly concluded that **Phase 6** could be completed with **gateway-supported** PROFINET, because the MES requirement is protocol-independent `GenericEquipment` — not a native fieldbus stack inside MES.

That decision remains valid for **MES**.

The product requirement has since changed for **ICP** (ADR-042):

> ICP must be a serious standalone Industrial Connectivity Platform capable of **directly** communicating with industrial devices where technically and commercially feasible.

Therefore native PROFINET is strategically desirable **inside ICP**, while gateway integration remains a first-class coexistence path.

```text
Native path (desired):
  PROFINET IO-Device → Native PROFINET IO-Controller (ICP) → GenericEquipment → CIC → MES / SCADA / third parties

Gateway path (retained):
  PROFINET IO-Device → Gateway → OPC UA|Modbus|REST|MQTT → ICP adapters → GenericEquipment → CIC → …
```

MES must never learn whether the source was native PROFINET, gateway-backed PROFINET, or any other protocol.

---

## 2. Repository baseline (audit 2026-08-29)

| Item | Value |
| --- | --- |
| Branch at audit start | `master` @ `0dffad96fdbe70c2b2763be44874906e55b2bac6` |
| ICP-1A | **IMPLEMENTED / TESTED** (`icp/`: AdapterManager, PollScheduler, LiveStateCache, AdapterFactory) |
| Phase 6 | **COMPLETE** (6A–6G + 6H gateway) |
| `IndustrialAdapter` | Sufficient for future native PN (`connect`/`disconnect`/`poll`/`equipment`/`connectionState`) |
| Protected interfaces | Unchanged by this investigation: `Equipment.hh`, `IndustrialAdapter.hh`, Phase 6 adapters, Gazebo, MES |
| Role required | **IO-Controller** only (not IO-Device) |

---

## 3. Technical requirements (IO-Controller)

If implemented, ICP acts as a **PROFINET IO-Controller** talking to **PROFINET IO-Devices**.

| Requirement | First native slice (proposed) | Later / out of first slice |
| --- | --- | --- |
| IO-Controller role | **Required** | — |
| RT Class 1 cyclic process data | **Required** | — |
| DCP / station naming | **Required** | — |
| AR establishment | **Required** | — |
| Slots / subslots / process-data mapping | **Required** | — |
| Basic diagnostics / alarms | **Basic** | Advanced alarms |
| GSDML-driven configuration | **Required** (tooling or import) | Full Designer UX (ICP-1F) |
| Multiple IO-Devices per controller | **Required** | Scale benchmark |
| IRT / RT Class 3 | **Not** first slice | Optional |
| MRP / redundancy / PROFIsafe / PROFIdrive / PROFIenergy | **Not** first slice | Optional |
| Fake TCP/UDP “PROFINET” | **Forbidden** | — |
| p-net used as controller | **Forbidden** | — |

Cyclic process data must come from the stack’s real cyclic IO image — **not** from faking cycle with periodic TCP polls.

---

## 4. Candidate comparison (summary)

| Candidate | Role | C/C++ | Linux | Windows | Docker | Commercial redistributable? | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **Softing PROFINET Controller Stack** | **IO-Controller** (CC-A/B) | Yes (C / PNAK) | Yes (portable + Linux ports) | Yes (dedicated Win product; CCA SKU documented) | Difficult (L2 / NIC) | Commercial OEM — **yes, with license** | **PRIMARY RECOMMENDATION** |
| **Hilscher cifX + PN Controller firmware** | **IO-Controller** (on netX) | Yes (cifX C API) | Yes | Yes (production-grade) | Needs PCIe/device passthrough | Commercial hardware + firmware licenses | **STRONG ALTERNATE** (esp. Windows) |
| **Siemens PROFINET Driver** | **IO-Controller** (+ Device) | Yes (C IO-Base style APIs) | Yes (Debian sample; RT patch recommended) | Std NIC = **demo / not productive** (Npcap); quantity ~16 devices | Difficult | Dev + **runtime** licenses (unit or product-line); free on SIMATIC HW | **Viable Linux OEM path**; Windows soft-NIC weak |
| **PI PROFINET Community Stack** | Core Controller+Device toolkit; **not** turnkey SDK | Mostly C | Linux HAL / device demos | Not a turnkey Win product | Toolkit only | PI membership + CS license; object-code in products; no copyleft combo | **Not first choice** — high integration cost |
| **RTA toolkit on PI CS** | Device + Controller kits (commercial wrapper) | Yes | Claimed | Claimed | Unknown | Commercial | Optional accelerator if PI CS chosen |
| **RT-Labs p-net** | **IO-Device only** | C | Yes (raw Eth) | Limited | Possible as **device sim** | GPL-3 eval / commercial for device | **REJECTED as controller** |
| **profinet-py** | Controller (Python) | No | AF_PACKET | Npcap | Poor | GPL-3 + commercial | **REJECTED** for C++ ICP |
| **SYBERA PN Master / PnioVerify** | Windows master / simulator | Library (Win) | No | Yes | No | Commercial | Niche Win option / test tool |
| **Fake TCP/UDP** | — | — | — | — | — | — | **REJECTED** |

Detailed criterion matrix: §5–§12 below and ADR-040 amendment tables.

---

## 5. IO-Controller capability (A–J)

| Criterion | Softing | Hilscher cifX | Siemens PN Driver | PI CS | p-net |
| --- | --- | --- | --- | --- | --- |
| A. Actually IO-Controller? | **Yes** | **Yes** (firmware) | **Yes** | Core yes; app kit incomplete | **No** |
| B. PROFINET IO? | Yes | Yes | Yes | Yes | Device only |
| C. RT Class 1? | Yes (CC-A/B) | Yes | Yes | Yes (with HAL) | N/A as controller |
| D. Cyclic process data? | Yes | Yes | Yes | Yes (with integration) | Device cyclic |
| E. DCP? | Yes (w/ configurator) | Yes | Yes | Yes | Device side |
| F. Device naming? | Yes | Yes | Yes | Yes | Device side |
| G. AR / controller relationships? | Yes (≤255 connections documented) | Yes | Yes | Yes | No |
| H. Slots/subslots? | Yes | Yes | Yes | Yes | Device |
| I. Alarms/diagnostics? | Yes (stack-dependent depth) | Yes | Yes | Yes | Device |
| J. GSDML / device config? | Communication Configurator | SYCON.net / Communication Studio | Engineering + PN config | External / vendor kits | GSD for device |

---

## 6–9. Platform matrix (O–V, Docker, Ubuntu)

| Target | Softing software stack | Siemens PN Driver | Hilscher cifX |
| --- | --- | --- | --- |
| **Ubuntu 24.04 native** | **Expected feasible** after porting-layer validation with Softing (Linux listed; confirm 24.04 formally in procurement) | **Possible** but samples target Debian + RT patch; Ubuntu must be validated | **Yes** (cifX Linux driver + firmware) |
| **Windows 10 / 11 / Server** | **Yes** (Windows SKU exists; CCA documented for Win product) | Std NIC: **not approved for productive RT** (Npcap demo; ~16 devices; RT ≥ ~32 ms) | **Yes — preferred for production Windows** |
| **Linux Docker** | **Conditional**: privileged/`CAP_NET_RAW`, host NIC or macvlan, dedicated interface; RT/jitter degraded; **not** default ICP packaging | Same L2 constraints | **Conditional**: PCIe/USB passthrough to netX hardware |
| **Docker Desktop on Windows** | **Effectively no** for real plant L2 PROFINET | **No** | **Practically no** without nested virtualization + hardware passthrough |
| PREEMPT_RT | Optional for RT1; recommended for low jitter / IRT | Recommended for Linux soft-NIC | Less critical (ASIC offload) |
| Special drivers | Porting layer → raw Ethernet / NIC | EDDS/AF_PACKET or CP1625/PNDevDriver | cifX kernel/user drivers |

**Deployment honesty for ICP packaging:**

| Component | Windows | Ubuntu 24.04 | Docker Linux | Docker Desktop Win |
| --- | --- | --- | --- | --- |
| ICP Core (OPC UA/Modbus/MQTT/REST/EIP + runtime) | ✓ | ✓ | ✓ | ✓ |
| PROFINET **gateway** path | ✓ | ✓ | ✓ | ✓ |
| Native PROFINET (software Softing) | ✓ (validate) | ✓ (validate) | ? / advanced | ✗ |
| Native PROFINET (Hilscher) | ✓ | ✓ | ? (passthrough) | ✗ / rare |

---

## 10. Licensing / commercial suitability (W–AA)

| Stack | License model | Redistribute in commercial ICP? | Royalties / runtime | Source? | Vendor support? |
| --- | --- | --- | --- | --- | --- |
| Softing Controller Stack | Commercial OEM | **Yes, under Softing contract** | Expect developer + runtime/OEM fees (confirm quote) | Typically binary + porting layer / samples (confirm) | Yes |
| Hilscher | Hardware purchase + firmware/protocol licenses + drivers | **Yes** (ship with cifX or require customer card) | Per-card / firmware model | Toolkit C sources for DPM; protocol on ASIC | Yes |
| Siemens PN Driver | Development license + **runtime** (unit stickers or product-line); **no** extra runtime on SIMATIC HW | **Yes** under Siemens terms | Yes on non-SIMATIC / std Ethernet | Source available to licensees | Yes |
| PI Community Stack | PI membership + CS license; royalty-free object-code in products | **Yes** if membership + license; **cannot** combine with copyleft that forces source disclosure of CS | No PI royalty stated; membership cost | Source to members | Community / best-effort; commercial partners (e.g. RTA) |
| p-net public | GPL-3 | **No** for proprietary ICP without commercial RT-Labs license — and still **wrong role** | Commercial deployment licenses exist for **device** | Yes (commercial) | Yes for device |
| profinet-py | GPL-3 | **No** without commercial | Commercial option exists | Python | Limited |

**Conclusion:** There is **no** open-source C/C++ IO-Controller suitable for commercial ICP redistribution comparable to open62541/libmodbus. Native PROFINET **requires a commercial (or PI-member toolkit) path**.

---

## 11. Hardware / NIC (R–T, AC)

- Dedicated (or carefully shared) **100 Mbit full-duplex switched** Ethernet segment for PROFINET.
- **Loopback is not PROFINET.**
- Software stacks: **Layer-2** access (`AF_PACKET` / CAP_NET_RAW on Linux; NDIS/Npcap or vendor porting layer on Windows).
- Hilscher: **cifX PCIe/M.2/etc.** card — protocol timing on netX.
- Siemens production soft path on Windows is weak; CP1625 / SIMATIC HW for serious Siemens deployments.
- IRT / isochronous: expect specialized NIC/ASIC — **out of first slice**.

---

## 12. Testing / simulation (AD–AF)

| Approach | Viable? | Notes |
| --- | --- | --- |
| Real PROFINET IO-Device hardware | **Preferred** | Buy ≥1 certified device + managed switch |
| Softing Communication Configurator + stack samples | **Yes** | Config + commissioning against live devices |
| Siemens / vendor virtual IO-Device | **Emerging** | PI CS plans virtual IO-Device container (Linux); useful as **device under test**, not as our controller |
| p-net as **IO-Device simulator** | **Yes** (secondary) | Only as peer device for our controller; never claim p-net is our controller |
| SYBERA PnioVerify | Windows tooling | Master simulator / diagnostics — not our product stack |
| Fake TCP/UDP server labeled PROFINET | **Forbidden** | |

Future tests (when implementation approved): controller start, DCP/name/IP, AR up, cyclic IO in/out, mapping → GenericEquipment, diagnostics, disconnect/fault/reconnect policy, multi-device isolation, bad GSDML/config, process-image bounds.

**1,200-device benchmark:** do **not** run now. Native scale is **devices per IO-Controller** + **controllers per ICP**, separate from Modbus/OPC UA endpoint counts.

---

## 13. Recommended stack

### Primary recommendation

**Softing PROFINET Controller Stack** as the default software IO-Controller for ICP:

- Real **IO-Controller** with CC-A/B, cyclic IO, multi-device (documented ≤255 field devices / connections).
- **C** API (PNAK / Simple Controller Application Interface).
- Documented **Linux** and **Windows** product lines.
- GSDML/engineering via Softing **Communication Configurator**.
- Fits `ProfinetIndustrialAdapter` behind `IndustrialAdapter` without leaking PN types into Equipment/CIC/MES.

### Strong alternate (especially Windows production)

**Hilscher cifX + PROFINET Controller firmware** when customers need production-grade Windows RT or hardware offload.

### Secondary / situational

**Siemens PROFINET Driver** for Linux-centric OEM (especially with SIMATIC/CP1625) where Siemens licensing and ecosystem are preferred.

### Rejected for controller role

p-net, profinet-py, fake sockets, “Ethernet packets = PROFINET.”

### PI Community Stack

Credible long-term **core**, but **not** a drop-in SDK. Prefer Softing/Hilscher unless PI membership + large in-house porting investment is deliberately chosen.

---

## 14. Recommended implementation scope (first native slice — **not started**)

Only after: (1) commercial Softing **or** Hilscher license in hand, (2) explicit user approval of implementation plan.

**In scope (proposed):**

- `ProfinetIndustrialAdapter` in ICP industrial layer (protocol-oriented).
- One adapter instance = **one IO-Controller context** on **one Ethernet interface / PN segment**.
- Multiple IO-Devices per controller; `GenericEquipment` mappings from station/slot/subslot/process offsets.
- RT Class 1 cyclic image → private process image → `poll()` → LiveStateCache → (future) CIC.
- DCP/naming/AR as provided by stack; basic diagnostics.
- In-memory config first; Designer GSDML UX later (ICP-1F).
- Tests against real device and/or vendor-approved device simulator — **not** fake TCP.

**Out of scope for first slice:** IRT, MRP, PROFIsafe, PROFIdrive, PROFIenergy, redundancy, native certification campaign, 1,200-device run, MES changes, CIC implementation, Designer implementation.

---

## 15. Architecture impact

```text
ICP
 ├── AdapterManager / PollScheduler / LiveStateCache   (ICP-1A — unchanged)
 └── Industrial adapters
       ├── OPC UA | Modbus | MQTT | REST | EtherNet/IP
       ├── PROFINET Gateway path (existing adapters)     ← KEEP
       └── Native PROFINET → IO-Controller → IO-Devices  ← FUTURE
```

- No PROFINET types in `Equipment.hh`, CIC DTOs, or MES.
- No change required to `IndustrialAdapter.hh` for the contract (private cyclic thread OK, as with MQTT).
- Topology differs: **one controller → many devices** (unlike one Modbus TCP session = one endpoint).

---

## 16. ADR-040 amendment

See `docs/decisions.md` ADR-040 **Amendment 2026-08-29**. Summary:

| Path | Status |
| --- | --- |
| Gateway integration | **SUPPORTED** (unchanged) |
| Native IO-Controller | **APPROVED FOR IMPLEMENTATION** pending Softing (primary) or Hilscher (alternate) procurement + explicit coding approval |
| Implementation in repo | **NOT IMPLEMENTED** |

---

## 17. Risks

1. Commercial license cost / royalties unknown until vendor quote.
2. Windows soft-NIC RT quality may force Hilscher for serious Win deployments.
3. Docker native PN is a second-class citizen — product docs must not over-claim.
4. Ubuntu 24.04 may need Softing porting validation (samples often older OS).
5. GSDML/config complexity → Designer (ICP-1F) is substantial follow-on work.
6. Conformance / certification of *our* ICP as a controller product is a separate commercial program.
7. PI CS alone underestimates engineering effort (forum guidance: not a free SDK).

---

## 18. Implementation complexity (technical, not calendar)

| Workstream | Invasiveness |
| --- | --- |
| Vendor procurement + legal | External dependency |
| Porting Softing to Ubuntu 24.04 ICP build | Moderate–high (HAL/NIC/threads) |
| `ProfinetIndustrialAdapter` + process-image mapping | Moderate (fits existing adapter pattern) |
| Windows Softing vs Hilscher dual path | High if both shipped initially — prefer Softing-first, Hilscher optional SKU |
| Test bench (hardware + CI policy) | Moderate; CI may remain gateway-only without hardware |
| Docs / Designer schema for GSDML | Later slices |

---

## 19. What remains gateway-only

Until native code ships and is validated:

- All production PROFINET customer deployments continue via **gateway → OPC UA/Modbus/REST/MQTT**.
- Even after native ships: gateway remains supported for plants that already use gateways, Docker-only ICP, or sites unwilling to expose L2/NIC privileges.

---

## 20. References (public)

- Softing PROFINET Controller Stack datasheet / PI product finder entries
- Softing Communication Configurator documentation
- Siemens PROFINET Driver docs (licensing; Windows soft-NIC not productive; Linux quantity structure)
- PI Community Stack pages + PNO license text; profinews toolkit notes
- RT-Labs p-net README (IO-Device only)
- Hilscher cifX / netX driver documentation
- Prior ADR-040 investigation (2026-08-28)

Exact commercial quotes, EULAs, and NDA SDKs were **not** executed in this evaluation cycle — procurement is the next gated step.
