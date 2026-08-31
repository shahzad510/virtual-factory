# ICP Standalone Product — Acceptance / Validation Report

> **Branch:** `cursor/icp-standalone-validation-a88d`  
> **Base commit:** `f84e99e` (from `cursor/icp-standalone-gui-a88d`)  
> **Validation commit:** see `git log -1` on this branch  
> **Date:** 2026-08-31  
> **Scope:** Standalone ICP (ICP Core + Application API + GUI). **Not** CIC, MES, Designer, or new protocols.

---

## 1. Scope

Validate ICP as a **standalone industrial-connectivity product** against SoT, architecture docs, ICP-1A/1B, GUI architecture, ADRs, and existing tests.

**In scope:** Mock + software-protocol adapters via real local test peers; native PROFINET/PROFIBUS **software boundary only**.  
**Out of scope:** Hilscher CIFX hardware, physical PN IO-Devices, physical PROFIBUS slaves, CIC, MES, ICP Designer.

---

## 2. Tested software version / environment

| Item | Value |
| --- | --- |
| Repository | `virtual-factory` |
| Branch | `cursor/icp-standalone-validation-a88d` |
| OS | Ubuntu 24.04 (Cloud Agent VM) |
| Build | CMake Release, default Hilscher flags **OFF** |
| `ctest` | **17/17 passed** (includes new `icp_standalone_acceptance_test`) |
| GUI | Chromium manual exercise @ `http://127.0.0.1:8088` |
| API | cpp-httplib `/api/v1` via `icp_server` |

**External test peers used**

| Protocol | Peer | Role |
| --- | --- | --- |
| OPC UA | In-process `OpcUaTestServer` (open62541) | Real endpoint; also `opcua_adapter_test` |
| Modbus TCP | In-process `ModbusTestServer` (libmodbus) | Real TCP slave; also `modbus_adapter_test` |
| MQTT | Mosquitto (localhost) | `mqtt_adapter_test` |
| REST | In-process HTTP fixture | `rest_adapter_test` |
| EtherNet/IP | libplctag + local fixture | `eip_adapter_test` |
| Mock | `MockIndustrialAdapter` | Full-stack E2E |
| PROFINET/PB | None (software boundary) | Config + honest connect failure |

UAExpert / mosquitto_pub / curl: **not required** for automated acceptance; adapter-layer tests use equivalent in-process peers (documented below).

---

## 3. Requirements / acceptance matrix

| Requirement | Current implementation | How tested | Test method | Result | Evidence | Known limitation | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ICP starts without MES | `ApplicationService`, `icp_server` | Automated + manual | `icp_standalone_acceptance_test`, GUI | PASS | `mesDependency: false` in `/api/v1/status` | — | PASS |
| No CIC dependency | Application API only | Automated | status/diagnostics API | PASS | `cicDependency: false` | CIC not implemented (by design) | PASS |
| ICP-1A runtime | AdapterManager, PollScheduler, LiveStateCache | Existing + acceptance | `icp_runtime_test`, acceptance Mock E2E | PASS | ctest #10, #16 | No app-level auto-reconnect | PASS |
| ICP-1B config | Catalog, validator, JSON repo | Existing + acceptance | `icp_configuration_test`, acceptance config tests | PASS | ctest #14, #16 | No DB/vault | PASS |
| First-run missing config | Empty in-memory config, info event | Acceptance fix | `testMissingConfigFirstRun` | PASS | Event: "First run: no configuration file…" | User must Save to create file | PASS |
| Malformed config on load | Rejected, catalog unchanged | Acceptance | `testMalformedConfigRejected` | PASS | Load returns `ok: false` | — | PASS |
| Standalone GUI loads | `icp/gui` static SPA | Manual + API | Browser + GET `/` | PASS | Screenshots in manual run | Designer nav disabled | PASS |
| Dashboard real data | `/api/v1/status` | Automated + manual | API + GUI | PASS | Counts from backend | — | PASS |
| Adapter CRUD + lifecycle | ApplicationService + API | Automated + curl | `icp_application_api_test`, acceptance | PASS | Mock connect/disconnect | CONNECTING state N/A (not in architecture) | PASS |
| Mock full stack E2E | GUI→API→Service→Mock→Cache | Automated | acceptance + command API | PASS | Telemetry + start command | State/fault mapping on Mock config partial | PARTIAL |
| Equipment command path | `POST /equipment/{id}/command` | Acceptance | HTTP + ApplicationService | PASS | Start → RUNNING | GUI shows start/stop only | PASS |
| Comm fault ≠ machine fault | LiveStateCache + Mock | Acceptance | `testCommunicationVsMachineFault` | PASS | stale without machineFault | — | PASS |
| OPC UA real connectivity | OpcUaIndustrialAdapter | Adapter + ApplicationService | `opcua_adapter_test`, acceptance OPC UA | PASS | Real open62541 server | Not UAExpert manual in this run | PASS |
| Modbus real connectivity | ModbusIndustrialAdapter | Adapter + ApplicationService | `modbus_adapter_test`, acceptance Modbus | PASS | Real libmodbus slave | — | PASS |
| MQTT real connectivity | MqttIndustrialAdapter | Adapter layer | `mqtt_adapter_test` | PASS | Real Mosquitto | Not re-run through ApplicationService in acceptance | PARTIAL |
| REST real connectivity | RestIndustrialAdapter | Adapter layer | `rest_adapter_test` | PASS | Real HTTP fixture | Not re-run through ApplicationService in acceptance | PARTIAL |
| EtherNet/IP | libplctag adapter | Adapter layer | `eip_adapter_test` | PARTIAL | Real fixture in adapter test | No ApplicationService-path acceptance test | PARTIAL |
| PROFINET configure w/o HW | ICP-1B + factory | Automated | acceptance + API test | PASS | Validate/save OK | — | PASS |
| PROFINET connect w/o HW | Honest failure | Automated | acceptance | PASS | "Hilscher hardware not detected…" | No cyclic IO | BLOCKED BY HARDWARE |
| PROFIBUS configure w/o HW | ICP-1B + factory | Automated | API test | PASS | Save OK | — | PASS |
| PROFIBUS connect w/o HW | Honest failure | Automated | API test | PASS | Failure message honest | No DP cyclic | BLOCKED BY HARDWARE |
| Diagnostics honest Hilscher | readiness API | Manual + automated | GUI Diagnostics, `/diagnostics` | PASS | HARDWARE_NOT_AVAILABLE / SDK_MISSING | — | PASS |
| No secrets in API | redactCredentials | Automated | acceptance HTTP scan | PASS | No plaintext passwords | Auth/RBAC deferred | PASS |
| Restart persistence | JSON file | Automated | acceptance Mock E2E | PASS | Reload finds adapter | Connected state not restored (by design) | PASS |
| External peer recovery | Explicit reconnect | Adapter tests | opcua/modbus tests | PASS | Documented in adapter tests | No background auto-reconnect | PASS |
| Logs / events | Ring buffer + API | Automated | `/api/v1/events` | PASS | startup, config, connection events | Not a full historian | PASS |
| Mapping projection | `/api/v1/mappings` | Manual + API | GUI Mappings screen | PASS | Config fields shown | GUI read-only | PASS |
| Stability (extended) | Poll + HTTP | Short run | 256s full ctest + manual session | PARTIAL | No leak detected in short run | No 24h soak | NOT TESTED |
| Authentication | — | — | — | NOT APPLICABLE | — | Deferred (Phase 9) | NOT APPLICABLE |
| ICP Designer | — | — | — | NOT APPLICABLE | Nav disabled | ICP-1F not started | NOT APPLICABLE |

---

## 4. GUI validation

| Screen | Load | Console | Empty state | Populated state | Reload | Result | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Dashboard | OK | OK (favicon fixed) | OK | OK (after API workflow) | OK | **PASS** | Shows config load state |
| Adapters | OK | OK | OK | OK via API | OK | **PASS** | Editor opens below fold; scroll hint added |
| Equipment | OK | OK | OK | OK with connected Mock | OK | **PASS** | Start/Stop on detail page |
| Connections | OK | OK | OK | OK | OK | **PASS** | — |
| Configuration | OK | OK | OK | OK | OK | **PASS** | Validate/save/load/import |
| Mappings | OK | OK | OK | OK with config | OK | **PASS** | Read-only |
| Diagnostics | OK | OK | OK | OK | OK | **PASS** | Hilscher honest |
| Logs / Events | OK | OK | OK | OK | OK | **PASS** | First-run info not error |
| Settings | OK | OK | OK | OK | OK | **PASS** | Shows config state |

**Configuration startup scenarios**

| Scenario | Expected | Result |
| --- | --- | --- |
| No configuration file | First-run empty config, **info** event | **PASS** (after validation fix) |
| Empty valid configuration | Load/save OK | **PASS** |
| Valid configuration with adapters | Load + persist | **PASS** |
| Malformed JSON | Safe error, no crash | **PASS** |
| Invalid configuration (validation) | Rejected on save/apply | **PASS** (`icp_configuration_test`) |

---

## 5. API validation

All documented `/api/v1` endpoints exercised in `icp_standalone_acceptance_test` and `icp_application_api_test`.

| Check | Result |
| --- | --- |
| Valid requests | PASS |
| Malformed JSON → 400 | PASS |
| Missing adapter → 404 | PASS |
| Consistent `{ok, message, issues}` | PASS |
| No password/token leakage | PASS |
| Equipment command endpoint | PASS (added in validation) |

---

## 6. Mock validation (full stack)

```
GUI → HTTP API → ApplicationService → ICP-1B config → AdapterManager
  → MockIndustrialAdapter → GenericEquipment → LiveStateCache → GUI
```

| Step | Result |
| --- | --- |
| Create adapter | PASS |
| Validate / save / load | PASS |
| Connect | PASS |
| Live telemetry | PASS |
| Command (start) | PASS |
| Machine state RUNNING | PASS |
| Disconnect / stale | PASS |
| Remove adapter | PASS |
| Dashboard counters | PASS (via `/status`) |

---

## 7–11. Protocol validation summary

| Protocol | ApplicationService path | Adapter-layer tests | Real peer | Result |
| --- | --- | --- | --- | --- |
| **OPC UA** | YES (acceptance) | `opcua_adapter_test` | open62541 in-process | **PASS** |
| **Modbus TCP** | YES (acceptance) | `modbus_adapter_test` | libmodbus slave | **PASS** |
| **MQTT** | NOT in acceptance | `mqtt_adapter_test` | Mosquitto | **PASS** (adapter) |
| **REST** | NOT in acceptance | `rest_adapter_test` | HTTP fixture | **PASS** (adapter) |
| **EtherNet/IP** | NOT in acceptance | `eip_adapter_test` | libplctag fixture | **PARTIAL** |
| **Mock** | YES | `industrial_adapter_test`, acceptance | In-process | **PASS** |

---

## 12–13. PROFINET / PROFIBUS (software boundary)

| Check | PROFINET | PROFIBUS |
| --- | --- | --- |
| Configure without hardware | PASS | PASS |
| Validate / save | PASS | PASS |
| Adapter factory integration | PASS | PASS |
| Connect without hardware | FAIL (honest) | FAIL (honest) |
| Never shows CONNECTED/READY/cyclic IO | PASS | PASS |
| GUI configuration | PASS | PASS |
| GUI diagnostics | PASS | PASS |

**Status label (unchanged):** Native PROFINET / PROFIBUS — **SOFTWARE-INTEGRATION TESTED**, **HARDWARE VALIDATION PENDING**.

---

## 14–15. Configuration & mapping

Covered by `icp_configuration_test` (duplicate IDs, invalid protocol, plaintext password rejection, unknown fields, unsupported version, credential refs, restart persistence, PN/PB examples) and acceptance tests (missing/malformed/empty).

Mapping chain validated for Mock and OPC UA/Modbus via ApplicationService equipment snapshots.

---

## 16. Connection lifecycle

| Transition | Supported | Tested |
| --- | --- | --- |
| DISCONNECTED → CONNECTED | Explicit `connect()` | PASS |
| CONNECTED → DISCONNECTED | Explicit `disconnect()` | PASS |
| CONNECTED → FAULTED | Peer failure / comm loss | PASS (adapter + Mock) |
| FAULTED → CONNECTED | Explicit reconnect | PASS |
| CONNECTING | **Not in architecture** | NOT APPLICABLE |
| False CONNECTED (PN/PB no HW) | Must not occur | PASS |

---

## 17–18. Logs, events, diagnostics

Events recorded for startup, configuration (info on first-run), adapter upsert, connect/disconnect, command, validation errors.

Diagnostics distinguish SDK missing vs hardware missing vs communication vs machine fault.

---

## 19. Security audit

| Check | Result |
| --- | --- |
| Plaintext password in JSON import | Rejected (validator) |
| Password in API export | Not present |
| Bearer/token in responses | Not present |
| Credential refs (`env:`, `passwordRef`) | Supported in model |
| Browser localStorage secrets | None found |
| Hard-coded production passwords | None found |
| Authentication / RBAC | **Deferred** — documented limitation |

---

## 20–21. Restart / recovery

| Scenario | Result |
| --- | --- |
| Save → stop → start → load | PASS |
| Adapters/equipment in config restored | PASS |
| Runtime connected state restored | **No** (by design — explicit connect) |
| External server stop → fault/stale | PASS (adapter tests) |
| External server return → explicit reconnect | PASS (adapter tests) |

---

## 22. Stability

| Check | Result |
| --- | --- |
| Full ctest (~257s) | PASS |
| Manual GUI session (~15 min) | PASS |
| Long-duration soak (hours) | NOT TESTED |
| Memory leak hunt | NOT TESTED |

---

## 23. GUI UX acceptance

Terminology and status badges are distinct (CONNECTED, **SIMULATED ACTIVE** for mock, FAULTED, STALE, HARDWARE_NOT_AVAILABLE, SDK_MISSING). Validation messages come from backend. Destructive remove requires confirmation.

**Manual-test defect fixes (2026-08-31, post-validation):**

| Area | Root cause | Fix | Verified |
| --- | --- | --- | --- |
| Protocol selector | ~2s polling called `render()` which rebuilt the Add Adapter form from `state._editingAdapter` without syncing DOM `#f-protocol` changes | `captureAdapterFormDraft()`, `applyProtocolChange()`, input listeners, pause poll while editor open | Headless smoke: all 8 protocols |
| Refresh button | Same stale re-render race; Configuration editor overwritten on poll | Serialized `render()` queue + generation guard; preserve config draft; refresh feedback flash | Headless smoke + API |
| Mock CONNECTED label | Canonical state remains `CONNECTED`; GUI showed raw enum | `connectionStateDisplay` → `SIMULATED_ACTIVE` for mock when connected | API + GUI smoke |
| icp-config.json first-run | Old binary/process/cwd confusion during manual test | Already fixed in validation branch; status API exposes path/state | `testMissingConfigFirstRun` |

Minor polish: favicon added; adapter editor scroll hint added; `.status.SIMULATED_ACTIVE` styling.

---

## 24. Automated tests added / extended

| Test | Purpose |
| --- | --- |
| `icp_standalone_acceptance_test` | Config scenarios, Mock E2E, API security, OPC UA/Modbus via ApplicationService, PN software boundary, comm vs machine fault, **mock display semantics**, **multi-protocol coexistence** |
| `tests/gui_protocol_selector_smoke.mjs` | Headless Chromium regression: protocol selection sticks, refresh re-render, poll does not wipe draft, mock `SIMULATED ACTIVE` badge |
| Fixes in `JsonFileConfigurationRepository`, `ConfigurationCatalog`, `ApplicationService`, `HttpApiServer`, GUI | First-run config, command API, UX, protocol/refresh/mock semantics |

**ctest:** **17/17 passed** (full suite ~280s).

**GUI smoke:** `node tests/gui_protocol_selector_smoke.mjs http://127.0.0.1:8090` — **PASS** (requires running `icp_server`).

---

## 25. Fixed defects (validation branch)

| ID | Issue | Fix |
| --- | --- | --- |
| V-001 | Missing config file logged as **error** | ENOENT → empty config, **info** first-run event |
| V-002 | First-run message lost after catalog validate | Preserve repository message in `ConfigurationCatalog::load` |
| V-003 | No HTTP path for equipment commands | `POST /api/v1/equipment/{id}/command` |
| V-004 | GUI could not exercise commands | Equipment detail Start/Stop buttons |
| V-005 | Settings showed config state "unknown" | Expose `configurationLoaded` / `configurationLoadState` in status API |
| V-006 | favicon 404 | Added `assets/favicon.svg` |
| V-007 | Add Adapter editor easy to miss | Scroll + flash hint |
| V-008 | Protocol dropdown selection did not stick (poll/re-render wiped DOM) | Draft capture, protocol change handler, poll pause on Adapters editor |
| V-009 | Refresh appeared broken / overwrote unsaved editors | Serialized render queue, config draft preservation, refresh flash |
| V-010 | Mock connect showed plain CONNECTED | `connectionStateDisplay` = `SIMULATED_ACTIVE` (canonical state unchanged) |
| V-011 | Concurrent async `render()` races lost save results | Render chain + generation invalidation on save |
| V-012 | Multi-protocol adapter persistence untested at acceptance layer | `testMultiProtocolAdapterCoexistence()` |

---

## 26. Remaining defects / limitations

| ID | Severity | Description |
| --- | --- | --- |
| L-001 | Low | MQTT/REST/EIP not exercised through ApplicationService in acceptance test (adapter tests cover peers) |
| L-002 | Low | No authentication on Application API (expected deferral) |
| L-003 | Low | GUI protocol forms use JSON editor for equipment — functional but not full dynamic forms for all protocols |
| L-004 | Info | CONNECTING state not part of `ConnectionState` enum |
| L-005 | Info | Native PN/PB hardware validation blocked until CIFX arrives |
| L-006 | Info | ICP Designer not implemented (nav disabled) |

No **critical** open defects after validation fixes and rebuilt `icp_server`.

---

## 27. Hardware-blocked tests

- Native PROFINET cyclic IO / DCP / AR with real IO-Device  
- Native PROFIBUS DP cyclic with real slaves  
- Hilscher firmware/license operational validation on card  

---

## 28. Product acceptance gate

| Gate | Verdict |
| --- | --- |
| **A. ICP standalone software** | **ACCEPTED** — suitable foundation for next phase, with documented limitations |
| **B. GUI** | **ACCEPTED** — protocol selector + refresh verified (headless smoke + API); Designer explicitly out of scope |
| **C. API** | **ACCEPTED** — `/api/v1` complete for current scope |
| **D. Existing protocol adapters** | **PARTIAL ACCEPTED** — Mock/OPC UA/Modbus fully validated through ApplicationService; MQTT/REST/EIP validated at adapter layer |
| **E. Native PROFINET** | **SOFTWARE ACCEPTED / HARDWARE VALIDATION PENDING** |
| **F. Native PROFIBUS** | **SOFTWARE ACCEPTED / HARDWARE VALIDATION PENDING** |

**Overall:** ICP standalone product is **functionally solid** for software-protocol operation and configuration-first native fieldbus, and is **ready to serve as the foundation** for the next approved milestone (recommended: **Hilscher hardware validation** or **ICP-1E packaging** — not CIC/MES without explicit approval).

---

## 29. Git / merge status

- **Branch:** `cursor/icp-standalone-validation-a88d`  
- **Base:** `cursor/icp-standalone-gui-a88d` @ `f84e99e`  
- **master:** unchanged @ `d3e557b`  
- **PR #10:** not merged  
- **Hilscher branch:** not modified  

---

## 30. Recommended next milestone

1. **Hilscher CIFX hardware validation** when hardware arrives (native PN/PB physical gate).  
2. **ICP-1E** standalone deployable package (Windows/Linux/Docker).  
3. **ICP-1C (CIC)** only after explicit product approval — not inferred from this acceptance.

**Do not start MES or ICP Designer** without separate approval.
