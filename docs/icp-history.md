# ICP Persistent Historical Data (Milestone 1 / 1.1)

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
- GUI actions never delete historical records.

Default DB path: `icp-history.sqlite` beside the configuration file (override with `--history` / `ICP_HISTORY_PATH`).

## Schema (version 2)

Tables: `history_event`, `communication_interval`, `health_transition`, `alarm_occurrence`, `alarm_event`, `equipment_state_interval`, `command_audit`, `config_revision`, `schema_meta`.

- `alarm_occurrence` — one historical incident/occurrence (raised/ack/cleared times + status).
- `alarm_event` — lifecycle actions (`raised` | `acknowledged` | `cleared`) linked by `occurrence_id`.
- Stable alarm identity `alarm_key` = `sourceType:sourceId:category` (message excluded).
- Repeated incidents create **new** occurrence rows; poll cycles do not duplicate an open identity.
- UTC timestamps stored as epoch milliseconds (`ts_utc_ms` / `raised_at_utc_ms`, etc.). Millisecond precision retained.

Secrets are never written (config revisions store fingerprint of exported JSON with credential **refs** only).

## HTTP API

`GET /api/v1/history?kind=...&startUtcMs=&endUtcMs=&adapterId=&equipmentId=&protocol=&category=&eventType=&severity=&status=&limit=&offset=`

Kinds: `events`, `communication_intervals`, `health_transitions`, `alarm_events`, `alarm_occurrences`, `equipment_state_intervals`, `command_audit`, `config_revisions`.

Response includes `historian`, `items`, `limit`, `offset`, `returned`, `truncated`, and ISO-8601 UTC companions (e.g. `tsUtc` / `raisedAtUtc`) alongside epoch-ms fields.

`POST /api/v1/alarms/occurrences/{id}/acknowledge` — historical acknowledgement (never deletes).

`GET /api/v1/history/export?...` — CSV export only (read-only; does not alter SQLite).

In-memory `GET /api/v1/events` remains for the current process ring buffer.

## GUI

Separate views: **Active Alarms**, **Alarm History**, **Event History**. Event History reads persistent `/api/v1/history` (survives refresh/restart). Filters + pagination + CSV export. Timestamps shown in local human-readable form.

## Startup independence

ICP must start with no PLC, OPC UA, Modbus, MQTT, PROFINET, MES, SCADA, or remote DB available. Historian is local-only and optional for process liveness.

## Tests

`icp_history_test` — alarm raise/ack/clear/occurrence separation, no duplicate raise for same occurrence id, event+alarm survival across restart, ISO timestamps, CSV non-mutation, unavailable DB, startup independence.
