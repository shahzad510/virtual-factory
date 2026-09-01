/**
 * ICP adapter editor helpers — protocol-specific fields aligned with
 * ConfigurationModel / ConfigurationValidator (ICP-1B). Browser + Node.
 */
(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
  } else {
    root.IcpAdapterForm = factory();
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  const PROTOCOL_LABELS = {
    mock: "Mock",
    opcua: "OPC UA",
    modbus: "Modbus TCP",
    mqtt: "MQTT",
    rest: "REST",
    ethernetip: "EtherNet/IP",
    profinet: "PROFINET",
    profibus: "PROFIBUS",
  };

  /** Connection fields required/useful per protocol (validator-aligned). */
  const CONNECTION_FIELDS = {
    mock: [],
    opcua: [
      { key: "endpointUrl", label: "Endpoint URL", type: "text", required: true, placeholder: "opc.tcp://host:4840" },
      { key: "timeoutMs", label: "Timeout (ms)", type: "number" },
      { key: "useTls", label: "Use TLS", type: "bool" },
    ],
    modbus: [
      { key: "host", label: "Host", type: "text", required: true },
      { key: "port", label: "Port", type: "number", required: true },
      { key: "timeoutMs", label: "Timeout (ms)", type: "number" },
    ],
    mqtt: [
      { key: "host", label: "Broker host", type: "text", required: true },
      { key: "port", label: "Port", type: "number", required: true },
      { key: "clientId", label: "Client ID", type: "text" },
      { key: "keepaliveSeconds", label: "Keepalive (s)", type: "number" },
      { key: "useTls", label: "Use TLS", type: "bool" },
    ],
    rest: [
      { key: "scheme", label: "Scheme", type: "text", required: true, placeholder: "http" },
      { key: "host", label: "Host", type: "text", required: true },
      { key: "port", label: "Port", type: "number", required: true },
      { key: "basePath", label: "Base path", type: "text" },
      { key: "timeoutMs", label: "Timeout (ms)", type: "number" },
    ],
    ethernetip: [
      { key: "host", label: "PLC host", type: "text", required: true },
      { key: "port", label: "Port", type: "number", required: true, placeholder: "44818" },
      { key: "path", label: "CIP path", type: "text", placeholder: "1,0" },
      { key: "plcType", label: "PLC type", type: "text", placeholder: "controllogix" },
      { key: "timeoutMs", label: "Timeout (ms)", type: "number" },
    ],
    profinet: [
      { key: "boardId", label: "Board ID", type: "text", required: true, placeholder: "cifx0" },
      { key: "channel", label: "Channel", type: "number" },
      { key: "interfaceName", label: "Interface", type: "text" },
      { key: "stationName", label: "Controller station name", type: "text" },
      { key: "processImageBytes", label: "Process image (bytes)", type: "number" },
    ],
    profibus: [
      { key: "boardId", label: "Board ID", type: "text", required: true, placeholder: "cifx0" },
      { key: "channel", label: "Channel", type: "number" },
      { key: "masterAddress", label: "Master address", type: "number" },
      { key: "baudRateKbps", label: "Baud rate (kbps)", type: "number", required: true },
      { key: "processImageBytes", label: "Process image (bytes)", type: "number" },
    ],
  };

  /** Telemetry fields shown in GUI per protocol (validator-aligned). */
  const TELEMETRY_FIELDS = {
    mock: [
      { key: "name", label: "Name", required: true },
      { key: "unit", label: "Unit" },
    ],
    opcua: [
      { key: "name", label: "Name", required: true },
      { key: "unit", label: "Unit" },
      { key: "address", label: "NodeId", required: true, placeholder: "ns=1;s=Speed" },
      { key: "namespaceIndex", label: "Namespace index", type: "number" },
    ],
    modbus: [
      { key: "name", label: "Name", required: true },
      { key: "unit", label: "Unit" },
      { key: "table", label: "Table", placeholder: "holdingRegister" },
      { key: "registerAddress", label: "Register address", type: "number" },
      { key: "address", label: "Address (alt)", placeholder: "optional if table/register set" },
      { key: "unitId", label: "Unit ID", type: "number" },
    ],
    mqtt: [
      { key: "name", label: "Name", required: true },
      { key: "unit", label: "Unit" },
      { key: "address", label: "Topic", required: true, placeholder: "plant/telemetry" },
      { key: "qos", label: "QoS", type: "number" },
    ],
    rest: [
      { key: "name", label: "Name", required: true },
      { key: "unit", label: "Unit" },
      { key: "jsonPointer", label: "JSON pointer", placeholder: "/speed" },
      { key: "address", label: "Address (alt)", placeholder: "optional if jsonPointer set" },
    ],
    ethernetip: [
      { key: "name", label: "Name", required: true },
      { key: "unit", label: "Unit" },
      { key: "address", label: "Tag", required: true, placeholder: "Program:Main.MyTag" },
    ],
    profinet: [
      { key: "name", label: "Name", required: true },
      { key: "unit", label: "Unit" },
      { key: "inputByteOffset", label: "Input byte offset", type: "number" },
      { key: "bitOffset", label: "Bit offset", type: "number" },
      { key: "valueType", label: "Value type", placeholder: "UINT8" },
    ],
    profibus: [
      { key: "name", label: "Name", required: true },
      { key: "unit", label: "Unit" },
      { key: "inputByteOffset", label: "Input byte offset", type: "number" },
      { key: "bitOffset", label: "Bit offset", type: "number" },
      { key: "valueType", label: "Value type", placeholder: "UINT8" },
    ],
  };

  const COMMAND_FIELDS = {
    mock: [{ key: "command", label: "Command", required: true }],
    opcua: [
      { key: "command", label: "Command", required: true },
      { key: "address", label: "NodeId" },
      { key: "namespaceIndex", label: "Namespace index", type: "number" },
    ],
    modbus: [
      { key: "command", label: "Command", required: true },
      { key: "table", label: "Table" },
      { key: "registerAddress", label: "Register address", type: "number" },
      { key: "address", label: "Address (alt)" },
    ],
    mqtt: [
      { key: "command", label: "Command", required: true },
      { key: "address", label: "Topic" },
      { key: "qos", label: "QoS", type: "number" },
    ],
    rest: [
      { key: "command", label: "Command", required: true },
      { key: "address", label: "Path" },
      { key: "method", label: "Method", placeholder: "POST" },
      { key: "bodyTemplate", label: "Body template" },
    ],
    ethernetip: [
      { key: "command", label: "Command", required: true },
      { key: "address", label: "Tag" },
    ],
    profinet: [
      { key: "command", label: "Command", required: true },
      { key: "outputByteOffset", label: "Output byte offset", type: "number" },
      { key: "valueType", label: "Value type" },
    ],
    profibus: [
      { key: "command", label: "Command", required: true },
      { key: "outputByteOffset", label: "Output byte offset", type: "number" },
      { key: "valueType", label: "Value type" },
    ],
  };

  function clone(obj) {
    return JSON.parse(JSON.stringify(obj == null ? {} : obj));
  }

  function defaultAdapter(protocol, preferredId) {
    const id =
      preferredId || protocol + "-" + Date.now().toString(36).slice(-4);
    const base = {
      adapterId: id,
      protocol: protocol,
      enabled: true,
      description: "",
      connection: {},
      credentials: {},
      equipment: [],
    };
    if (protocol === "mock") {
      base.description = "Mock industrial source";
      base.equipment = [
        {
          equipmentId: id + "-Motor-01",
          type: "motor",
          capabilities: ["start", "stop"],
          telemetry: [
            { name: "speed", unit: "rpm" },
            { name: "temperature", unit: "C" },
            { name: "current", unit: "A" },
          ],
          commands: [{ command: "start" }, { command: "stop" }],
          state: { mapped: false },
          fault: { mapped: false },
        },
      ];
    } else if (protocol === "opcua") {
      base.connection = { endpointUrl: "opc.tcp://127.0.0.1:4840", timeoutMs: 2000 };
      base.equipment = [
        {
          equipmentId: id + "-Device-01",
          type: "device",
          capabilities: [],
          telemetry: [
            { name: "speed", unit: "rpm", address: "ns=1;s=Speed", namespaceIndex: 1 },
          ],
          commands: [],
          state: { mapped: false },
          fault: { mapped: false },
        },
      ];
    } else if (protocol === "modbus") {
      base.connection = { host: "127.0.0.1", port: 502, timeoutMs: 2000 };
      base.equipment = [
        {
          equipmentId: id + "-Device-01",
          type: "device",
          capabilities: [],
          telemetry: [
            {
              name: "register",
              unit: "",
              table: "holdingRegister",
              registerAddress: 1,
              unitId: 1,
            },
          ],
          commands: [],
          state: { mapped: false },
          fault: { mapped: false },
        },
      ];
    } else if (protocol === "mqtt") {
      base.connection = { host: "127.0.0.1", port: 1883, clientId: id };
      base.equipment = [
        {
          equipmentId: id + "-Device-01",
          type: "device",
          capabilities: [],
          telemetry: [{ name: "value", unit: "", address: "plant/telemetry", qos: 0 }],
          commands: [],
          state: { mapped: false },
          fault: { mapped: false },
        },
      ];
    } else if (protocol === "rest") {
      base.connection = {
        scheme: "http",
        host: "127.0.0.1",
        port: 8081,
        timeoutMs: 2000,
      };
      base.equipment = [
        {
          equipmentId: id + "-Device-01",
          type: "device",
          capabilities: [],
          telemetryPath: "/api/device",
          telemetry: [{ name: "speed", unit: "rpm", jsonPointer: "/speed" }],
          commands: [],
          state: { mapped: false },
          fault: { mapped: false },
        },
      ];
    } else if (protocol === "ethernetip") {
      base.connection = {
        host: "127.0.0.1",
        port: 44818,
        path: "1,0",
        plcType: "controllogix",
        timeoutMs: 2000,
      };
      base.equipment = [
        {
          equipmentId: id + "-Device-01",
          type: "device",
          capabilities: [],
          telemetry: [{ name: "tag", unit: "", address: "Program:Main.MyTag" }],
          commands: [],
          state: { mapped: false },
          fault: { mapped: false },
        },
      ];
    } else if (protocol === "profinet") {
      base.connection = {
        boardId: "cifx0",
        channel: 0,
        interfaceName: "eth0",
        stationName: "icp-controller",
        processImageBytes: 64,
      };
      base.equipment = [
        {
          equipmentId: id + "-IO-01",
          type: "io_device",
          stationName: "device-01",
          ipAddress: "192.168.0.10",
          vendorId: 0,
          deviceId: 0,
          capabilities: [],
          telemetry: [
            { name: "input0", inputByteOffset: 0, bitOffset: 0, valueType: "UINT8" },
          ],
          commands: [],
          state: { mapped: false },
          fault: { mapped: false },
          submodules: [{ slot: 0, subslot: 1, inputLength: 8, outputLength: 8 }],
        },
      ];
    } else if (protocol === "profibus") {
      base.connection = {
        boardId: "cifx0",
        channel: 0,
        masterAddress: 1,
        baudRateKbps: 1500,
        processImageBytes: 64,
      };
      base.equipment = [
        {
          equipmentId: id + "-Slave-01",
          type: "dp_slave",
          stationAddress: 3,
          capabilities: [],
          telemetry: [
            { name: "input0", inputByteOffset: 0, bitOffset: 0, valueType: "UINT8" },
          ],
          commands: [],
          state: { mapped: false },
          fault: { mapped: false },
          modules: [{ slot: 0, ident: "module-0", inputLength: 8, outputLength: 8 }],
        },
      ];
    }
    return base;
  }

  /**
   * Change protocol: keep identity fields; reset connection/equipment to protocol defaults.
   */
  function applyProtocolChange(prev, newProtocol) {
    if (!prev) {
      return defaultAdapter(newProtocol);
    }
    if (prev.protocol === newProtocol) {
      return prev;
    }
    const next = defaultAdapter(newProtocol, prev.adapterId);
    next.description = prev.description || next.description;
    next.enabled = prev.enabled !== false;
    next.credentials = clone(prev.credentials || {});
    next._edit = prev._edit;
    next._validationIssues = undefined;
    return next;
  }

  /** Strip adapter to ICP-1B JSON (no GUI-only keys). */
  function adapterToConfigJson(adapter) {
    const out = {
      adapterId: adapter.adapterId || "",
      protocol: adapter.protocol || "mock",
      enabled: adapter.enabled !== false,
      description: adapter.description || "",
      connection: clone(adapter.connection || {}),
      credentials: clone(adapter.credentials || {}),
      equipment: clone(adapter.equipment || []),
    };
    return out;
  }

  function parseAdapterJson(text) {
    const parsed = JSON.parse(text);
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
      throw new Error("Adapter JSON must be an object");
    }
    if (!parsed.protocol) {
      throw new Error("Adapter JSON requires protocol");
    }
    if (!CONNECTION_FIELDS[parsed.protocol]) {
      throw new Error("Unsupported protocol: " + parsed.protocol);
    }
    const adapter = {
      adapterId: parsed.adapterId || "",
      protocol: parsed.protocol,
      enabled: parsed.enabled !== false,
      description: parsed.description || "",
      connection: parsed.connection || {},
      credentials: parsed.credentials || {},
      equipment: Array.isArray(parsed.equipment) ? parsed.equipment : [],
    };
    return adapter;
  }

  /**
   * Map validator issue path to GUI field id.
   * Paths look like: /adapters/0/connection/endpointUrl or relative connection/host
   * When editing a single adapter, strip leading /adapters/N/
   */
  function issuePathToFieldId(path, adapterId) {
    if (!path) return null;
    let p = String(path);
    const adapterMatch = p.match(/^\/adapters\/\d+\/(.*)$/);
    if (adapterMatch) {
      p = adapterMatch[1];
    } else if (p.startsWith("/")) {
      p = p.slice(1);
    }
    // connection/endpointUrl
    const conn = p.match(/^connection\/([A-Za-z0-9_]+)$/);
    if (conn) {
      return "f-conn-" + conn[1];
    }
    if (p === "adapterId") return "f-id";
    if (p === "protocol") return "f-protocol";
    if (p === "description") return "f-desc";
    if (p === "enabled") return "f-enabled";
    // equipment/0/telemetry/0/address
    const tel = p.match(/^equipment\/(\d+)\/telemetry\/(\d+)\/([A-Za-z0-9_]+)$/);
    if (tel) {
      return "f-eq-" + tel[1] + "-tel-" + tel[2] + "-" + tel[3];
    }
    const cmd = p.match(/^equipment\/(\d+)\/commands\/(\d+)\/([A-Za-z0-9_]+)$/);
    if (cmd) {
      return "f-eq-" + cmd[1] + "-cmd-" + cmd[2] + "-" + cmd[3];
    }
    const eqField = p.match(/^equipment\/(\d+)\/([A-Za-z0-9_]+)$/);
    if (eqField) {
      return "f-eq-" + eqField[1] + "-" + eqField[2];
    }
    const cred = p.match(/^credentials\/([A-Za-z0-9_]+)$/);
    if (cred) {
      return "f-cred-" + cred[1];
    }
    return null;
  }

  function fieldIdsForIssues(issues) {
    const map = {};
    (issues || []).forEach((issue) => {
      const id = issuePathToFieldId(issue.path);
      if (id) {
        if (!map[id]) map[id] = [];
        map[id].push(issue.message || "invalid");
      }
    });
    return map;
  }

  function requiredTelemetryKeys(protocol) {
    return (TELEMETRY_FIELDS[protocol] || [])
      .filter((f) => f.required)
      .map((f) => f.key);
  }

  function requiredConnectionKeys(protocol) {
    return (CONNECTION_FIELDS[protocol] || [])
      .filter((f) => f.required)
      .map((f) => f.key);
  }

  function equipmentExtraFields(protocol) {
    if (protocol === "rest") {
      return [
        { key: "telemetryPath", label: "Telemetry path", placeholder: "/api/device" },
        { key: "statePointer", label: "State JSON pointer" },
        { key: "faultPointer", label: "Fault JSON pointer" },
      ];
    }
    if (protocol === "profinet") {
      return [
        { key: "stationName", label: "Station name", required: true },
        { key: "ipAddress", label: "IP address" },
        { key: "vendorId", label: "Vendor ID", type: "number" },
        { key: "deviceId", label: "Device ID", type: "number" },
      ];
    }
    if (protocol === "profibus") {
      return [
        {
          key: "stationAddress",
          label: "Station address (1–126)",
          type: "number",
          required: true,
        },
      ];
    }
    return [];
  }

  function protocolsList() {
    return Object.keys(CONNECTION_FIELDS);
  }

  function adapterImplementation(protocol) {
    if (protocol === "mock") return "simulated";
    if (protocol === "profinet" || protocol === "profibus") return "hilscher_native";
    if (
      protocol === "opcua" ||
      protocol === "modbus" ||
      protocol === "mqtt" ||
      protocol === "rest" ||
      protocol === "ethernetip"
    ) {
      return "gateway";
    }
    return "gateway";
  }

  return {
    PROTOCOL_LABELS,
    CONNECTION_FIELDS,
    TELEMETRY_FIELDS,
    COMMAND_FIELDS,
    clone,
    defaultAdapter,
    applyProtocolChange,
    adapterToConfigJson,
    parseAdapterJson,
    issuePathToFieldId,
    fieldIdsForIssues,
    requiredTelemetryKeys,
    requiredConnectionKeys,
    equipmentExtraFields,
  protocolsList,
  adapterImplementation,
};
});
