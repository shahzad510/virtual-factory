# ICP configuration (ICP-1B)

**Status:** **IMPLEMENTED** / **TESTED** (backend configuration subsystem).  
**ICP Designer:** **NOT STARTED**.  
**CIC:** **NOT STARTED**.  
**MES:** **NOT STARTED**.

This document describes the persistent configuration model owned by the **Industrial Connectivity Platform (ICP)**. It is the backend foundation for the future **ICP Designer** GUI. It is **not** the Connectivity Integration Contract (CIC) and it is **not** an MES configuration store.

---

## 1. Ownership

ICP owns:

- industrial connections
- adapter configuration
- equipment mappings
- live industrial state (ICP-1A cache)
- industrial communication health

MES Core later consumes **normalized** industrial facts through **CIC** only. Configuration files must never contain work orders, scheduling, OEE, materials, plant hierarchy as a MES resource tree, or other MES concepts.

There is **no** C++ dependency from ICP to MES.

---

## 2. Designer-ready API (no GUI in this slice)

The in-memory workspace is `virtual_factory::icp::ConfigurationCatalog`. Persistence is `ConfigurationRepository` (file backend today).

Intended Designer flow (ICP-1F will call the same API):

```text
ADD → CONFIGURE → VALIDATE → CONNECT → DEPLOY → MONITOR
```

ICP-1B implements **ADD / CONFIGURE / VALIDATE / SAVE / LOAD**.  
**CONNECT** remains ICP-1A (`AdapterFactory` + `AdapterManager`).  
**DEPLOY / MONITOR / GUI canvas** are **NOT STARTED**.

---

## 3. Document format

Human-readable **versioned JSON**. No database in ICP-1B (ADR-047). The repository interface is replaceable if a database is required later.

| Field | Meaning |
| --- | --- |
| `schema` | Must be `virtual-factory.icp.config` |
| `version` | Integer. Current: **1** |
| `name` | Optional plant/workspace label |
| `adapters[]` | Protocol-oriented adapter records |

Deterministic serialization: objects are written with a fixed key set (nlohmann `json` / `std::map` key order) and 2-space indent plus a trailing newline.

Atomic save: write `path.tmp`, `fsync`, `rename` over the destination. A failed save must leave the previous file intact.

Unknown fields are **rejected** (no silent data loss). Corrupt JSON fails with an explicit `corrupt configuration` error.

### Migration

- Version **1**: identity (no transform).
- Version **< 1**: fail — `no migration path`.
- Version **> 1**: fail — `unsupported configuration version`.

Future versions add sequential `vN → vN+1` migrators. Do not load newer files with an older ICP.

---

## 4. Configuration model

Protocol-neutral records (not Siemens/Allen-Bradley/Pump/Robot C++ types):

| Record | Role |
| --- | --- |
| `AdapterConfigRecord` | `adapterId`, `protocol`, `enabled`, connection, credential **references**, equipment mappings, metadata |
| `EquipmentMappingRecord` | `equipmentId`, optional `adapterId`, type, telemetry/command/state/fault mappings, protocol-specific mapping data |
| `ProfinetSubmoduleRecord` | Future native IO: slot / subslot / process-data lengths |
| `ProfibusModuleRecord` | Future native IO: slot / ident / process-data lengths |

Supported `protocol` values: `mock`, `opcua`, `modbus`, `mqtt`, `rest`, `ethernetip`, `profinet`, `profibus`.

Loading a file **does not** require the Hilscher SDK. Native PROFINET/PROFIBUS fields are configuration data only. Plant IO remains **HARDWARE VALIDATION PENDING**. ICP-1B does **not** implement native fieldbus communication.

### PROFINET fields (model only)

Controller/interface: `boardId`, `channel`, `interfaceName`, `configArtifactPath`.  
Device: `stationName`, `ipAddress`, `vendorId`, `deviceId`, `submodules[]`.  
Mappings: telemetry / commands / state / fault (byte/bit offsets).

### PROFIBUS fields (model only)

Master/interface: `boardId`, `channel`, `masterAddress`, `baudRateKbps`, `configArtifactPath`.  
Slave: `stationAddress` (1–126), `modules[]`.  
Mappings: telemetry / commands / state / fault.

---

## 5. Validation

`ConfigurationValidator` reports JSON-pointer-like paths and actionable messages for:

- duplicate `adapterId`
- duplicate `equipmentId` (per adapter and globally)
- invalid protocol
- missing required connection parameters
- invalid mappings (empty names, missing protocol addresses)
- equipment `adapterId` that does not match the parent adapter
- plaintext credential values in `passwordRef` / `tokenRef`
- malformed documents (parser)
- unsupported / unmigratable versions

Invalid documents are **not** written to disk.

---

## 6. Security boundary

ICP-1B does **not** implement a secret vault.

- Store **references only**: `env:NAME`, `file:PATH`, `secret:NAME`.
- JSON keys `password`, `token`, `bearerToken`, `secret`, `passwd` are rejected.
- Usernames may be stored (identity, not a secret).
- Do not log passwords, tokens, or secret values (this subsystem does not log credentials).
- Resolving `env:` / `file:` / `secret:` at connect-time is **not** implemented here (future runtime / Designer deploy).

Phase 9 remains the home for RBAC and production secret management.

---

## 7. Example (trimmed)

```json
{
  "schema": "virtual-factory.icp.config",
  "version": 1,
  "name": "demo-plant",
  "adapters": [
    {
      "adapterId": "opcua-line1",
      "protocol": "opcua",
      "enabled": true,
      "connection": {
        "endpointUrl": "opc.tcp://127.0.0.1:4840",
        "host": "127.0.0.1",
        "port": 4840
      },
      "equipment": [
        {
          "equipmentId": "MIXER-001",
          "type": "mixer",
          "telemetry": [
            {"name": "speed", "address": "ns=1;s=Mixer.SpeedActual", "unit": "rpm"}
          ]
        }
      ]
    }
  ]
}
```

---

## 8. What ICP-1B does not do

- ICP Designer GUI
- CIC northbound API
- Connecting/deploying adapters from the configuration file automatically
- Native PROFINET/PROFIBUS protocol stacks
- Database persistence
- MES configuration
