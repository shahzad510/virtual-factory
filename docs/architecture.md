# Virtual Factory + MES Architecture

## 1. Project Purpose

This project creates a virtual industrial factory that can be used as a development, testing, demonstration, and simulation environment for a Manufacturing Execution System (MES).

The physical factory is represented by Gazebo.

The control and industrial communication layers are developed separately so that the architecture can eventually move from a virtual factory to real industrial equipment.

---

## 2. High-Level Architecture

```text
                         MES
                          |
                   Production Data
                          |
                    SCADA / HMI
                          |
                       OPC UA
                          |
                C++ Industrial Gateway
                    /           \
              Modbus TCP        OPC UA
                  /                \
                PLC             Clients
                  |
             Machine I/O
                  |
                Gazebo
                  |
       +----------+----------+
       |          |          |
   Conveyors    Robots    Machines
       |          |          |
       +----------+----------+
                  |
               Sensors
