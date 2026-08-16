# Virtual Factory + MES

A virtual industrial manufacturing environment built for MES development, testing, and demonstration.

## Core Technologies

- Gazebo Sim
- C++
- CMake
- OpenPLC
- Modbus TCP
- open62541
- OPC UA
- UAExpert
- SCADA
- MES

## Project Goal

The project simulates a physical manufacturing environment and progressively connects it to industrial control, communication, SCADA, and MES layers.

The ultimate goal is to create a repeatable virtual factory capable of generating realistic manufacturing events for MES testing.

## Current Status

Stage 0 — Development foundation complete.

Next:

Stage 1 — First Gazebo physical simulation.

## First Milestone

The first physical simulation will contain:

```text
Product
   |
   v
Conveyor CV-001
   |
   v
Sensor SEN-001
