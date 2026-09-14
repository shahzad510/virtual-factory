# Native fieldbus ICP-1B example configurations

**Branch:** `cursor/icp-hilscher-native-development-a88d` (isolated)  
**Status:** placeholders for hardware validation. **Not** production plant configs.

These JSON documents use the existing ICP-1B schema (`virtual-factory.icp.config`). They load **without** Hilscher hardware. Live bus behavior remains **HARDWARE VALIDATION PENDING**.

| File | Topology |
| --- | --- |
| `profinet-single-iodevice.json` | one PROFINET controller + one IO-Device |
| `profinet-multi-iodevice.json` | one PROFINET controller + multiple IO-Devices |
| `profibus-single-slave.json` | one PROFIBUS master + one DP slave |
| `profibus-multi-slave.json` | one PROFIBUS master + multiple DP slaves |

## Placeholders (replace before plant use)

- `boardId` / `channel` — match `xDriverEnumBoards` / aliases (`cifx0`, …)
- `configArtifactPath` — SYCON.net / Communication Studio export (`.nxd`)
- `stationName` / `ipAddress` / `vendorId` / `deviceId` — from GSDML
- `stationAddress` / module `ident` — from GSD
- process-image offsets — match the engineered process image

Do **not** hard-code Siemens, Allen-Bradley, or other brand-specific PLC classes. Equipment remains `GenericEquipment` via type strings such as `remote_io` / `drive`.

Do **not** store passwords or license keys in these files.

## Load path

```text
JSON → ConfigurationCatalog / JsonFileConfigurationRepository
     → NativeFieldbusConfigMapper
     → ProfinetIndustrialAdapter / ProfibusIndustrialAdapter
     → AdapterManager / PollScheduler / LiveStateCache
```

## Honesty

Loading or validating these files is a **SOFTWARE-INTEGRATION** action. It does **not** prove native PROFINET or PROFIBUS on the wire.
