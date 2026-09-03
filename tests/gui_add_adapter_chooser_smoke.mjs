/**
 * Headless GUI smoke: Add Industrial Adapter chooser + transport/implementation flow.
 * Run: node tests/gui_add_adapter_chooser_smoke.mjs http://127.0.0.1:8090
 */
import puppeteer from "puppeteer-core";

const base = process.argv[2] || "http://127.0.0.1:8090";
const chromePath = process.env.CHROME_PATH || "/usr/local/bin/google-chrome";

function fail(msg) {
  console.error("FAIL:", msg);
  process.exitCode = 1;
}

function pass(msg) {
  console.log("PASS:", msg);
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

  if (await page.$("#new-protocol")) {
    fail("toolbar protocol dropdown removed");
  } else {
    pass("toolbar protocol dropdown removed");
  }

  await page.click('[data-action="open-add-chooser"]');
  await page.waitForSelector(".chooser-modal", { timeout: 5000 });
  pass("Add Industrial Adapter chooser opens");

  const chooserText = await page.$eval(".chooser-modal", (el) => el.innerText);
  for (const label of ["Mock", "OPC UA", "Modbus", "MQTT", "REST", "EtherNet/IP", "PROFINET", "PROFIBUS"]) {
    if (!chooserText.includes(label)) fail("chooser missing protocol: " + label);
  }
  if (/UaExpert/i.test(chooserText)) fail("chooser must not mention UaExpert");
  if (/implementation required/i.test(chooserText)) {
    fail("chooser must not say Implementation required");
  }
  const badges = await page.$$eval(".chooser-modal .badge", (els) =>
    els.map((e) => (e.textContent || "").trim())
  );
  if (!badges.some((b) => /supported/i.test(b))) fail("chooser should show Supported");
  if (!badges.some((b) => /gateway/i.test(b))) fail("chooser should show Gateway for PN/PB");
  pass("chooser lists all eight protocols with professional badges");

  await page.click('[data-action="choose-protocol"][data-protocol="opcua"]');
  await page.waitForSelector("#adapter-editor", { timeout: 5000 });
  await page.waitForFunction(() => document.getElementById("f-protocol")?.value === "opcua");
  const editorText = await page.$eval("#adapter-editor", (el) => el.innerText);
  if (!editorText.includes("Basic Information") || !editorText.includes("Connection")) {
    fail("OPC UA editor should use sectioned layout");
  }
  if (await page.$("#f-conn-useTls")) fail("OPC UA must not show unused useTls");
  if (await page.$("#f-conn-timeoutMs")) fail("OPC UA must not show unused timeoutMs");
  if (!(await page.$("#f-conn-endpointUrl"))) fail("OPC UA must show endpointUrl");
  if (await page.$("#f-cred-username")) fail("OPC UA must not show unused credentials");
  pass("OPC UA opens editor with runtime-honest connection fields");

  await page.click('[data-action="cancel-editor"]');
  await page.waitForSelector('[data-action="open-add-chooser"]', { timeout: 5000 });
  await page.click('[data-action="open-add-chooser"]');
  await page.waitForSelector(".chooser-modal", { timeout: 5000 });
  await page.click('[data-action="choose-protocol"][data-protocol="modbus"]');
  await page.waitForSelector("#transport-title", { timeout: 5000 });
  const transportText = await page.$eval(".chooser-modal", (el) => el.innerText);
  if (!transportText.includes("Modbus TCP") || !/supported/i.test(transportText)) {
    fail("Modbus TCP transport must be Supported");
  }
  if (!transportText.includes("Modbus RTU") || !/coming soon/i.test(transportText)) {
    fail("Modbus RTU / RS-485 must be Coming Soon");
  }
  pass("Modbus opens transport selection");

  await page.click('[data-action="choose-modbus-transport"][data-transport="rtu"]');
  await new Promise((r) => setTimeout(r, 300));
  if (await page.$("#adapter-editor")) fail("RTU Coming Soon must not create adapter");
  pass("Modbus RTU Coming Soon does not create adapter");

  await page.waitForSelector('[data-action="open-add-chooser"]', { timeout: 5000 });
  await page.click('[data-action="open-add-chooser"]');
  await page.waitForSelector(".chooser-modal", { timeout: 5000 });
  await page.click('[data-action="choose-protocol"][data-protocol="modbus"]');
  await page.waitForSelector("#transport-title", { timeout: 5000 });
  await page.click('[data-action="choose-modbus-transport"][data-transport="tcp"]');
  await page.waitForSelector("#adapter-editor", { timeout: 5000 });
  await page.waitForFunction(() => document.getElementById("f-protocol")?.value === "modbus");
  if (!(await page.$("#f-conn-host")) || !(await page.$("#f-conn-port")) || !(await page.$("#f-conn-timeoutMs"))) {
    fail("Modbus TCP editor must expose host, port, timeout");
  }
  const portVal = await page.$eval("#f-conn-port", (el) => el.value);
  if (portVal !== "502") fail("Modbus TCP default port should be 502");
  if (await page.$("#f-conn-unitId")) fail("Unit ID must not be adapter-level");
  pass("Modbus TCP selectable with backend-supported fields");

  await page.click('[data-action="cancel-editor"]');
  await page.waitForSelector('[data-action="open-add-chooser"]', { timeout: 5000 });
  await page.click('[data-action="open-add-chooser"]');
  await page.waitForSelector(".chooser-modal", { timeout: 5000 });
  await page.click('[data-action="choose-protocol"][data-protocol="profinet"]');
  await page.waitForSelector("#impl-title", { timeout: 5000 });
  const implText = await page.$eval(".chooser-modal", (el) => el.innerText);
  if (!implText.includes("Gateway") || !/available/i.test(implText)) fail("Gateway not marked Available");
  if (!implText.includes("Hilscher") || !/coming soon/i.test(implText)) fail("Hilscher not Coming Soon");
  if (!implText.includes("Softing") || !/coming soon/i.test(implText)) fail("Softing not Coming Soon");
  if (/implementation required/i.test(implText)) fail("implementation dialog must not say Implementation required");
  pass("PROFINET opens gateway-oriented implementation chooser");

  await page.click('[data-action="choose-implementation"][data-implementation="hilscher_native"]');
  await new Promise((r) => setTimeout(r, 300));
  if (await page.$("#adapter-editor")) fail("Hilscher selection must not open editor");
  pass("Hilscher Coming Soon does not create adapter");

  await page.waitForSelector('[data-action="open-add-chooser"]', { timeout: 5000 });
  await page.click('[data-action="open-add-chooser"]');
  await page.waitForSelector(".chooser-modal", { timeout: 5000 });
  await page.click('[data-action="choose-protocol"][data-protocol="profibus"]');
  await page.waitForSelector("#impl-title");
  await page.click('[data-action="choose-implementation"][data-implementation="gateway"]');
  await page.waitForSelector("#adapter-editor", { timeout: 5000 });
  await page.waitForFunction(() => document.getElementById("f-implementation-value")?.value === "gateway");
  const boardVal = await page.$eval("#f-conn-endpointUrl", (el) => el.value);
  if (!boardVal.includes("opc.tcp")) fail("Gateway editor should use gateway endpoint, not native board");
  if (await page.$("#f-conn-boardId")) fail("Gateway PROFIBUS editor must not show boardId field");
  pass("Gateway PROFIBUS editor uses gateway defaults");

  await page.goto(`${base}/#/diagnostics`, { waitUntil: "networkidle2" });
  const diagText = await page.evaluate(() => document.body.innerText);
  if (/Hilscher native fieldbus/i.test(diagText)) {
    fail("diagnostics must not show Hilscher section without native adapters");
  } else {
    pass("diagnostics omits Hilscher without native adapters");
  }
} finally {
  await browser.close();
}

if (process.exitCode) {
  console.error("gui_add_adapter_chooser_smoke: failures");
  process.exit(process.exitCode);
}
console.log("gui_add_adapter_chooser_smoke: OK");
