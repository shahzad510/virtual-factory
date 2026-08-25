# OPC UA multi-PLC scalability validation

**This is a test/validation record, not a production feature.**

It does **not** mean the system is production-proven for hundreds of PLCs.
It does **not** start Phase 7, MES, SCADA, REST, MQTT, EtherNet/IP, or PROFINET.
It does **not** prove production hardware capacity.
Production `OpcUaIndustrialAdapter` was not redesigned for this test.

Distinguish:

- **Architectural capability** — one adapter instance per OPC UA server (ADR-026) can represent N independent PLCs.
- **Measured scalability** — this run validated that design at **100** and **200** simulated servers under the conditions below.

Do **not** say “supports unlimited PLCs.”

---

## Objective

Determine whether the **current** OPC UA architecture — one `OpcUaIndustrialAdapter` instance per OPC UA server — can host many independent connections without cross-server contamination, and whether one PLC failure stays isolated.

---

## Architecture tested

Inspected production code (`industrial/include/virtual_factory/industrial/`, `industrial/src/`, existing `tests/opcua_test_server.*`):

- One `OpcUaIndustrialAdapter` owns one `ClientHandle` / one `UA_Client *`.
- `connect()` creates that client (`UA_Client_new`), timeout 2000 ms, `noReconnect = true`, `MessageSecurityMode` None, then `UA_Client_connect` to `OpcUaAdapterConfig::endpointUrl`.
- `disconnect()` clears bound equipment, disconnects, and deletes the client.
- `poll()` reads mapped telemetry / state / fault nodes; any read failure calls `enterFault()` (`ConnectionState::Faulted`). That is a **comms** fault, not `Equipment::fault()`.
- Equipment is mapping-driven `GenericEquipment` wrapped by a bound adapter object. No Pump/Robot/Oven classes.
- One adapter instance = one endpoint. Multiple adapters can coexist as independent objects.
- The existing mixer/pump `OpcUaTestServer` was left unchanged. This test uses a separate fixture (`OpcUaScalePlcServer`).

```text
                 IndustrialAdapter
                        |
              OpcUaIndustrialAdapter   (one UA_Client)
                        |
                   one OPC UA server
                        |
                  GenericEquipment
```

N PLCs ⇒ N adapter instances. The test did **not** put hundreds of `UA_Client` objects inside one adapter.

MES-facing APIs (`IndustrialAdapter`, `Equipment`, `GenericEquipment`) were not changed.

---

## Hardware / software environment

Recorded on the machine that ran the test (2026-08-23):

| Item | Value |
| --- | --- |
| Host | Linux `shz` 7.0.0-29-generic, Ubuntu 24.04.2 LTS, x86_64 |
| CPU | Intel Core i5-8350U @ 1.70 GHz, 8 threads |
| RAM | 16226040 kB (~15.5 GiB) |
| Compiler | g++ 13.3.0, C++17 |
| open62541 | **1.4.0-rc2** (`pkg-config --modversion open62541`) |
| Test binary | `build/opcua_multi_server_scalability_test` |
| Stack | real open62541 `UA_Server` + `UA_Client` (not `MockIndustrialAdapter`) |

---

## Test methodology

Test-only code (not production architecture):

- `tests/opcua_scale_plc_server.hh/.cc` — one simulated PLC server each
- `tests/opcua_multi_server_scalability_test.cc` — scale runner
- CMake target `opcua_multi_server_scalability_test` (ctest TIMEOUT 600 s)

Each simulated PLC:

- its own open62541 server on a dynamically bound localhost TCP port
- unique equipment id `PLC_NNN_MACHINE_001`
- unique node identifiers
- unique telemetry (`temperature = 20+N`, `pressure = 1+0.01*N`, `speed = 10+N`)
- commands `start`, `stop`, `set_speed` via existing mappings and `GenericEquipment`

Scales: 10, 25, 50, 100, then 200 (100 passed, so 200 was included). Not 500/1000.

At each scale:

1. Start N servers, create N adapters, connect each to a different endpoint.
2. Poll every adapter for 100 cycles; require zero poll failures.
3. Assert telemetry isolation (values and equipment ids).
4. Execute `start` / `set_speed` / `stop` on every adapter; assert writes landed on the matching server (`set_speed` setpoint = `1000+N`).
5. At 100 only: stop PLC-037, poll all, confirm victim `Faulted` and all others `Connected`, comms fault ≠ machine fault; restart PLC-037, reconnect adapter 37, confirm healthy telemetry.
6. Disconnect all adapters, reconnect all, poll, confirm telemetry.
7. Snapshot RSS (`/proc/self/status` VmRSS), FD count (`/proc/self/fd`), CPU (`getrusage`).

### In-process limitation (server vs client load)

**Servers and clients share one process.** RSS, FDs, and CPU are **total process load**, not client-only production load.

| Bucket | What this test measures |
| --- | --- |
| SERVER LOAD | N `UA_Server` iterate threads in the test process |
| CLIENT/ADAPTER LOAD | N `OpcUaIndustrialAdapter` / `UA_Client` instances |
| TOTAL PROCESS LOAD | the numbers below |

Do not treat these numbers as production PLC hardware, WAN latency, or certificate-mode OPC UA.

CPU times from `getrusage(RUSAGE_SELF)` are **cumulative from process start**. Later scales include work from earlier scales. RSS and FD counts are snapshots after connect at that scale.

---

## Results

Wall-clock for the full suite (10→200): about 240 s.

### Connection

| Servers | Success | Failure | Total (ms) | Avg (ms) | Max (ms) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 10 | 10 | 0 | 64.8 | 6.5 | 8.6 |
| 25 | 25 | 0 | 170.7 | 6.8 | 9.5 |
| 50 | 50 | 0 | 392.2 | 7.8 | 24.2 |
| 100 | 100 | 0 | 784.2 | 7.8 | 18.5 |
| 200 | 200 | 0 | 1357.1 | 6.8 | 21.6 |

Connect is sequential. Average localhost connect stayed on the order of 6–8 ms.

### Polling (100 cycles after all connections)

| Servers | Successful adapter-polls | Failed | Total (ms) | Avg cycle (ms) | Avg adapter (ms) | Max adapter (ms) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10 | 1000 | 0 | 602 | 6.0 | 0.60 | 15.7 |
| 25 | 2500 | 0 | 1525 | 15.3 | 0.61 | 18.0 |
| 50 | 5000 | 0 | 3856 | 38.6 | 0.77 | 17.2 |
| 100 | 10000 | 0 | 8678 | 86.8 | 0.87 | 21.7 |
| 200 | 20000 | 0 | 11344 | 113.4 | 0.57 | 29.5 |

Per-adapter poll time stayed ~0.6–0.9 ms. Full-cycle time scaled with N because `poll()` is called sequentially.

### Resource usage (snapshot after connect; servers + clients)

Baseline (no servers): RSS 3852 kB, FDs 27, CPU user 0.002 s / sys 0.005 s.

| Servers | RSS (kB) | FDs | CPU user (s, cumulative) | CPU sys (s, cumulative) |
| ---: | ---: | ---: | ---: | ---: |
| 10 | 57032 | 87 | 1.333 | 0.088 |
| 25 | 128252 | 177 | 5.915 | 0.833 |
| 50 | 246268 | 327 | 16.945 | 3.147 |
| 100 | 479548 | 627 | 44.181 | 9.401 |
| 200 | 944224 | 1227 | 115.740 | 32.066 |

RSS grew roughly linearly (~4.7 MB per simulated PLC, server thread + client together). FD count matched `27 + 6×N`.

### Telemetry isolation

Passed at every scale. Adapter *i* read `PLC_i` temperature/pressure/speed only. Adapter *i* could not look up `PLC_{i-1}` equipment.

### Command isolation

Passed at every scale. `start` / `set_speed` / `stop` on adapter *i* wrote PLC-*i* nodes. Speed setpoint `1000+i` was present only on that server.

### Failure isolation (100-server level)

Passed (`isolation_ok=1`):

1. All 100 connected and healthy.
2. PLC-037 stopped.
3. After `poll()`: adapter 37 → `Faulted`; adapters 36, 38, and all others → `Connected`.
4. PLC-037 equipment remained listed; `Equipment::fault()` stayed false (comms ≠ machine fault).
5. PLC-037 restarted; adapter 37 `connect()` + `poll()` restored unique telemetry.

One PLC failure did not fail the adapter layer.

### Reconnect

Disconnect all, reconnect all, poll. All adapters recovered unique telemetry.

| Servers | Reconnect total (ms) | Avg per adapter (ms) | Result |
| ---: | ---: | ---: | --- |
| 10 | 1147.2 | 114.7 | all ok |
| 25 | 2852.8 | 114.1 | all ok |
| 50 | 5322.5 | 106.4 | all ok |
| 100 | 10902.0 | 109.0 | all ok |
| 200 | 22171.0 | 110.9 | all ok |

Average ~110 ms is sequential disconnect+connect. Client teardown in this open62541 build waits on the event loop (~100 ms), so reconnect latency is dominated by teardown, not `UA_Client_connect`.

---

## Limitations

- In-process simulated servers **and** clients; not production PLC hardware.
- Localhost, SecurityPolicy#None, anonymous — DEVELOPMENT ONLY.
- One mapped machine per simulated PLC.
- Sequential connect/poll/reconnect on one thread.
- CPU figures are process-cumulative.
- RSS cannot be split into server vs client.
- Not SignAndEncrypt, subscriptions, history, WAN, or packet loss (except the explicit PLC-037 stop).

---

## Conclusions

**Architectural capability:** the current design (N adapters, N clients, N endpoints) can represent many independent OPC UA servers. Isolation, command routing, comms-vs-machine fault, and reconnect behaved correctly in this test.

**Measured scalability:** validated in this test at **100** and **200** simulated OPC UA servers under the conditions above.

Do **not** say: “supports unlimited PLCs” or “production proven for hundreds of PLCs.”

Practical limit observed: **sequential poll cycle time**. At 100 servers that was ~87 ms/cycle; at 200, ~113 ms/cycle. Per-adapter poll stayed under 1 ms on average. Correctness did not break.

---

## Bottlenecks (observed, not fixed)

| Observation | Scale | Notes |
| --- | --- | --- |
| Sequential poll cycle | 100 / 200 | Grows with N; per-adapter poll stayed ~0.6–0.9 ms |
| Sequential reconnect | all | ~110 ms/adapter, teardown-dominated |
| Combined RSS | 200 | ~944 MB for 200 servers **plus** 200 clients in one process |
| Combined FDs | 200 | 1227 open FDs in the test process |
| Connect max | 50–200 | 18–24 ms (localhost, sequential, loaded process) |

No correctness failure occurred. Production code was not changed to “fix” these.

---

## Recommendations (not implemented)

Only if a future poll-rate or ops requirement needs them:

- A MES-side collection of `IndustrialAdapter*` (not multiple `UA_Client`s in one adapter)
- Parallel or asynchronous `poll()` workers if cycle time must stay well under ~100 ms at 100+ PLCs
- Connection lifecycle / backoff for real network flaps
- Health/metrics for per-adapter `connectionState()`
- Client-only RSS measurement with servers in another process

Do not implement these unless a measured requirement demands it. Do not merge many endpoints into one `OpcUaIndustrialAdapter`.
