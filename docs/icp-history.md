# ICP Persistent Historical Data (Milestone 1)

**Status:** IMPLEMENTED / TESTED (software)  
**Baseline:** builds additively on `v0.1.0-icp-runtime-stable` (`320d79d`)  
**Not:** telemetry sampling historian, OEE engine, RBAC, MES/SCADA connectors

## Purpose

Persist operational facts across ICP restarts for later availability, downtime, alarm, command, and configuration analysis. Live protocol/session state is **not** restored from history.

## Architecture

```text
ApplicationService  --enqueue-->  AsyncHistoryWriter  -->  HistoryRepository
                                                              ├─ SqliteHistoryRepository (local file)
                                                              └─ NullHistoryRepository (open failure)
```

- Protocol adapters remain unaware of SQLite.
- History writes never run under adapter protocol I/O mutexes (async queue + worker thread).
- Opening the local DB never waits on remote services. Open failure → ICP still starts; historian reports unavailable/degraded.

Default DB path: `icp-history.sqlite` beside the configuration file (override with `--history` / `ICP_HISTORY_PATH`).

## Schema (version 1)

Tables: `history_event`, `communication_interval`, `health_transition`, `alarm_event`, `equipment_state_interval`, `command_audit`, `config_revision`, `schema_meta`.

UTC timestamps stored as epoch milliseconds. Indexed by timestamp and common (adapter/equipment) filters.

Secrets are never written (config revisions store fingerprint of exported JSON with credential **refs** only).

## HTTP API

`GET /api/v1/history?kind=...&start=&end=&adapterId=&equipmentId=&protocol=&category=&eventType=&severity=&limit=`

Kinds: `events`, `communication_intervals`, `health_transitions`, `alarm_events`, `equipment_state_intervals`, `command_audit`, `config_revisions`.

Response includes `historian` availability and `items`. Designed for later `history.view` RBAC (Milestone 2).

In-memory `GET /api/v1/events` remains for the current process ring buffer.

## Startup independence

ICP must start with no PLC, OPC UA, Modbus, MQTT, PROFINET, MES, SCADA, or remote DB available. Historian is local-only and optional for process liveness.

## Tests

`icp_history_test` — persistence across restart, chronological coexistence, no CONNECTED restore, unavailable DB, HTTP history query.
