/**
 * Headless GUI smoke: Add Industrial Adapter chooser + implementation flow.
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
  for (const label of ["Mock", "OPC UA", "Modbus TCP", "MQTT", "REST", "EtherNet/IP", "PROFINET", "PROFIBUS"]) {
    if (!chooserText.includes(label)) fail("chooser missing protocol: " + label);
  }
  const badges = await page.$$eval(".chooser-modal .badge", (els) =>
    els.map((e) => (e.textContent || "").toLowerCase())
  );
  if (!badges.some((b) => b.includes("supported"))) fail("chooser should show Supported");
  if (!badges.some((b) => b.includes("implementation required"))) {
    fail("chooser should show Implementation required");
  }
  pass("chooser lists all eight protocols");

  await page.click('[data-action="choose-protocol"][data-protocol="opcua"]');
  await page.waitForSelector("#adapter-editor", { timeout: 5000 });
  await page.waitForFunction(() => document.getElementById("f-protocol")?.value === "opcua");
  const editorText = await page.$eval("#adapter-editor", (el) => el.innerText);
  if (!editorText.includes("Basic Information") || !editorText.includes("Connection")) {
    fail("OPC UA editor should use sectioned layout");
  }
  if (!editorText.includes("Show JSON")) fail("JSON should be an advanced/collapsed control");
  pass("OPC UA opens editor directly");

  await page.click('[data-action="cancel-editor"]');
  await page.waitForSelector('[data-action="open-add-chooser"]', { timeout: 5000 });
  await page.click('[data-action="open-add-chooser"]');
  await page.waitForSelector(".chooser-modal", { timeout: 5000 });
  await page.click('[data-action="choose-protocol"][data-protocol="profinet"]');
  await page.waitForSelector("#impl-title", { timeout: 5000 });
  const implText = await page.$eval(".chooser-modal", (el) => el.innerText);
  if (!implText.includes("Gateway") || !implText.includes("AVAILABLE")) fail("Gateway not marked AVAILABLE");
  if (!implText.includes("Hilscher") || !implText.includes("COMING SOON")) fail("Hilscher not COMING SOON");
  if (!implText.includes("Softing") || !implText.includes("COMING SOON")) fail("Softing not COMING SOON");
  pass("PROFINET opens implementation chooser with correct labels");

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
