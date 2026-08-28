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

**Current phase:** Phase 6 — Industrial Adapter Layer: **IN PROGRESS**. Slices **6A–6G** done. **6H GATEWAY-ONLY** (PROFINET via external gateway → OPC UA/Modbus/REST; native adapter **NOT IMPLEMENTED**, ADR-040). Phase 6 final audit pending. Phase 7 **NOT STARTED**.

**Next step:** Phase 6 final audit, then Phase 7 only when explicitly instructed. Native PROFINET deferred unless a future ADR approves commercial or PI stack.

**Next major phase:** Phase 7 — MES Core + Resource Management (**NOT STARTED** / **NOT IMPLEMENTED**). All MES features are **PLANNED** (ADR-024, 027–035). Do not implement them until instructed. Do not start Phase 7 after PROFINET investigation.

**SoT maintenance:** edit `docs/source/MES_SCADA_Virtual_Factory_Source_of_Truth.md`, then run `docs/source/generate-sot-pdf.sh`. The PDF is the architectural authority; the Markdown is how that PDF is maintained.
