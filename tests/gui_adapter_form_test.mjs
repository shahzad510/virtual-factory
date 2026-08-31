/**
 * Unit tests for IcpAdapterForm (protocol-specific GUI model helpers).
 * Run: node tests/gui_adapter_form_test.mjs
 */
import { createRequire } from "module";
import { pathToFileURL } from "url";
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

// Protocol-specific required connection fields visibility
expect(Form.requiredConnectionKeys("opcua").includes("endpointUrl"), "OPC UA requires endpointUrl");
expect(Form.requiredConnectionKeys("modbus").includes("port"), "Modbus requires port");
expect(Form.requiredConnectionKeys("mqtt").includes("host"), "MQTT requires host");
expect(Form.requiredConnectionKeys("rest").includes("scheme"), "REST requires scheme");
expect(Form.requiredConnectionKeys("ethernetip").includes("port"), "EIP requires port");
expect(Form.requiredConnectionKeys("profinet").includes("boardId"), "PN requires boardId");
expect(Form.requiredConnectionKeys("profibus").includes("baudRateKbps"), "PB requires baud");
expect(Form.requiredConnectionKeys("mock").length === 0, "Mock has no connection requirements");

// Required telemetry keys
expect(Form.requiredTelemetryKeys("opcua").includes("address"), "OPC UA telemetry NodeId required");
expect(Form.requiredTelemetryKeys("mqtt").includes("address"), "MQTT topic required");
expect(Form.requiredTelemetryKeys("ethernetip").includes("address"), "EIP tag required");
expect(
  Form.TELEMETRY_FIELDS.modbus.some((f) => f.key === "registerAddress"),
  "Modbus exposes registerAddress field"
);
expect(
  Form.TELEMETRY_FIELDS.rest.some((f) => f.key === "jsonPointer"),
  "REST exposes jsonPointer field"
);

// Protocol change resets protocol-specific fields
const opc = Form.defaultAdapter("opcua", "a1");
opc.connection.endpointUrl = "opc.tcp://custom:4840";
opc.equipment[0].telemetry[0].address = "ns=2;s=X";
const mod = Form.applyProtocolChange(opc, "modbus");
expect(mod.protocol === "modbus", "protocol changed to modbus");
expect(mod.adapterId === "a1", "adapter id preserved on protocol change");
expect(mod.connection.host === "127.0.0.1", "modbus defaults host");
expect(mod.connection.endpointUrl == null || mod.connection.endpointUrl === undefined, "opcua endpoint cleared");
expect(
  mod.equipment[0].telemetry[0].table === "holdingRegister",
  "modbus telemetry defaults applied"
);
expect(
  !mod.equipment[0].telemetry[0].address || mod.equipment[0].telemetry[0].address === undefined,
  "stale opcua NodeId not retained as primary mapping"
);

// PROFINET / PROFIBUS equipment extras
expect(
  Form.equipmentExtraFields("profinet").some((f) => f.key === "stationName" && f.required),
  "PROFINET stationName required in GUI"
);
expect(
  Form.equipmentExtraFields("profibus").some((f) => f.key === "stationAddress" && f.required),
  "PROFIBUS stationAddress required in GUI"
);

// GUI -> JSON
const mock = Form.defaultAdapter("mock", "mock-1");
const jsonObj = Form.adapterToConfigJson(mock);
expect(jsonObj.protocol === "mock", "JSON protocol mock");
expect(Array.isArray(jsonObj.equipment), "JSON has equipment array");
expect(jsonObj._edit === undefined, "GUI-only keys stripped from JSON");

// JSON -> GUI
const roundTrip = Form.parseAdapterJson(JSON.stringify(jsonObj));
expect(roundTrip.adapterId === "mock-1", "JSON->GUI restores adapterId");
expect(roundTrip.equipment.length === mock.equipment.length, "JSON->GUI equipment count");

const opcJson = Form.adapterToConfigJson(Form.defaultAdapter("opcua", "ua-1"));
const opcGui = Form.parseAdapterJson(JSON.stringify(opcJson));
expect(opcGui.connection.endpointUrl, "JSON->GUI restores endpointUrl");
expect(opcGui.equipment[0].telemetry[0].address, "JSON->GUI restores NodeId");

// Validation issue path mapping
expect(
  Form.issuePathToFieldId("/adapters/0/connection/endpointUrl") === "f-conn-endpointUrl",
  "maps connection endpointUrl issue"
);
expect(
  Form.issuePathToFieldId("/adapters/0/equipment/0/telemetry/0/address") ===
    "f-eq-0-tel-0-address",
  "maps telemetry address issue"
);
expect(
  Form.issuePathToFieldId("/adapters/0/equipment/0/stationName") === "f-eq-0-stationName",
  "maps equipment stationName issue"
);
expect(
  Form.issuePathToFieldId("/adapters/0/equipment/0/stationAddress") === "f-eq-0-stationAddress",
  "maps stationAddress issue"
);

const fmap = Form.fieldIdsForIssues([
  { path: "/adapters/0/connection/host", message: "Modbus host is required" },
  { path: "/adapters/0/equipment/0/telemetry/0/address", message: "NodeId required" },
]);
expect(fmap["f-conn-host"][0].includes("host"), "field map host message");
expect(fmap["f-eq-0-tel-0-address"][0].includes("NodeId"), "field map NodeId message");

// Defaults include EIP port for validation
const eip = Form.defaultAdapter("ethernetip");
expect(eip.connection.port === 44818, "EIP default port 44818");
expect(eip.equipment[0].telemetry[0].address, "EIP default telemetry tag");

const rest = Form.defaultAdapter("rest");
expect(rest.connection.scheme === "http", "REST default scheme");
expect(rest.equipment[0].telemetry[0].jsonPointer, "REST default jsonPointer");

if (failures) {
  console.error("gui_adapter_form_test:", failures, "failure(s)");
  process.exit(1);
}
console.log("gui_adapter_form_test: OK");
