# Connectivity Integration Contract (CIC)

**Status:** **PLANNED** — architecture only (ADR-043). **Not implemented.**

The **Connectivity Integration Contract** is the **only** approved boundary between the **Industrial Connectivity Platform (ICP)** and any consumer: **MES Core**, SCADA, ERP, third-party MES, or custom applications.

Neither product may depend on the other's implementation. Both depend on **CIC**.

---

## 1. Purpose

| Requirement | How CIC satisfies it |
| --- | --- |
| ICP sellable without MES | Northbound API exposes normalized industrial data |
| MES sellable without our ICP | MES consumes CIC via `IIndustrialDataProvider` adapters |
| Third-party interchange | Versioned, technology-independent schema |
| No protocol leak | No NodeIds, registers, topics, CIP tags, PROFINET slots in contract |

---

## 2. Contract concepts (v1 — planned)

| Concept | Description |
| --- | --- |
| `EquipmentId` | Stable string; globally unique within an ICP deployment; configuration-defined |
| `EquipmentType` | Metadata string (`"plc"`, `"mixer"`, …) — not a C++ class |
| `Capability[]` | Open-ended capability tags |
| `TelemetryPoint` | `{name, value, unit, observedAtUtc, quality?}` |
| `OperationalState` | `Stopped` \| `Running` |
| `MachineFault` | `{active, reasonCode?, reasonText?}` from mapped process data |
| `CommunicationState` | Per equipment view of source/session health |
| `SourceHealth` | Adapter/source id, protocol family metadata, connection state, lastError |
| `CommandRequest` | `{requestId, equipmentId, command, parameter?}` |
| `CommandResult` | `{requestId, accepted, message}` |
| `IndustrialEvent` | `{eventId, type, equipmentId, sourceId, timestampUtc, payload}` |

### Does not cross the boundary

- OPC UA NodeIds, Modbus maps, MQTT topics, REST paths, CIP tags
- PROFINET GSDML, station names, slot/submodule engineering
- MES work order IDs, material lots, scheduling state

---

## 3. Transport (planned — not chosen exclusively yet)

CIC semantics must be expressible over:

| Transport | Role |
| --- | --- |
| **gRPC** | Primary high-performance API (internal + integrated deployments) |
| **REST + OpenAPI** | Primary external / third-party integration |
| **WebSocket or SSE** | Live telemetry, state, comms health, industrial events |

**In-process SDK** (optional): same semantics for embedded integrated deployments — not a second architecture.

**Distinct from:** `RestIndustrialAdapter` (southbound industrial HTTP **client**, ADR-037).

---

## 4. Versioning

- Contract version: **SemVer** (`cic/v1`, `cic/v2`, …)
- ICP and MES Core release on **independent** product version tracks
- Published **compatibility matrix**: which MES version supports which CIC version

---

## 5. Fault model

| Signal | Producer | Consumer interpretation |
| --- | --- | --- |
| Communication fault | ICP (`SourceHealth`, `CommunicationState`) | Data may be stale; commands rejected |
| Machine fault | ICP (`MachineFault` from mapping) | Process/alarm workflows |
| MES availability | MES Core only | Independent of comms (ADR-030) |

Communication fault **≠** machine fault (Phase 6 invariant preserved).

---

## 6. Equipment identity stability

- `EquipmentId` is assigned in **ICP configuration** and remains stable when equipment moves between lines/floors in MES.
- MES `EquipmentResource` references `equipmentId` — it does not rename industrial identity when plant assignment changes.

---

## 7. Implementation status

| Item | Status |
| --- | --- |
| CIC schema (Protobuf/OpenAPI) | **NOT IMPLEMENTED** |
| ICP northbound server | **NOT IMPLEMENTED** |
| MES `IIndustrialDataProvider` | **NOT IMPLEMENTED** |
| Phase 6 `Equipment` / `GenericEquipment` | **IMPLEMENTED** — in-process contract precursor; CIC DTOs align with these semantics |

See ADR-043. Do not implement APIs until an approved implementation slice explicitly starts.
