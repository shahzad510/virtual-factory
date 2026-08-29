# Native PROFINET — proposed implementation plan (ICP)

**Status:** **PLAN ONLY** — not implemented. Awaiting explicit approval of stack selection + this plan before any production code.

**Authority:** ADR-040 (amendment 2026-08-29), ADR-042–045, [`profinet-native-evaluation.md`](profinet-native-evaluation.md).

**Git:** Evaluation secured in `docs(6h): reopen native PROFINET evaluation for ICP`. This plan does **not** authorize coding.

---

## 1. Selected vendor stack (provisional)

| Role | Vendor / product | Status |
| --- | --- | --- |
| **PRIMARY candidate** | **Softing PROFINET Controller Stack** | Pending procurement + SDK/license verification |
| **ALTERNATE / fallback** | **Hilscher cifX + PROFINET Controller firmware** | Use if Softing license/platform/Win RT is insufficient |
| **Situational** | Siemens PROFINET Driver | Linux/OEM (esp. SIMATIC/CP1625); **not** first choice for Win soft-NIC |

**Hard gate:** Softing is **not** finally selected until the commercial SDK package, EULA, and redistribution terms are in hand and verified against ICP product needs. Until then: **primary candidate only**.

**Update 2026-08-29:** Softing **SDK gate FAILED** in this environment. Hilscher feasibility: [`profinet-hilscher-evaluation.md`](profinet-hilscher-evaluation.md) — **FEASIBLE with mandatory cifX hardware**; **not approved for coding** until explicit approval + hardware/license + smoke test.

Do **not** assume commercial redistribution rights without signed vendor evidence.

---

## 2. Why Softing (primary candidate)

- Real **IO-Controller** (not IO-Device).
- Conformance Class A/B; RT Class 1 cyclic process data.
- Multi-device (documented ≤255 field devices / connections).
- **C** API (PNAK / Simple Controller Application Interface) — embeddable behind a private wrapper.
- Documented **Linux** and **Windows** product lines.
- Engineering via Softing **Communication Configurator** / GSDML workflow — prefer vendor tooling over a custom GSDML parser.
- Fits ICP commercial product better than PI Community Stack (toolkit, not turnkey) or p-net (wrong role).

**Why Hilscher as alternate:** Production-grade **Windows** RT via netX ASIC; proven cifX C API on Win + Linux; avoids soft-NIC jitter on Windows when Softing Win path proves insufficient.

---

## 3. Procurement requirements (SDK gate)

Before any CMake/SDK install or `ProfinetIndustrialAdapter` code, obtain and file:

| Item | Softing | Hilscher (if fallback) |
| --- | --- | --- |
| SDK / protocol package | Controller Stack binaries + porting layer | cifX driver + PN Controller firmware |
| Headers | PNAK / SCAI (confirm names in delivered package) | cifX API headers |
| Libraries | Static/shared as licensed | libcifX / toolkit |
| Runtime components | Stack runtime, SNMP/LLDP deps as required | Firmware loaders, config files |
| Samples | Controller sample apps | cifX PN controller samples |
| Documentation | Porting guide, API ref, OS notes | Driver + DPM manuals |
| License / EULA | Signed Softing OEM agreement | Hilscher HW + FW licenses |
| Redistribution terms | Explicit right to ship in commercial ICP | Customer ships card and/or OEM terms |
| Runtime licensing | Per-install / OEM fees (quote) | Per-card / firmware model |
| Development licensing | Dev seats / advance license | Dev kits / cards |
| Supported OS | Confirm **Win 10/11/Server**, **Ubuntu 24.04** in writing | Confirm same |
| CPU architectures | x86_64 required; ARM optional | x86_64 host + netX |
| Supported NICs | Porting-layer / NIC list | cifX form factors |
| Drivers | Any NDIS/AF_PACKET requirements | cifX kernel + user space |
| Required hardware | Dedicated Ethernet NIC (min) | cifX PCIe/M.2/etc. |
| PROFINET features | CC-A/B, RT1, DCP, AR, alarms (confirm) | Same via firmware |
| Vendor support | Named support channel / SLA | Named support |
| Conformance / certification | Who certifies ICP-as-controller product | Same |

**Technically possible ≠ legally redistributable.** Implementation starts only after legal/commercial clearance.

---

## 4. License requirements (summary)

- ICP is a **commercial** product → GPL-only or non-redistributable research stacks are out.
- Softing/Hilscher/Siemens: commercial OEM contracts required.
- PI Community Stack: membership + CS license possible later; **not** the first implementation path (integration cost).
- Runtime royalties / stickers / product-line licenses must be reflected in ICP SKU pricing.

---

## 5. Supported platforms (honest matrix)

ICP Core (non-PN) remains portable. Native PROFINET is **platform-constrained**.

| | Windows 10/11/Server | Ubuntu 24.04 LTS | Docker Linux | Docker Desktop (Win) |
| --- | --- | --- | --- | --- |
| **ICP Core** (OPC UA, Modbus, MQTT, REST, EIP, runtime, CIC later) | ✓ | ✓ | ✓ | ✓ |
| **PROFINET gateway path** | ✓ | ✓ | ✓ | ✓ |
| **Native PROFINET (Softing candidate)** | **?** — mark ✓ only after Softing confirms Win SKU + RT quality | **?** — mark ✓ only after Softing confirms Ubuntu 24.04 port | **?** — likely privileged + host NIC / macvlan; not default | **✗** — no reliable plant L2 |
| **Native PROFINET (Hilscher alternate)** | **?** → expected ✓ with cifX | **?** → expected ✓ | **?** — PCIe passthrough only | **✗** / rare |

Do **not** claim native PROFINET ✓ on any cell until vendor docs + a smoke bench prove it.

---

## 6. Windows strategy

1. Prefer Softing Windows Controller Stack (CCA product line) for software-only SKU **if** Softing confirms productive use on target NICs.
2. If Softing Win soft-NIC is demo-quality only (similar risk as Siemens Npcap path), ship **Hilscher cifX** as the Windows native-PN SKU.
3. Document ICP installer prerequisites: admin rights, NIC binding, driver install, no Docker Desktop for native PN.
4. Keep domain/runtime C++ portable; isolate Win drivers behind `profinet_session` / HAL.

---

## 7. Ubuntu 24.04 strategy

1. Softing porting layer → `AF_PACKET` / raw Ethernet; capabilities `CAP_NET_RAW` (or root for lab).
2. Dedicated physical NIC for the PROFINET segment (not shared casually with IT traffic).
3. PREEMPT_RT **optional** for first RT Class 1 slice; measure jitter; do not require RT kernel unless Softing mandates it.
4. Validate Softing sample on Ubuntu 24.04 **before** adapter integration (procurement smoke test).

---

## 8. Docker strategy

| Mode | Guidance |
| --- | --- |
| Default ICP Docker image | **Gateway PROFINET only** — no native stack required |
| Native PN in Docker Linux | Advanced: `--network=host` or macvlan, `CAP_NET_RAW`, dedicated interface; RT degraded; document as unsupported/experimental until proven |
| Docker Desktop on Windows | **Out of scope** for native PN plant connectivity |
| Hilscher in container | Needs device/PCIe passthrough — optional enterprise profile |

ICP application packaging must not block non-PN protocols when native PN is unavailable.

---

## 9. NIC requirements

- 100 Mbit full-duplex **switched** Ethernet PROFINET segment.
- Loopback / pure virtual bridges are **not** PROFINET validation.
- One `ProfinetIndustrialAdapter` ↔ one controller context ↔ one bound interface (unless Softing documents multi-IF in one instance — default is one IF per adapter).
- Softing: standard NIC + porting layer. Hilscher: cifX hardware.

---

## 10. Controller topology (≠ EIP)

```text
ONE ProfinetIndustrialAdapter  =  ONE IO-Controller on ONE NIC/segment
        │
        ├── IO-Device A  (station name, AR, slots/subslots)
        ├── IO-Device B
        ├── IO-Device C
        └── IO-Device N
```

| Protocol | Adapter instance means |
| --- | --- |
| EtherNet/IP | One device/session |
| Modbus TCP | One TCP endpoint |
| OPC UA | One UA endpoint |
| REST | One HTTP origin |
| MQTT | One broker (many topic mappings) |
| **PROFINET native** | **One IO-Controller** (many IO-Devices) |
| PROFINET gateway | Reuses OPC UA/Modbus/REST/MQTT topology |

Multiple independent PN networks ⇒ multiple `ProfinetIndustrialAdapter` instances.

`GenericEquipment` IDs remain configuration metadata (e.g. `PLC-001`) — **not** C++ classes.

---

## 11. Configuration model (Designer-ready)

In-memory first (align with ICP-1A); persistence = ICP-1B; GUI = ICP-1F.

```text
ProfinetAdapterConfig
  adapterId
  nicInterface          // OS interface name / Softing IF handle
  cycleTimeUs
  devices[]             // ProfinetDeviceConfig

ProfinetDeviceConfig
  stationName
  ip / deviceIdentity   // as required by stack
  gsdmlRef              // path or catalog id
  equipmentMappings[]   // ProfinetEquipmentMapping

ProfinetEquipmentMapping
  equipmentId           // GenericEquipment id
  type                  // metadata string
  slots[]               // ProfinetSlotMapping

ProfinetSlotMapping
  slot
  subslots[]
  processData[]         // ProcessDataMapping

ProcessDataMapping
  signalName            // telemetry or command key
  byteOffset, bitOffset, length
  datatype
  direction             // In | Out
  unit
```

No hard-coded machine catalogs. Same Designer later configures OPC UA / Modbus / MQTT / REST / EIP / gateway / native PN.

---

## 12. GSDML strategy

**Prefer vendor-supported configuration:**

1. Softing **Communication Configurator** (or equivalent) imports GSDML, builds offline configuration, exports binary/config consumed by the stack.
2. ICP stores: GSDML catalog references + exported config blob + our process-data ↔ equipment signal map.
3. **Do not** write a custom GSDML XML parser unless Softing/Hilscher leave a proven gap.
4. ICP Designer (future): Import GSDML → invoke/vendor-export or guided mapping UI → deploy config to runtime.

Runtime must not invent module layouts without GSDML/engineering input.

---

## 13. Runtime architecture

```text
ICP
 ├── AdapterManager / PollScheduler / LiveStateCache   (existing ICP-1A — unchanged this plan)
 └── Industrial adapters
       ├── … existing …
       ├── PROFINET Gateway path (unchanged)
       └── ProfinetIndustrialAdapter          // NEW when approved
              └── private profinet_session    // wraps Softing/Hilscher; no public vendor types
                     └── commercial stack
                            └── NIC → IO-Devices
```

- Vendor headers **only** in private `.cc` / `profinet_session` TU.
- Public ICP headers: config DTOs + `IndustrialAdapter` only.
- Background cyclic thread inside private session (like MQTT Paho pattern); `poll()` copies latest image into `GenericEquipment` with bounded work.

**Contract check:** `IndustrialAdapter.hh` / `Equipment.hh` expected **unchanged**. If a gap appears → **STOP** and report before editing.

---

## 14. Data flow

**Inbound (cyclic):**

```text
PN process image → stack → private session → ProfinetIndustrialAdapter::poll()
  → GenericEquipment telemetry/state → LiveStateCache → (future) CIC → MES/SCADA/third parties
```

**Outbound (commands):**

```text
MES/SCADA → CIC → ICP → Equipment::execute → adapter writes output image / acyclic write → IO-Device
```

MES never sees Softing/Hilscher/PN types.

---

## 15. Fault model

| Concern | Mechanism |
| --- | --- |
| Link / controller / AR failure | `ConnectionState::Faulted` + `lastError()` |
| Machine/process fault from diagnosis bits | `Equipment::fault()` / operational state via mapping |
| Stale cache while Faulted | LiveStateCache `stale` + communication state (ICP-1A pattern) |
| Reconnect | Explicit `connect()` after Faulted (no app-level auto-reconnect in first slice unless separately approved) |

Communication failure **≠** machine fault.

---

## 16. Testing hardware / software

**Preferred order:**

1. Real certified PROFINET IO-Device + managed switch + dedicated NIC.
2. Softing / vendor-supported device simulation or Communication Configurator online checks.
3. p-net **only** as an IO-Device peer for lab (never as our controller).

**Forbidden:** fake TCP/UDP “PROFINET” servers.

**First-slice tests (DEVELOPMENT / INTEGRATION VALIDATION ONLY):**

- Controller + NIC init
- DCP / station name
- AR establishment
- Cyclic IO in/out
- Process-data → GenericEquipment mapping
- Commands / outputs
- Diagnostics → equipment fault vs comms Faulted
- Device disconnect / explicit reconnect
- Multi IO-Device on one controller
- Two controllers (two adapters) isolation
- Invalid config / bad mapping bounds
- GSDML/vendor config load failure paths

No PI conformance claim without a formal certification campaign.

**Scalability (future, not now):** devices-per-controller, cycle time, CPU, RSS, threads, FDs, bandwidth, latency, jitter, startup, recovery. Report as **VALIDATED UNDER THESE TEST CONDITIONS** only.

---

## 17. First implementation slice (when approved)

**In scope:**

- Private Softing (or Hilscher) session wrapper
- `ProfinetIndustrialAdapter` implementing `IndustrialAdapter`
- RT Class 1 cyclic image exchange
- Multi IO-Device config
- DCP/naming/AR as provided by stack
- Slots/subslots + process-data mappings → `GenericEquipment`
- Basic diagnostics
- `connect` / `disconnect` / `poll` / `connectionState`
- AdapterFactory / CMake optional feature flag (off by default without SDK)
- Integration tests against real device or vendor-approved simulator
- Docs: status, capability honesty, platform matrix

**Out of scope (first slice):** IRT, MRP, PROFIsafe, PROFIenergy, PROFIdrive, advanced redundancy, Designer GUI, MES, CIC, Docker packaging as product, 1,200-device run, Windows packaging installer, custom GSDML parser.

Suggested slice label: **ICP-PN-1** (or reopen **6H-native**) — only when you explicitly approve after Softing SDK verification.

---

## 18. Future capabilities

- ICP Designer: drag/drop PN controller, NIC select, GSDML import, map, test, deploy
- Persistent config (ICP-1B)
- CIC exposure of PN-origin equipment (protocol-opaque)
- Hilscher Windows SKU / Softing dual transport
- Optional IRT/MRP if stack + customer demand
- Conformance certification program
- Native contribution to ~200 PN logical devices in the 1,200-device experiment

---

## 19. Risks

1. Softing quote / redistribution terms block commercial ICP.
2. Ubuntu 24.04 porting effort larger than datasheet implies.
3. Windows soft-NIC forces Hilscher hardware dependency for Win customers.
4. Docker customers expect native PN — must set expectations (gateway SKU).
5. GSDML/engineering UX complexity delayed until Designer.
6. Stack cycle threads vs PollScheduler interaction (priority, CPU).
7. Conformance/certification cost if marketing “PROFINET Controller” product claim.

---

## 20. Estimated complexity (technical)

| Workstream | Invasiveness |
| --- | --- |
| Procurement + legal | External gate |
| Softing porting on Ubuntu 24.04 | High |
| Private session + adapter | Moderate (pattern exists) |
| Config + process-image mapping | Moderate–high |
| Hardware test bench | Moderate |
| Optional Hilscher second transport | High (defer unless needed) |
| Designer GSDML UX | Later (ICP-1F) — high |

---

## 21. Files to create (when implementation approved — **not now**)

```text
industrial/include/virtual_factory/industrial/ProfinetIndustrialAdapter.hh   # or under icp/ — decide at coding approval
industrial/src/ProfinetIndustrialAdapter.cc
industrial/src/profinet_session.hh / .cc          # private
tests/profinet_adapter_test.cc
docs updates (implementation-status, CHANGELOG, capability matrix)
CMake option VIRTUAL_FACTORY_ENABLE_PROFINET      # off without SDK
```

Exact paths may move under `icp/` if approved to keep vendor deps out of the Phase 6 foundation library — **decide at coding gate** without changing Equipment/IndustrialAdapter contracts.

---

## 22. Files that must remain untouched (unless STOP + gap report)

- `equipment/include/virtual_factory/equipment/Equipment.hh`
- `industrial/include/virtual_factory/industrial/IndustrialAdapter.hh`
- Production OPC UA / Modbus / MQTT / REST / EtherNet/IP adapters
- Gazebo production sources
- MES sources (none / not started)
- ICP-1A runtime semantics (no auto-reconnect creep; no MES coupling)
- Gateway documentation demotion (gateway stays **SUPPORTED**)

---

## Stop conditions before coding

1. Softing (or Hilscher) SDK + EULA + redistribution cleared.
2. Platform matrix cells updated from `?` to proven ✓/✗.
3. Explicit user approval of this plan + selected stack.
4. No edits to protected interfaces without a separate STOP report.

**Native PROFINET code: NOT STARTED.**
