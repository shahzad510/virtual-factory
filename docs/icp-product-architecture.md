# Industrial Connectivity Platform (ICP) — product architecture

**Status:** **PLANNED** — architecture / documentation only (ADR-042, ADR-044). **Not a shipping product yet.**

Phase 6 delivered the **adapter foundation**. ICP is the **complete standalone industrial connectivity product** built on that foundation.

---

## 1. Product definition

**ICP** is independently:

- deployable
- sellable
- licensable
- versioned
- upgradeable
- configurable
- operable
- equipped with its **own GUI**

ICP must be **commercially complete and useful without MES Core**.

---

## 2. Scope — what ICP owns

| Domain | Owner | Phase 6 today |
| --- | --- | --- |
| Protocol adapters (OPC UA, Modbus, MQTT, REST, EtherNet/IP) | ICP | **IMPLEMENTED** |
| PROFINET via gateway | ICP | **SUPPORTED VIA GATEWAY** (ADR-040) |
| Native PROFINET IO-Controller | ICP (future) | **DEFERRED** |
| `Equipment` / normalized live state | ICP | **IMPLEMENTED** (contract lib) |
| Adapter lifecycle (`AdapterManager`) | ICP | **IMPLEMENTED** (ICP-1A) |
| Poll scheduling (`PollScheduler`) | ICP | **IMPLEMENTED** (ICP-1A) |
| Connection management / reconnect policy | ICP | **PARTIAL** — explicit `connect()` only; **no** app-level auto-reconnect (ICP-1A) |
| Configuration & equipment mappings | ICP | **PARTIAL** — in-memory factory/config in ICP-1A; durable store is ICP-1B |
| Industrial data acquisition | ICP | **IMPLEMENTED** — adapters + PollScheduler |
| Command execution to field | ICP | **IMPLEMENTED** (via `Equipment::execute`) |
| Connection health / diagnostics | ICP | **IMPLEMENTED** — `ConnectionState` / cache `stale` + `communicationState` |
| Industrial event acquisition | ICP | **NOT IMPLEMENTED** (ICP-1D) |
| Northbound **Connectivity Integration Contract** | ICP | **NOT IMPLEMENTED** (ADR-043 / ICP-1C) |
| ICP API (gRPC / REST / stream) | ICP | **NOT IMPLEMENTED** |
| **ICP Designer GUI** | ICP | **NOT IMPLEMENTED** (ADR-044 / ICP-1F) |
| ICP configuration storage | ICP | **NOT IMPLEMENTED** (ICP-1B) |
| ICP deployment package | ICP | **NOT IMPLEMENTED** (ICP-1E) |
| Live state cache | ICP | **IMPLEMENTED** (ICP-1A `LiveStateCache`) |

ICP must **not** depend on MES business logic, MES database, work orders, OEE, scheduling, materials, or MES GUI.

---

## 3. ICP GUI — ICP Designer (core product component)

The ICP GUI is **not** an afterthought. It is a **major product component** (ADR-044).

**Intended UX:** **DRAG → DROP → CONFIGURE → CONNECT → DEPLOY**

```text
                    ICP DESIGNER

 Sources          Equipment           Integrations         Services
 ───────          ─────────           ────────────         ────────
 OPC UA           PLC-001 ───┐
 Modbus           PLC-002 ───┤
 MQTT             PLC-003 ───┤──►  ICP Runtime  ──►  MES (via CIC)
 REST             Machine-001┤                        SCADA
 EtherNet/IP      Gateway-001┘                        ERP
 PROFINET Gateway
 Native PROFINET (future)
```

Designer responsibilities (planned):

- Visual topology: sources, gateways, equipment mappings, northbound integrations
- Protocol-specific **configuration panels** (never leaked to MES)
- Connect / test / monitor session health
- Deploy configuration to ICP runtime without recompiling C++
- Export/import configuration; secrets by reference

**GUI stack (planned):** web/.NET aligned with ADR-011 — **separate application** from MES GUI. Do not implement in this documentation pass.

---

## 4. Modularity and scale

Adding equipment is **configuration/onboarding**, not source-code change:

```text
100 PLCs → 200 → 500 → 1000+
```

Preserved adapter topology (mandatory):

- One OPC UA server/endpoint → one adapter instance
- One Modbus TCP endpoint → one adapter instance
- One REST origin → one adapter instance
- One MQTT broker → one adapter session
- One EtherNet/IP device → one adapter instance
- PROFINET → gateway → one of the above per gateway northbound endpoint

Many logical `GenericEquipment` mappings may share one adapter session (e.g. one MQTT broker, one OPC UA gateway server).

---

## 5. Northbound integrations (via CIC)

ICP publishes normalized data to **any** CIC-compatible consumer:

```text
ICP ──CIC──► Our MES Core
ICP ──CIC──► Rockwell / Siemens / third-party MES
ICP ──CIC──► SCADA / ERP / data lake / custom apps
```

ICP does **not** embed MES-specific assumptions in adapters or runtime.

---

## 6. Implementation slices (planned — do not start without approval)

| Slice | Deliverable |
| --- | --- |
| **ICP-1A** | Runtime: AdapterManager, PollScheduler, LiveStateCache | **IMPLEMENTED** / **TESTED** |
| **ICP-1B** | Persistent configuration storage | **NOT STARTED** |
| **ICP-1C** | CIC v1 northbound API | **NOT STARTED** |
| **ICP-1D** | Command gateway + industrial events | **NOT STARTED** |
| **ICP-1E** | Standalone deployable package | **NOT STARTED** |
| **ICP-1F** | ICP Designer GUI (MVP) | **NOT STARTED** |

**Current repo state:** Phase 6 adapters + **ICP-1A runtime** (`icp/` → `virtual_factory_icp`). CIC, Designer, MES **not** started.

See `docs/roadmap.md` and ADR-042.
