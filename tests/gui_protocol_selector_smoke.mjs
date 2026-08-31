/**
 * Headless GUI smoke: protocol sync, protocol-specific fields, refresh, mock semantics.
 * Run: node tests/gui_protocol_selector_smoke.mjs http://127.0.0.1:8090
 */
import puppeteer from "puppeteer-core";

const base = process.argv[2] || "http://127.0.0.1:8090";
const chromePath = process.env.CHROME_PATH || "/usr/local/bin/google-chrome";

const protocols = [
  "mock",
  "opcua",
  "modbus",
  "mqtt",
  "rest",
  "ethernetip",
  "profinet",
  "profibus",
];

const expectedConnField = {
  mock: null,
  opcua: "f-conn-endpointUrl",
  modbus: "f-conn-host",
  mqtt: "f-conn-host",
  rest: "f-conn-scheme",
  ethernetip: "f-conn-port",
  profinet: "f-conn-boardId",
  profibus: "f-conn-baudRateKbps",
};

const expectedTelField = {
  mock: "f-eq-0-tel-0-name",
  opcua: "f-eq-0-tel-0-address",
  modbus: "f-eq-0-tel-0-registerAddress",
  mqtt: "f-eq-0-tel-0-address",
  rest: "f-eq-0-tel-0-jsonPointer",
  ethernetip: "f-eq-0-tel-0-address",
  profinet: "f-eq-0-stationName",
  profibus: "f-eq-0-stationAddress",
};

function fail(msg) {
  console.error("FAIL:", msg);
  process.exitCode = 1;
}

function pass(msg) {
  console.log("PASS:", msg);
}

async function waitForProtocol(page, proto) {
  await page.waitForFunction(
    (p) =>
      document.getElementById("f-protocol")?.value === p &&
      document.getElementById("new-protocol")?.value === p,
    {},
    proto
  );
  await new Promise((r) => setTimeout(r, 200));
}

const browser = await puppeteer.launch({
  executablePath: chromePath,
  headless: true,
  args: ["--no-sandbox", "--disable-setuid-sandbox"],
});

try {
  const page = await browser.newPage();
  page.on("pageerror", (err) => console.error("pageerror:", err.message));

  await page.goto(`${base}/#/adapters`, { waitUntil: "networkidle2" });
  await page.click('[data-action="add-adapter"]');
  await page.waitForSelector("#f-protocol", { timeout: 5000 });

  for (const proto of protocols) {
    // Change via toolbar (authoritative sync).
    await page.select("#new-protocol", proto);
    await waitForProtocol(page, proto);
    pass(`toolbar+editor protocol synchronized for ${proto}`);

    const connId = expectedConnField[proto];
    if (connId) {
      const visible = await page.$(`#${connId}`);
      if (!visible) fail(`missing connection field ${connId} for ${proto}`);
      else pass(`protocol-specific connection field visible: ${connId}`);
    }

    const telId = expectedTelField[proto];
    const telVisible = await page.$(`#${telId}`);
    if (!telVisible) fail(`missing equipment/telemetry field ${telId} for ${proto}`);
    else pass(`required mapping field visible: ${telId}`);

    await page.click("#btn-refresh");
    await page.waitForSelector("#f-protocol", { timeout: 5000 });
    const afterRefresh = await page.$eval("#f-protocol", (el) => el.value);
    const toolbarAfter = await page.$eval("#new-protocol", (el) => el.value);
    if (afterRefresh !== proto || toolbarAfter !== proto) {
      fail(`protocol ${proto} lost after refresh (editor=${afterRefresh} toolbar=${toolbarAfter})`);
    } else {
      pass(`protocol ${proto} survives refresh re-render`);
    }
  }

  // Change via editor select and verify toolbar follows.
  await page.select("#f-protocol", "opcua");
  await waitForProtocol(page, "opcua");
  pass("editor protocol change syncs toolbar");

  // Advanced JSON round-trip.
  await page.click('[data-action="toggle-advanced"]');
  await page.waitForSelector("#f-advanced-json", { timeout: 5000 });
  await page.waitForSelector('[data-action="gui-to-json"]', { timeout: 3000 });
  await page.click('[data-action="gui-to-json"]');
  await page.waitForFunction(
    () => (document.getElementById("f-advanced-json")?.value || "").includes('"protocol"'),
    { timeout: 5000 }
  );
  const jsonText = await page.$eval("#f-advanced-json", (el) => el.value);
  if (!jsonText.includes('"protocol": "opcua"')) fail("GUI→JSON missing opcua");
  else pass("GUI → JSON generation");

  const mutated = JSON.parse(jsonText);
  mutated.adapterId = "opcua-from-json";
  mutated.connection.endpointUrl = "opc.tcp://10.0.0.9:4840";
  await page.$eval(
    "#f-advanced-json",
    (el, t) => {
      el.value = t;
    },
    JSON.stringify(mutated, null, 2)
  );
  await page.click('[data-action="json-to-gui"]');
  await page.waitForFunction(
    () => document.getElementById("f-id")?.value === "opcua-from-json",
    { timeout: 8000 }
  );
  await page.waitForSelector("#f-conn-endpointUrl", { timeout: 5000 });
  const endpoint = await page.$eval("#f-conn-endpointUrl", (el) => el.value);
  if (endpoint !== "opc.tcp://10.0.0.9:4840") fail("JSON→GUI endpoint mismatch: " + endpoint);
  else pass("JSON → GUI import/apply");

  // Save mock via form fields (not freehand equipment JSON).
  await page.select("#f-protocol", "mock");
  await waitForProtocol(page, "mock");
  await new Promise((r) => setTimeout(r, 400));
  const adapterId = "mock-gui-" + Date.now().toString(36);
  await page.evaluate((id) => {
    document.getElementById("f-id").value = id;
  }, adapterId);
  await page.click('[data-action="save-adapter"]');
  await page.waitForFunction(
    (id) => Boolean(document.querySelector(`[data-id="${id}"]`)),
    { timeout: 15000 },
    adapterId
  );
  pass("mock adapter saved from protocol-specific GUI");

  // Validation error display: open OPC UA with empty NodeId.
  await page.click('[data-action="add-adapter"]');
  await page.waitForSelector("#f-protocol");
  await page.select("#new-protocol", "opcua");
  await waitForProtocol(page, "opcua");
  await page.evaluate(() => {
    document.getElementById("f-id").value = "opcua-bad-" + Date.now().toString(36);
    const node = document.getElementById("f-eq-0-tel-0-address");
    if (node) node.value = "";
  });
  await page.click('[data-action="save-adapter"]');
  await page.waitForFunction(
    () => document.querySelector(".field-error") || /Validation failed/i.test(document.body.innerText),
    { timeout: 8000 }
  );
  pass("validation error displayed against GUI fields");

  await page.click('[data-action="cancel-editor"]');
  await page.waitForFunction(() => !document.getElementById("adapter-editor"));

  const connectBtn = await page.$(`[data-action="connect"][data-id="${adapterId}"]`);
  if (!connectBtn) {
    fail("connect button missing");
  } else {
    await connectBtn.click();
    await page.waitForFunction(
      (id) => {
        const flash = document.getElementById("flash")?.textContent || "";
        if (flash.includes("connect succeeded")) return true;
        const rows = [...document.querySelectorAll("table tbody tr")];
        return rows.some(
          (row) =>
            row.innerText.includes(id) && row.querySelector(".status.SIMULATED_ACTIVE")
        );
      },
      { timeout: 20000 },
      adapterId
    );
    pass("mock connect shows simulated active label");
  }

  // Diagnostics dynamic content.
  await page.goto(`${base}/#/diagnostics`, { waitUntil: "networkidle2" });
  await page.waitForFunction(
    () => /Per-adapter diagnostics|Runtime health/i.test(document.body.innerText),
    { timeout: 5000 }
  );
  pass("diagnostics renders runtime/adapter sections");

  // Polling must not wipe unsaved draft.
  await page.goto(`${base}/#/adapters`, { waitUntil: "networkidle2" });
  await page.waitForSelector('[data-action="add-adapter"]', { timeout: 8000 });
  await page.click('[data-action="add-adapter"]');
  await page.waitForSelector("#f-protocol", { timeout: 8000 });
  await page.select("#new-protocol", "modbus");
  await waitForProtocol(page, "modbus");
  await page.evaluate(() => {
    document.getElementById("f-id").value = "modbus-draft";
  });
  await new Promise((r) => setTimeout(r, 2600));
  const draftProto = await page.$eval("#f-protocol", (el) => el.value);
  const draftId = await page.$eval("#f-id", (el) => el.value);
  const toolbar = await page.$eval("#new-protocol", (el) => el.value);
  if (draftProto !== "modbus" || draftId !== "modbus-draft" || toolbar !== "modbus") {
    fail(`poll overwrote draft: proto=${draftProto} id=${draftId} toolbar=${toolbar}`);
  } else {
    pass("polling does not overwrite unsaved adapter editor");
  }
} catch (e) {
  fail(e.message);
} finally {
  await browser.close();
}

if (process.exitCode) process.exit(process.exitCode);
console.log("gui_protocol_selector_smoke: OK");
