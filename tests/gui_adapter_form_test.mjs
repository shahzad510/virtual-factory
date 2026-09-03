/**
 * Unit tests for IcpAdapterForm (protocol-specific GUI model helpers).
 * Run: node tests/gui_adapter_form_test.mjs
 */
import { createRequire } from "module";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const Form = require(path.join(__dirname, "../icp/gui/js/adapter_form.js"));

let failures = 0;
function expect(cond, msg) {
  if (!cond) {
    console.error("FAIL:", msg);
    failures++;
  } else {
    console.log("PASS:", msg);
  }
}

const protocols = Form.protocolsList();
expect(protocols.length === 8, "eight protocols supported");
expect(protocols.includes("opcua") && protocols.includes("profibus"), "opcua and profibus present");

expect(Form.PROTOCOL_LABELS.modbus === "Modbus", "Modbus card title is Modbus (transport chosen next)");
expect(Form.PROTOCOL_INFO.opcua.description.indexOf("UaExpert") < 0, "OPC UA description has no UaExpert");
expect(
  !/implementation required/i.test(Form.PROTOCOL_INFO.profinet.support),
  "PROFINET does not say Implementation required"
);
expect(
  !/implementation required/i.test(Form.PROTOCOL_INFO.profibus.support),
  "PROFIBUS does not say Implementation required"
);
expect(Form.PROTOCOL_INFO.profinet.support === "Gateway", "PROFINET status is Gateway");
expect(Form.PROTOCOL_INFO.profibus.support === "Gateway", "PROFIBUS status is Gateway");
expect(Form.PROTOCOL_INFO.modbus.next === "transport", "Modbus opens transport selection");
expect(Form.PROTOCOL_INFO.mock.next === "editor", "Mock opens editor directly");
expect(Form.PROTOCOL_INFO.opcua.support === "Supported", "OPC UA marked supported");

expect(Form.MODBUS_TRANSPORT_INFO.tcp.available === true, "Modbus TCP transport available");
expect(Form.MODBUS_TRANSPORT_INFO.rtu.available === false, "Modbus RTU Coming Soon / unavailable");
expect(/coming soon/i.test(Form.MODBUS_TRANSPORT_INFO.rtu.support), "RTU badge Coming Soon");

const opcConn = Form.CONNECTION_FIELDS.opcua.map((f) => f.key);
expect(opcConn.includes("endpointUrl"), "OPC UA exposes endpointUrl");
expect(!opcConn.includes("useTls"), "OPC UA does not expose unused useTls");
expect(!opcConn.includes("timeoutMs"), "OPC UA does not expose unused timeoutMs");

const mbConn = Form.CONNECTION_FIELDS.modbus.map((f) => f.key);
expect(mbConn.join(",") === "host,port,timeoutMs", "Modbus TCP fields are host/port/timeout only");
expect(!mbConn.includes("unitId"), "Unit ID is not an adapter connection field");

const mbTel = Form.TELEMETRY_FIELDS.modbus;
expect(mbTel.some((f) => f.key === "unitId"), "Unit ID remains mapping-level");
expect(mbTel.some((f) => f.key === "registerAddress" && /0-based/i.test(f.help || "")), "Address help explains 0-based");
expect(!mbTel.some((f) => f.key === "address"), "No ambiguous Modbus address alt field");

expect(Form.requiredConnectionKeys({ protocol: "opcua" }).includes("endpointUrl"), "OPC UA requires endpointUrl");
expect(Form.requiredConnectionKeys({ protocol: "modbus" }).includes("port"), "Modbus requires port");
expect(Form.requiredConnectionKeys({ protocol: "mqtt" }).includes("host"), "MQTT requires host");
expect(Form.requiredConnectionKeys({ protocol: "rest" }).includes("scheme"), "REST requires scheme");
expect(Form.requiredConnectionKeys({ protocol: "ethernetip" }).includes("port"), "EIP requires port");
expect(
  Form.CONNECTION_FIELDS.rest.some((f) => f.key === "healthPath"),
  "REST exposes healthPath used by runtime"
);
expect(
  Form.CONNECTION_FIELDS.mqtt.some((f) => f.key === "useTls"),
  "MQTT exposes useTls used by runtime"
);
expect(
  Form.requiredConnectionKeys({ protocol: "profinet", implementation: "hilscher_native" }).includes("boardId"),
  "PN native requires boardId"
);
expect(
  Form.requiredConnectionKeys({ protocol: "profibus", implementation: "hilscher_native" }).includes("baudRateKbps"),
  "PB native requires baud"
);
expect(
  Form.requiredConnectionKeys({ protocol: "profinet", implementation: "gateway" }).includes("endpointUrl"),
  "PN gateway requires endpointUrl not boardId"
);
expect(
  !Form.requiredConnectionKeys({ protocol: "profinet", implementation: "gateway" }).includes("boardId"),
  "PN gateway does not require boardId"
);
expect(Form.requiredConnectionKeys({ protocol: "mock" }).length === 0, "Mock has no connection requirements");

expect(Form.requiredTelemetryKeys({ protocol: "opcua" }).includes("address"), "OPC UA telemetry NodeId required");
expect(Form.requiredTelemetryKeys({ protocol: "mqtt" }).includes("address"), "MQTT topic required");
expect(Form.requiredTelemetryKeys({ protocol: "ethernetip" }).includes("address"), "EIP tag required");

expect(Form.credentialsSupport("mqtt").username === true, "MQTT username supported");
expect(Form.credentialsSupport("mqtt").passwordRef === false, "MQTT password ref not live-resolved");
expect(Form.credentialsSupport("rest").username === true, "REST username supported");
expect(Form.credentialsSupport("opcua").username === false, "OPC UA has no live credentials UI");
expect(Form.credentialsSupport("modbus").username === false, "Modbus has no credentials UI");

const opc = Form.defaultAdapter("opcua", "a1");
expect(!Object.prototype.hasOwnProperty.call(opc.connection, "useTls"), "OPC UA default has no useTls");
opc.connection.endpointUrl = "opc.tcp://custom:4840";
opc.equipment[0].telemetry[0].address = "ns=2;s=X";
const mod = Form.applyProtocolChange(opc, "modbus");
expect(mod.protocol === "modbus", "protocol changed to modbus");
expect(mod.adapterId === "a1", "adapter id preserved on protocol change");
expect(mod.connection.port === 502, "Modbus TCP default port 502");

expect(
  Form.equipmentExtraFields({ protocol: "profinet", implementation: "hilscher_native" }).some(
    (f) => f.key === "stationName" && f.required
  ),
  "PROFINET native stationName required in GUI"
);
expect(
  Form.equipmentExtraFields({ protocol: "profinet", implementation: "gateway" }).length === 0,
  "PROFINET gateway has no native stationName extras"
);

const pnGateway = Form.defaultAdapter("profinet", "pn-gw", { implementation: "gateway" });
expect(pnGateway.implementation === "gateway", "gateway PN default implementation");
expect(!pnGateway.connection.boardId, "gateway PN default has no boardId");
expect(pnGateway.connection.endpointUrl, "gateway PN default has gateway endpointUrl");

const pnNative = Form.defaultAdapter("profinet", "pn-native", { implementation: "hilscher_native" });
expect(pnNative.implementation === "hilscher_native", "native PN default implementation");
expect(pnNative.connection.boardId === "cifx0", "native PN default retains boardId");

const jsonObj = Form.adapterToConfigJson(pnGateway);
expect(jsonObj.implementation === "gateway", "JSON persists implementation");
const roundTrip = Form.parseAdapterJson(JSON.stringify(jsonObj));
expect(roundTrip.implementation === "gateway", "JSON round-trip implementation");

expect(Form.adapterImplementation({ protocol: "profinet", connection: { boardId: "cifx0" } }) === "hilscher_native", "profinet with boardId is hilscher_native");
expect(
  Form.adapterImplementation({ protocol: "profinet", implementation: "gateway", connection: { endpointUrl: "opc.tcp://127.0.0.1:4840" } }) === "gateway",
  "profinet gateway implementation"
);
expect(Form.PROTOCOL_INFO.profinet.next === "implementation", "PROFINET requires implementation step");
expect(Form.PROTOCOL_INFO.profibus.next === "implementation", "PROFIBUS requires implementation step");
expect(Form.adapterImplementation("mock") === "simulated", "mock is simulated");

if (failures) {
  console.error("gui_adapter_form_test:", failures, "failure(s)");
  process.exit(1);
}
console.log("gui_adapter_form_test: OK");
