# Architecture Decisions

## ADR-001 — Gazebo represents the physical factory

Gazebo is used as the virtual physical plant.

It simulates:

- machines
- conveyors
- products
- sensors
- physical interactions

Gazebo does not act as the PLC, SCADA, or MES.

---

## ADR-002 — C++ is the primary simulation/integration language

C++ is used for Gazebo systems and industrial integration components.

The goal is to understand and control the underlying simulation and industrial interfaces rather than relying on a large opaque framework.

---

## ADR-003 — PLC remains separate from Gazebo

The PLC is treated as the machine controller.

Gazebo represents the plant.

This separation allows the same control concepts to eventually be used with real industrial equipment.

---

## ADR-004 — Industrial protocols are separated from machine logic

Machine logic should not directly depend on Modbus TCP or OPC UA.

An abstraction layer will separate machine behavior from communication protocols.

---

## ADR-005 — OPC UA is the main information-model interface

OPC UA will eventually expose structured information about:

- equipment
- states
- modes
- faults
- measurements
- commands
- production data

open62541 will initially provide the OPC UA implementation.

---

## ADR-006 — UAExpert is a diagnostic tool

UAExpert will be used to verify and debug OPC UA communication.

It is not part of the core factory architecture.

---

## ADR-007 — SCADA and MES have different responsibilities

SCADA is responsible for operator monitoring and control.

MES is responsible for manufacturing management and production information.

They must not be treated as interchangeable systems.

---

## ADR-008 — Incremental implementation

The project will be implemented one verified layer at a time.

A phase must pass its verification gate before the next major layer is introduced.

---

## ADR-009 — Avoid unnecessary dependencies

No framework or middleware should be introduced merely because it is available.

A technology should be introduced when it solves a specific project requirement.

---

## ADR-010 — Virtual factory should generate realistic manufacturing events

The ultimate value of the simulation is not visual appearance.

The virtual factory should generate realistic:

- production events
- machine states
- faults
- downtime
- quality results
- product movement
- traceability information

for testing the MES.
