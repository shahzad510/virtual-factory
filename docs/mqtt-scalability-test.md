# MQTT multi-equipment scalability validation

**This is a test/validation record, not a production feature.**

It does **not** mean the system is production-proven for hundreds of PLCs on MQTT.
It does **not** start Phase 7, MES, SCADA, EtherNet/IP, or PROFINET.
It does **not** prove cloud broker or vendor MQTT capacity.
Production `MqttIndustrialAdapter` was not redesigned solely for this test.

Distinguish:

- **Architectural capability** — one adapter instance per MQTT broker (ADR-038) can represent N machines via topic mappings.
- **Measured scalability** — this run validated that design at **10**, **50**, **100**, and **200** mapped equipment on one localhost Mosquitto broker, plus **2×50** on two brokers.

Do **not** say “supports 200 PLCs” as a production capacity guarantee.

---

## Objective

Determine whether the **current** MQTT architecture — one `MqttIndustrialAdapter` = one broker/session, many `GenericEquipment` mappings — can host many mapped machines without inventing per-PLC C++ classes, and whether broker failure stays isolated between adapters.

---

## Architecture tested

```text
                 IndustrialAdapter
                        |
              MqttIndustrialAdapter   (one MQTTAsync client / one broker)
                        |
                   one MQTT broker
                        |
          GenericEquipment × N  (topic mappings)
```

N machines on one broker ⇒ **one** adapter instance with N mappings (unlike OPC UA, where N servers ⇒ N adapters).

---

## Hardware / software environment

Recorded on the machine that ran the test (2026-08-25):

| Item | Value |
| --- | --- |
| Host | Linux, Ubuntu 24.04-class, x86_64 |
| Compiler | g++ 13.3.0, C++17 |
| Paho MQTT C | **1.3.13** (`libpaho-mqtt3as`, MQTT 3.1.1) |
| Test broker | Mosquitto **2.0.18** (local process; **DEVELOPMENT ONLY**) |
| Test binary | `build/mqtt_multi_equipment_scalability_test` |

---

## Test methodology

- `tests/mqtt_multi_equipment_scalability_test.cc`
- CMake target `mqtt_multi_equipment_scalability_test` (ctest TIMEOUT 900 s)
- Single-broker scales: **10 / 50 / 100 / 200** mappings
- Multi-broker: **2 brokers × 50** equipment
- Publish telemetry/state/fault in batches interleaved with `poll()`
- Measure connect time, poll cycle latency, RSS, FDs, CPU where practical
- Isolation: stop broker → `ConnectionState::Faulted`; equipment still listed
- Multi-broker isolation: stop broker A → adapter A Faulted; adapter B stays Connected

---

## Measured results (2026-08-25)

| Equipment | Brokers | Connect_ok | Connect_ms | Poll_avg_cycle_ms | After_poll RSS_kB | FDs | Telemetry | Isolation |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | :---: | :---: |
| 10 | 1 | 1 | ~36 | ~50.2 | ~8764 | 33 | OK | OK |
| 50 | 1 | 1 | ~33 | ~50.5 | ~8996 | 33 | OK | OK |
| 100 | 1 | 1 | ~405 | ~50.5 | ~9216 | 33 | OK | OK |
| 200 | 1 | 1 | ~1048 | ~50.6 | ~9616 | 33 | OK | OK |
| 100 (50+50) | 2 | 2 | ~2660 total | ~51.1 | ~9620 | 33 | OK | OK |

Notes:

- `pollTimeoutMs` default **50 ms** dominates poll cycle time when the queue is empty.
- Connect time grows with subscription count (telemetry + state + fault topics per machine).
- RSS growth is modest relative to OPC UA’s one-process-per-server pattern (different architecture).
- Local Mosquitto only — **not** cloud/vendor certification.

---

## Status label

**VALIDATED** under the conditions above. **Not** production capacity certification.
