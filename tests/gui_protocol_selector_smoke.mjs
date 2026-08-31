/**
 * Headless GUI smoke test for protocol selector + refresh + mock semantics.
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

function fail(msg) {
  console.error("FAIL:", msg);
  process.exitCode = 1;
}

function pass(msg) {
  console.log("PASS:", msg);
}

async function waitForProtocol(page, proto) {
  await page.waitForFunction(
    (p) => document.getElementById("f-protocol")?.value === p,
    {},
    proto
  );
  await new Promise((r) => setTimeout(r, 300));
}

const browser = await puppeteer.launch({
  executablePath: chromePath,
  headless: true,
  args: ["--no-sandbox", "--disable-setuid-sandbox"],
});

try {
  const page = await browser.newPage();
  page.on("pageerror", (err) => console.error("pageerror:", err.message));
  page.on("console", (msg) => {
    if (msg.type() === "error") console.error("browser:", msg.text());
  });

  await page.goto(`${base}/#/adapters`, { waitUntil: "networkidle2" });
  await page.click('[data-action="add-adapter"]');
  await page.waitForSelector("#f-protocol", { timeout: 5000 });

  for (const proto of protocols) {
    await page.select("#f-protocol", proto);
    await waitForProtocol(page, proto);
    pass(`protocol selector accepts ${proto}`);

    await page.click("#btn-refresh");
    await page.waitForSelector("#f-protocol", { timeout: 5000 });
    const afterRefresh = await page.$eval("#f-protocol", (el) => el.value);
    if (afterRefresh !== proto) {
      fail(`protocol ${proto} lost after refresh (got ${afterRefresh})`);
    } else {
      pass(`protocol ${proto} survives refresh re-render`);
    }
  }

  await page.select("#f-protocol", "mock");
  await waitForProtocol(page, "mock");
  await page.waitForFunction(
    () => (document.getElementById("flash")?.textContent || "").includes("Protocol changed"),
    { timeout: 5000 }
  );
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
  pass("mock adapter saved from GUI");

  await page.click("#btn-refresh");
  await new Promise((r) => setTimeout(r, 500));
  const listed = await page.evaluate((id) =>
    Boolean(document.querySelector(`[data-id="${id}"]`))
  , adapterId);
  if (!listed) fail("adapter missing after refresh");
  else pass("refresh updates adapter list");

  const connectBtn = await page.$(`[data-action="connect"][data-id="${adapterId}"]`);
  if (!connectBtn) {
    fail("connect button missing");
  } else {
    await connectBtn.click();
    await page.waitForFunction(
      () => {
        const flash = document.getElementById("flash")?.textContent || "";
        if (flash.includes("connect succeeded")) return true;
        const rows = [...document.querySelectorAll("table tbody tr")];
        return rows.some(
          (row) =>
            row.innerText.includes(id) &&
            row.querySelector(".status.SIMULATED_ACTIVE")
        );
      },
      { timeout: 20000 },
      adapterId
    );
    pass("mock connect shows simulated active label");
  }

  await page.click('[data-action="add-adapter"]');
  await page.waitForSelector("#f-protocol");
  await page.select("#f-protocol", "modbus");
  await waitForProtocol(page, "modbus");
  await page.evaluate(() => {
    document.getElementById("f-id").value = "modbus-draft";
  });
  await new Promise((r) => setTimeout(r, 2600));
  const draftProto = await page.$eval("#f-protocol", (el) => el.value);
  const draftId = await page.$eval("#f-id", (el) => el.value);
  if (draftProto !== "modbus" || draftId !== "modbus-draft") {
    fail(`poll overwrote draft: proto=${draftProto} id=${draftId}`);
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
