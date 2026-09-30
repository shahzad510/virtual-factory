# Hilscher test-peer options (research only)

**Date:** 2026-08-30  
**Scope:** documentation / procurement research for a legitimate PROFINET IO-Device or PROFIBUS DP slave peer.  
**Do not** add simulators as ICP dependencies. **Do not** implement a fake fieldbus stack in this repository.

---

## Goal

When CIFX hardware arrives, ICP needs a **test peer**:

- PROFINET: IO-Device that can exchange RT Class 1 cyclic data with a Hilscher IO-Controller
- PROFIBUS: DP slave that can exchange cyclic DP with a Hilscher DP Master

---

## PROFINET IO-Device options

| Option | Realistic for lab? | Notes |
| --- | --- | --- |
| **Inexpensive real remote I/O** (any PI-conformant IO-Device with GSDML) | **Yes — preferred** | Buy a small Ethernet remote I/O / coupler. Engineer in SYCON with vendor GSDML. Clearest pass/fail. |
| **CODESYS PROFINET Device** (soft device on Ethernet NIC or CIFX device firmware) | **Yes — with care** | Commercial CODESYS Device SL turns a CODESYS runtime into an IO-Device. Useful software peer; still a third-party stack, **not** part of ICP. Label as test peer. |
| **Hilscher netX IO-Device hardware / evaluation** | **Yes** | Same vendor family; good for interoperability labs. Separate from ICP controller card. |
| **Vendor OEM device simulators** bundled with stacks | **Sometimes** | Only if the vendor ships a supported peer. Do not use homemade TCP “PROFINET”. |
| **UaExpert / OPC UA** | **No for native PN** | Useful for gateway path only. |
| **Homemade UDP/TCP PROFINET** | **Forbidden** | |

**Recommendation:** start with a **real low-cost IO-Device** + official GSDML. Optionally add CODESYS Device as a second peer later.

---

## PROFIBUS DP slave options

| Option | Realistic for lab? | Notes |
| --- | --- | --- |
| **Inexpensive real DP slave / remote I/O** with GSD | **Yes — preferred** | Requires RS-485 cabling, termination, correct baud. |
| **USB/PCI PROFIBUS slave adapters** sold as test slaves | **Sometimes** | Vendor-specific; verify DP-V0 cyclic support. |
| **CODESYS / PLC acting as DP slave** (with PB interface) | **Possible** | Needs real PROFIBUS hardware on that PLC — not a soft NIC. |
| **Software-only DP slave on a PC without RS-485** | **Generally no** for credible cyclic DP | Physical layer matters. |
| **pyprofibus / homemade DP** | **Forbidden** as ICP validation | |

**Recommendation:** real DP remote I/O or coupler with published GSD.

---

## What not to put in ICP

- No CODESYS runtime as a submodule
- No third-party simulator linked into `virtual_factory_industrial`
- No Gazebo “fieldbus” plugin
- Softing remains a **future alternate controller path**, not a test peer requirement

---

## Labeling rule

If a software IO-Device/slave is used:

> TEST PEER ONLY — not part of ICP product; not native fieldbus proof by itself without the Hilscher controller card exchanging real cyclic data.
