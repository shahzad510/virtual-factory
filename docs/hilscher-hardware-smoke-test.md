# Hilscher hardware smoke-test plan

**Status:** PLAN ONLY for plant execution. Physical Hilscher hardware may be absent. Repository readiness tooling lives in `hilscher_hardware_readiness_test` and [`hilscher-hardware-validation-procedure.md`](hilscher-hardware-validation-procedure.md).

These tests are **HARDWARE VALIDATION** when run against real cards. They are distinct from SOFTWARE-INTEGRATION TESTS.

Do not claim Docker Desktop Windows = plant-grade native PROFINET.

---

## PROFINET (when hardware arrives)

**Hardware**

- CIFX 50E-RE
- NXLFW-PNM controller firmware
- NXLIC-MASTER
- Real or vendor-supported software PROFINET IO-Device
- GSDML imported in SYCON.net / Communication Studio

**Validate**

1. Driver init (`cifXDriverInit` / NXDRV)
2. Board + channel enumeration
3. Load SYCON `config.nxd` (`xChannelDownload` / DOWNLOAD_MODE_CONFIG)
4. Host ready + bus on
5. DCP (station naming) — **via firmware / Protocol API, not a homemade packet**
6. AR establishment
7. RT Class 1 cyclic IO
8. Process data mapping into `GenericEquipment`
9. Slots / subslots as engineered in SYCON
10. Diagnostics
11. Device loss
12. Recovery / explicit ICP reconnect (`connect()` after Faulted)
13. Multiple IO-Devices on one controller adapter
14. Clean shutdown (`xChannelBusState` off, host not ready, close)

**Pass criteria:** plant-visible cyclic IO and recovery. SOFTWARE-INTEGRATION tests remaining green is not sufficient.

---

## PROFIBUS (when hardware arrives)

**Hardware**

- CIFX 50E-DP
- DP Master firmware / license
- Real DP slave
- GSD imported in SYCON

**Validate**

1. Master startup
2. Slave configuration from SYCON artifact
3. Cyclic DP IO
4. Diagnostics
5. Slave failure
6. Recovery / explicit reconnect
7. Mapping into `GenericEquipment`
8. Multiple slaves on one master adapter
9. Clean shutdown

---

## Combined

1. CIFX 50E-RE + CIFX 50E-DP on the same host
2. Independent `ProfinetIndustrialAdapter` and `ProfibusIndustrialAdapter` instances
3. PN failure does not fault PB equipment (and vice versa)

---

## Out of scope until hardware exists

- Gazebo as a fieldbus simulator
- UaExpert as a native PROFINET substitute (gateway/OPC UA only)
- Fake PROFINET over TCP/UDP
- pyprofibus dummy bus
- Homemade DCP / AR / DP packets
