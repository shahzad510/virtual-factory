# MES Core — product architecture

**Status:** **PLANNED** — architecture / documentation only (ADR-042, ADR-045). **Not implemented** (official SoT Phase 7).

MES Core is the second independently sellable product. It consumes industrial data **only** through the **Connectivity Integration Contract (CIC)** — never through protocol SDKs.

---

## 1. Product definition

**MES Core** is independently:

- deployable
- sellable
- licensable
- versioned
- upgradeable
- configurable
- operable
- equipped with its **own GUI**

MES Core must **not** hard-depend on our ICP implementation. It must work with:

- Our ICP (via CIC client)
- Third-party connectivity exposing CIC-compatible API
- Simulated provider (tests, demos)

---

## 2. Scope — what MES Core owns

| Domain | Owner |
| --- | --- |
| Plant hierarchy (ADR-027) | MES Core |
| Logical equipment registry & MES `EquipmentResource` | MES Core |
| Production orders, operations, execution | MES Core |
| Resource management — capability vs availability (ADR-024, 030) | MES Core |
| Materials, scrap, quality, genealogy (ADR-031, 034) | MES Core |
| Downtime, OEE, KPIs, analytics (ADR-032, 033) | MES Core |
| Scheduling, readiness (ADR-029) | MES Core |
| MES API & **MES GUI** | MES Core |
| MES configuration & persistence | MES Core |

### MES Core must NOT own or depend on

- open62541, libmodbus, libcurl, Paho, libplctag, PROFINET stacks
- Protocol NodeIds, registers, topics, CIP tags
- `IndustrialAdapter` implementations
- AdapterManager, PollScheduler, protocol configuration
- ICP Designer

---

## 3. MES GUI (core product component)

MES Core has its **own GUI** — separate from **ICP Designer** (ADR-044 vs ADR-045).

**Planned UX domains:** plant overview, work centers, orders, execution, materials, quality, OEE, scheduling, reporting, administration.

MES GUI consumes **MES API** — not industrial protocol APIs.

**GUI stack (planned):** web/.NET per ADR-011. **Not implemented.**

---

## 4. Integration with industrial data

```text
                    ┌─────────────────────┐
                    │  IIndustrialDataProvider   (MES abstraction)
                    └──────────┬──────────┘
           ┌───────────────────┼───────────────────┐
           ▼                   ▼                   ▼
    IcpCicClient      ThirdPartyCicClient   SimulatedProvider
           │                   │                   │
           └───────────────────┴───────────────────┘
                               │
                    Connectivity Integration Contract
                               │
                         (ICP or compatible)
```

MES business logic references **`equipmentId`** from CIC — not protocol details.

---

## 5. Deployment models

| Model | Description |
| --- | --- |
| **A — ICP only** | Customer uses existing MES; our ICP feeds via CIC |
| **B — MES only** | Customer uses existing connectivity; our MES consumes via CIC |
| **C — Integrated** | ICP + MES Core co-deployed; CIC in-process or localhost |
| **D — Mixed** | Third-party ICP → our MES, or our ICP → third-party MES |

---

## 6. Official SoT mapping

Official **Phase 7** = **MES Core** product implementation (slices 7A+ in roadmap).

MES Core implementation **requires** CIC v1 from ICP (**ICP-1C** minimum) before live industrial integration — simulated provider may proceed earlier in development.

**Phase 7 NOT STARTED.** Do not implement until explicitly approved.

See ADR-042, ADR-045, `docs/connectivity-integration-contract.md`.
