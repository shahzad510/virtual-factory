# ICP Standalone GUI Architecture

> Industrial Connectivity Platform as a **standalone commercial product** UI.
> Not MES. Not CIC. Not a Hilscher-only frontend.

**Status:** **IMPLEMENTED** / **TESTED** (Mock + Application API; native PN/PB hardware validation remains deferred).

---

## 1. Technology selection

| Choice | Decision |
| --- | --- |
| **GUI** | Vanilla HTML/CSS/JS SPA served as static files (`icp/gui/`) |
| **API** | C++ HTTP JSON Application API (`/api/v1/...`) via **cpp-httplib** (MIT) |
| **Live updates** | Client **polling** (~2s) against `/api/v1/*` |
| **Config** | Existing ICP-1B `ConfigurationCatalog` / JSON repository |

### Why this stack

- Runs on **Windows 10/11/Server**, **Ubuntu 24.04**, and **Docker** via any modern browser + ICP process.
- No Next.js / heavy SPA framework — smallest maintainable product for this milestone.
- Same process hosts API + GUI; deployment is one `icp_server` binary + static `icp/gui` tree.
- Fits industrial desktop primary use (1280×720+).
- Leaves room for a future Designer without rewriting routing/API boundaries.

### Alternatives considered

| Alternative | Why not now |
| --- | --- |
| Next.js / React | Unnecessary framework weight for first product GUI |
| .NET / Blazor (ADR-011 long-term hint) | Cross-cutting runtime not required for ICP Core (C++); deferred |
| Qt / native desktop | Harder Docker/Windows shared packaging for this milestone |
| WebSocket/SSE | Polling is enough given PollScheduler + LiveStateCache |

### Deployment model

```
Browser  →  ICP GUI (static) + Application API (/api/v1)  →  ICP Core
                                              ↓
                                    Industrial adapters
                         (Mock, OPC UA, Modbus, MQTT, REST, EIP,
                          optional Hilscher PN/PB)
```

Native Hilscher CIFX remains **host-level** (not claimed inside Docker Desktop Windows for fieldbus).

---

## 2. Architecture

```
                 ICP
                  │
       ┌──────────┴──────────┐
       │                     │
   ICP CORE              ICP GUI
       │                     │
       │                 HTTP/API
       │                     │
       └──────────┬──────────┘
                  │
            Application API
                  │
        ┌─────────┼─────────┐
        │         │         │
     Runtime  Configuration  Diagnostics
```

### Components

| Component | Path | Role |
| --- | --- | --- |
| `ApplicationService` | `icp/src/app/` | Facade: catalog, AdapterManager, LiveStateCache, PollScheduler, events |
| `HttpApiServer` | `icp/src/app/` | `/api/v1` + static GUI mount |
| `icp_server` | `icp/src/app/icp_server_main.cc` | Standalone process entry |
| GUI | `icp/gui/` | Operator/configuration SPA |

### Boundaries (GUI must never)

- Call Hilscher SDK
- Include `IndustrialAdapter` / `GenericEquipment` C++ types
- Read private filesystem layout beyond API export
- Invent telemetry or connection state

The **API is the boundary**.

---

## 3. Application API (`/api/v1`)

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/status` | Dashboard aggregates (real backend counts) |
| GET | `/protocols` | Protocol capability model |
| GET/PUT | `/configuration` | Full ICP-1B document |
| POST | `/configuration/validate\|save\|load\|import` | Catalog ops |
| GET | `/configuration/export` | JSON export (credential refs only) |
| GET/POST | `/adapters` | List / upsert |
| GET/PUT/DELETE | `/adapters/{id}` | View / edit / remove |
| POST | `/adapters/{id}/connect\|disconnect\|reconnect` | Lifecycle |
| GET | `/equipment` / `/equipment/{id}` | Live snapshots |
| GET | `/mappings` | Config mapping projection |
| GET | `/diagnostics` | Runtime + Hilscher readiness |
| GET | `/events` | Recent application events |
| GET | `/health` | Liveness |

Secrets: only **credential references** (`passwordRef`, `tokenRef`). No passwords, tokens, license keys, or SDK internals.

This API is **ICP’s own GUI Application API**. It is **not** CIC (ICP-1C).

---

## 4. Screens

| Screen | Behavior |
| --- | --- |
| Dashboard | Real adapter/equipment/event stats; empty state → Add Adapter |
| Adapters | Add/Edit/Remove/Connect/Disconnect/Reconnect; protocol from capability API |
| Equipment | List + detail; **communication** vs **machine** fault distinguished |
| Connections | Lifecycle view |
| Configuration | Validate/Save/Load/Import/Export ICP-1B JSON |
| Mappings | Read-only projection of catalog mappings |
| Diagnostics | Runtime + Hilscher hardware readiness (`NOT DETECTED` when no card) |
| Logs / Events | Event ring buffer |
| Settings | Product/API/path metadata |
| Designer | Nav disabled — **not implemented** |

---

## 5. Configuration workflow

1. **Add Industrial Adapter** opens a protocol chooser (industrial wording; status badges reflect runtime reality: Supported / Gateway / Coming Soon).
2. **Modbus** opens a transport step: Modbus TCP (Supported) or Modbus RTU / RS-485 (Coming Soon — disabled; no adapter created). RTU/RS-485 is **not** implemented in the backend.
3. **PROFINET / PROFIBUS** open a Gateway Integration step. Gateway configures a northbound gateway endpoint (OPC UA by default). Native Hilscher/Softing remain Coming Soon in the GUI. Live communication for fieldbus via gateway uses a Supported OPC UA / Modbus / MQTT / REST adapter to that gateway.
4. Editor connection fields expose only parameters the runtime uses (for example OPC UA shows endpoint URL only; unused TLS/timeout controls are not shown).
5. Backend `ConfigurationValidator` is authoritative.
6. Save → `JsonFileConfigurationRepository` versioned JSON.
7. Restart ICP → Load → same document.
8. PROFINET/PROFIBUS native records: **Configure → Validate → Save** without hardware when that path is enabled.
9. Connect without hardware on Hilscher native → clear error: `Hilscher hardware not detected. …`

---

## 6. Mock end-to-end

Add Mock → Configure → Validate → Save → Connect → Equipment + telemetry → Disconnect → Reload persists.

Does **not** claim PROFINET/PROFIBUS support.

---

## 7. Platforms

| Platform | Support |
| --- | --- |
| Ubuntu 24.04 | Build `icp_server`; open `http://127.0.0.1:8080` |
| Windows 10/11/Server | Same browser GUI; build ICP Core with MSVC/Clang as existing CMake allows |
| Docker | Ship API+GUI container for software protocols; **do not claim** Docker Desktop Windows supports native CIFX fieldbus |

Run:

```bash
./icp_server --host 0.0.0.0 --port 8080 --config icp-config.json --gui-root /path/to/icp/gui
```

Env: `ICP_BIND_HOST`, `ICP_CONFIG_PATH`, `ICP_GUI_ROOT`.

---

## 8. Future Designer

Keep SPA hash routes + `/api/v1` stable. Designer can add a route and canvas module later without replacing the Application API. Drag-and-drop is **out of scope** for this milestone.

---

## 9. Security notes

- Credential refs only (`env:`, `file:`, `secret:` naming in ICP-1B).
- No hard-coded production passwords.
- API responses redact credential material beyond refs.
- Auth/RBAC remains a later product phase (not required for standalone lab GUI).

---

## 10. Tests

- `icp_application_api_test` — Mock persistence/lifecycle, HTTP API, GUI index load, PN/PB configure-without-hardware, Hilscher hardware honesty.
- Existing suite must remain green (`ctest`).
