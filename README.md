# Virtual Factory

MES + SCADA + industrial adapter platform. Gazebo Sim is a **virtual plant** for development, not the production architecture.

**Start here:** [docs/README.md](docs/README.md)

**Active Source of Truth (architecture):** [docs/MES_SCADA_Virtual_Factory_Source_of_Truth.pdf](docs/MES_SCADA_Virtual_Factory_Source_of_Truth.pdf)

| Document | Role |
| --- | --- |
| [docs/README.md](docs/README.md) | How to resume the project |
| [docs/MES_SCADA_Virtual_Factory_Source_of_Truth.pdf](docs/MES_SCADA_Virtual_Factory_Source_of_Truth.pdf) | Architectural authority |
| [docs/implementation-status.md](docs/implementation-status.md) | What is actually implemented |
| [docs/architecture.md](docs/architecture.md) | Current technical architecture |
| [docs/decisions.md](docs/decisions.md) | ADRs |
| [docs/roadmap.md](docs/roadmap.md) | Future phases |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | Change history |
| [docs/archive/](docs/archive/) | Historical documents only — not authoritative |

**Current phase:** Phase 6 — Industrial Adapter Layer: **IN PROGRESS**. Architecture + mock + OPC UA **COMPLETE** (multi-server = multiple adapter instances, ADR-026). Production Modbus / REST **NOT IMPLEMENTED**.

**Next major phase:** Phase 7 — MES Core (**NOT STARTED** / **NOT IMPLEMENTED**). Includes planned Resource Management (ADR-024). Do not implement it until instructed.
