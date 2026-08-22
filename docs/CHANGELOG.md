# Changelog

Meaningful engineering changes for the Virtual Factory project.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

### Added
### Changed
### Fixed
### Documentation
### Architecture

---

## 2026-08-22

### Added

- PRODUCT-001 discovery through the Gazebo ECM `Name` component and kinematic +X pose integration while the conveyor is running (`ConveyorSystem`).
- Heartbeat fields `dt` (seconds) and `product_x` (metres) for runtime diagnosis.
- Documentation set: `docs/README.md`, `docs/CHANGELOG.md`; SoT PDF placed under `docs/`.

### Changed

- Official implementation numbering is SoT Phases 1–11 (ADR-015). Retired competing Stage 0–25 / handoff sequences as live phase labels.
- `docs/architecture.md`, `docs/decisions.md`, `docs/roadmap.md`, `docs/implementation-status.md`, and root `README.md` rewritten to a single authority model.

### Fixed

- Product motion used `_info.dt.count()` (nanosecond ticks). Now `std::chrono::duration<double>(_info.dt).count()` (seconds). Headless verification: `dt=0.001 s`; 4.0 s at 0.5 m/s moved PRODUCT-001 from −1.5 m to 0.5 m.

### Documentation

- Established Levels 1–7: SoT PDF → implementation-status → architecture → decisions → roadmap → changelog → docs index.
- Discarded stale Stage 0 README content and the old gateway-only architecture sketch as live architecture.

### Architecture

- No SoT change. Markdown now matches the existing SoT (adapter layer, REST fallback, .NET/Blazor GUI, Gazebo-as-plant). PDF not regenerated.

---

## 2026-08-19

### Added

- PRODUCT-001 model and world include (`feat: add product to conveyor world`).
- Belt linear velocity command on the `belt` link.

---

## 2026-08-18

### Added

- Conveyor Start/Stop/SetSpeed and development update 1000/5000 control.
- PreUpdate heartbeat.

### Changed

- Factory visual materials.

---

## 2026-08-17

### Added

- Static conveyor CV-001.
- `ConveyorSystem` plugin loaded from `libConveyorSystem.so`.

---

## 2026-08-16

### Added

- Repository, initial documentation, Gazebo world and factory floor.
