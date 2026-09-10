# Hilscher hardware test report template

Copy this file for each physical validation run.  
**Do not** mark Pass for native PROFINET/PROFIBUS unless the peer was on a real bus.

---

## Identity

| Field | Value |
| --- | --- |
| Date (UTC) | |
| Operator | |
| ICP git branch | `cursor/icp-hilscher-native-development-a88d` |
| ICP commit | |
| OS | Windows 10 / 11 / Server / Ubuntu 24.04 / other: |
| Kernel / build | |
| Host architecture | |

## Hilscher stack

| Field | Value |
| --- | --- |
| Hardware SKU | CIFX 50E-RE / CIFX 50E-DP / other: |
| Serial number | |
| Device number | |
| Firmware name/version | |
| License product | NXLIC-MASTER / other: |
| License verification method | SYCON / vendor tool (MANUAL) |
| Driver product/version | NXDRV-WIN / NXDRV-LINUX: |
| libcifx / SDK version | |
| `VF_ENABLE_HILSCHER_PROFINET` | ON / OFF |
| `VF_ENABLE_HILSCHER_PROFIBUS` | ON / OFF |

## Peer / engineering

| Field | Value |
| --- | --- |
| Protocol under test | PROFINET / PROFIBUS |
| IO-Device or DP slave | |
| GSDML / GSD file | |
| SYCON artifact path | |
| Network topology | switch model / RS-485 segment: |
| ICP-1B config file | |

## Preflight (`hilscher_hardware_readiness_test`)

| Field | Value |
| --- | --- |
| Readiness state | SDK_MISSING / DRIVER_MISSING / NO_BOARD / NO_CHANNEL / WRONG_FIRMWARE / FIRMWARE_NOT_READY / LICENSE_NOT_READY / READY_FOR_TEST / MANUAL_VERIFICATION_REQUIRED |
| Driver version reported | |
| Board name/alias | |
| Channel | |
| Notes | |

## Results

| Step ID | Description | Pass / Fail / Blocked / N/A | Evidence |
| --- | --- | --- | --- |
| | | | |
| | | | |

## Observed errors

```
(paste adapter lastError / driver logs)
```

## Packet / network evidence (optional)

Wireshark / PROFINET analyzer / PROFIBUS analyzer notes:

```
```

## Overall

| Field | Value |
| --- | --- |
| Overall result | PASS / FAIL / INCOMPLETE |
| Native plant IO claimed? | **NO** unless Pass and evidence attached |
| Communication fault ≠ machine fault verified? | YES / NO / N/A |
| Explicit reconnect verified? | YES / NO / N/A |

## Sign-off

| Role | Name | Date |
| --- | --- | --- |
| Tester | | |
| Reviewer | | |
