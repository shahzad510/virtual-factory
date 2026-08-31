/* Standalone ICP GUI — industrial operator shell. Polling for live updates. */
(function () {
  const titles = {
    dashboard: "Dashboard",
    adapters: "Adapters",
    equipment: "Equipment",
    connections: "Connections",
    configuration: "Configuration",
    mappings: "Mappings",
    diagnostics: "Diagnostics",
    events: "Logs / Events",
    settings: "Settings",
  };

  const state = {
    route: "dashboard",
    equipmentId: null,
    adapterId: null,
    protocols: [],
    pollTimer: null,
  };

  function $(sel) {
    return document.querySelector(sel);
  }

  function esc(s) {
    return String(s == null ? "" : s)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function statusBadge(value) {
    const v = value || "UNKNOWN";
    return `<span class="status ${esc(v)}">${esc(v)}</span>`;
  }

  function flash(message, kind) {
    const el = $("#flash");
    if (!message) {
      el.className = "flash hidden";
      el.textContent = "";
      return;
    }
    el.className = "flash " + (kind || "");
    el.textContent = message;
  }

  function formatIssues(result) {
    if (!result || !result.issues || !result.issues.length) {
      return result && result.message ? esc(result.message) : "";
    }
    return (
      `<ul class="issues">` +
      result.issues
        .map((i) => `<li><code>${esc(i.path)}</code> — ${esc(i.message)}</li>`)
        .join("") +
      `</ul>`
    );
  }

  function defaultAdapter(protocol) {
    const id = protocol + "-" + Date.now().toString(36).slice(-4);
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
      base.connection = { endpointUrl: "opc.tcp://127.0.0.1:4840" };
    } else if (protocol === "modbus") {
      base.connection = { host: "127.0.0.1", port: 502, timeoutMs: 2000 };
    } else if (protocol === "mqtt") {
      base.connection = { host: "127.0.0.1", port: 1883, clientId: id };
    } else if (protocol === "rest") {
      base.connection = { scheme: "http", host: "127.0.0.1", port: 8081, timeoutMs: 2000 };
    } else if (protocol === "ethernetip") {
      base.connection = { host: "127.0.0.1", path: "1,0", plcType: "controllogix", timeoutMs: 2000 };
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
            {
              name: "input0",
              inputByteOffset: 0,
              bitOffset: 0,
              valueType: "UINT8",
            },
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
            {
              name: "input0",
              inputByteOffset: 0,
              bitOffset: 0,
              valueType: "UINT8",
            },
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

  async function renderDashboard() {
    const [st, ad] = await Promise.all([IcpApi.status(), IcpApi.adapters()]);
    if (!st.ok) {
      return `<div class="empty"><strong>API unavailable</strong>${esc(
        (st.data && st.data.message) || "Cannot reach /api/v1/status"
      )}</div>`;
    }
    const s = st.data;
    const adapters = (ad.data && ad.data.adapters) || [];
    let body = `
      <div class="grid stats">
        <div class="stat"><div class="label">ICP status</div><div class="value">${
          s.schedulerRunning ? "RUNNING" : "STOPPED"
        }</div></div>
        <div class="stat"><div class="label">Adapters</div><div class="value">${esc(
          s.configuredAdapterCount
        )}</div></div>
        <div class="stat"><div class="label">Connected</div><div class="value">${esc(
          s.connectedAdapters
        )}</div></div>
        <div class="stat"><div class="label">Faulted</div><div class="value">${esc(
          s.faultedAdapters
        )}</div></div>
        <div class="stat"><div class="label">Equipment</div><div class="value">${esc(
          s.equipmentCount
        )}</div></div>
        <div class="stat"><div class="label">Connected eq.</div><div class="value">${esc(
          s.connectedEquipment
        )}</div></div>
        <div class="stat"><div class="label">Stale eq.</div><div class="value">${esc(
          s.staleEquipment
        )}</div></div>
        <div class="stat"><div class="label">Active comms</div><div class="value">${esc(
          s.activeCommunications
        )}</div></div>
      </div>`;

    if (!adapters.length) {
      body += `<div class="panel"><div class="empty">
        <strong>No adapters configured</strong>
        Add an industrial adapter to begin.
        <div style="margin-top:0.75rem"><button class="primary" data-action="goto-adapters">Add Adapter</button></div>
      </div></div>`;
    } else {
      const dist = s.protocolDistribution || {};
      body += `<div class="panel"><h2>Protocol distribution</h2><table><thead><tr><th>Protocol</th><th>Count</th></tr></thead><tbody>`;
      body += Object.keys(dist)
        .map((k) => `<tr><td>${esc(k)}</td><td>${esc(dist[k])}</td></tr>`)
        .join("");
      body += `</tbody></table></div>`;
    }

    const events = s.recentEvents || [];
    body += `<div class="panel"><h2>Recent events</h2>`;
    if (!events.length) {
      body += `<p class="muted">No events yet.</p>`;
    } else {
      body += `<table><thead><tr><th>Time</th><th>Level</th><th>Category</th><th>Message</th></tr></thead><tbody>`;
      body += events
        .slice()
        .reverse()
        .map(
          (e) =>
            `<tr><td class="mono">${esc(e.atUtc)}</td><td>${esc(e.level)}</td><td>${esc(
              e.category
            )}</td><td>${esc(e.message)}</td></tr>`
        )
        .join("");
      body += `</tbody></table>`;
    }
    body += `</div>
      <p class="muted">MES dependency: ${s.mesDependency ? "yes" : "no"} · CIC dependency: ${
      s.cicDependency ? "yes" : "no"
    } · Config: ${esc(s.configurationName || "(unnamed)")} · ${
      s.configurationLoaded ? esc(s.configurationLoadState || "loaded") : "not loaded"
    }</p>`;
    return body;
  }

  function adapterFormHtml(adapter, protocols) {
    const options = protocols
      .map(
        (p) =>
          `<option value="${esc(p.id)}" ${
            adapter.protocol === p.id ? "selected" : ""
          }>${esc(p.label)}${p.requiresHilscherHardware ? " (HW optional to configure)" : ""}</option>`
      )
      .join("");
    const c = adapter.connection || {};
    return `
      <div class="panel" id="adapter-editor">
        <h2>${adapter._edit ? "Edit adapter" : "Add adapter"}</h2>
        <div class="form-grid">
          <label>Adapter ID<input id="f-id" value="${esc(adapter.adapterId)}" ${
            adapter._edit ? "readonly" : ""
          }/></label>
          <label>Protocol<select id="f-protocol">${options}</select></label>
          <label>Description<input id="f-desc" value="${esc(adapter.description || "")}"/></label>
          <label>Enabled<select id="f-enabled"><option value="true" ${
            adapter.enabled !== false ? "selected" : ""
          }>true</option><option value="false" ${
            adapter.enabled === false ? "selected" : ""
          }>false</option></select></label>
          <label>Endpoint / Host<input id="f-host" value="${esc(
            c.endpointUrl || c.host || ""
          )}" placeholder="host or opc.tcp://..."/></label>
          <label>Port<input id="f-port" type="number" value="${esc(c.port || "")}"/></label>
          <label>Station / Client / Path<input id="f-station" value="${esc(
            c.stationName || c.clientId || c.path || ""
          )}"/></label>
          <label>Timeout (ms)<input id="f-timeout" type="number" value="${esc(
            c.timeoutMs || ""
          )}"/></label>
          <label>Board ID<input id="f-board" value="${esc(c.boardId || "")}"/></label>
          <label>Channel<input id="f-channel" type="number" value="${esc(
            c.channel != null ? c.channel : ""
          )}"/></label>
          <label>Baud (kbps) / Master addr<input id="f-baud" value="${esc(
            c.baudRateKbps || c.masterAddress || ""
          )}"/></label>
          <label>Interface<input id="f-iface" value="${esc(c.interfaceName || "")}"/></label>
          <label class="full">Equipment JSON<textarea id="f-equipment" rows="10" class="mono">${esc(
            JSON.stringify(adapter.equipment || [], null, 2)
          )}</textarea></label>
          <label class="full">Credentials (refs only)<textarea id="f-creds" rows="3" class="mono">${esc(
            JSON.stringify(adapter.credentials || {}, null, 2)
          )}</textarea></label>
        </div>
        <div class="toolbar" style="margin-top:0.75rem">
          <button class="primary" data-action="save-adapter">Validate &amp; Save adapter</button>
          <button data-action="cancel-editor">Cancel</button>
        </div>
        <div id="editor-result" class="muted"></div>
      </div>`;
  }

  function readAdapterForm() {
    const protocol = $("#f-protocol").value;
    const adapter = defaultAdapter(protocol);
    adapter.adapterId = $("#f-id").value.trim();
    adapter.description = $("#f-desc").value;
    adapter.enabled = $("#f-enabled").value === "true";
    const hostOrEndpoint = $("#f-host").value.trim();
    const port = parseInt($("#f-port").value, 10);
    const station = $("#f-station").value.trim();
    const timeoutMs = parseInt($("#f-timeout").value, 10);
    const boardId = $("#f-board").value.trim();
    const channel = parseInt($("#f-channel").value, 10);
    const baudOrMaster = $("#f-baud").value.trim();
    const iface = $("#f-iface").value.trim();

    adapter.connection = adapter.connection || {};
    if (protocol === "opcua") {
      adapter.connection.endpointUrl = hostOrEndpoint;
    } else if (protocol === "rest") {
      adapter.connection.host = hostOrEndpoint;
      adapter.connection.scheme = adapter.connection.scheme || "http";
    } else if (hostOrEndpoint) {
      adapter.connection.host = hostOrEndpoint;
    }
    if (!Number.isNaN(port) && port > 0) adapter.connection.port = port;
    if (!Number.isNaN(timeoutMs) && timeoutMs > 0) adapter.connection.timeoutMs = timeoutMs;
    if (protocol === "mqtt" && station) adapter.connection.clientId = station;
    if (protocol === "ethernetip" && station) adapter.connection.path = station;
    if (protocol === "profinet") {
      if (station) adapter.connection.stationName = station;
      if (boardId) adapter.connection.boardId = boardId;
      if (!Number.isNaN(channel)) adapter.connection.channel = channel;
      if (iface) adapter.connection.iface = iface;
      if (iface) adapter.connection.interfaceName = iface;
    }
    if (protocol === "profibus") {
      if (boardId) adapter.connection.boardId = boardId;
      if (!Number.isNaN(channel)) adapter.connection.channel = channel;
      if (baudOrMaster) {
        const n = parseInt(baudOrMaster, 10);
        if (!Number.isNaN(n)) {
          if (n < 100) adapter.connection.masterAddress = n;
          else adapter.connection.baudRateKbps = n;
        }
      }
    }
    try {
      adapter.equipment = JSON.parse($("#f-equipment").value || "[]");
    } catch (e) {
      throw new Error("Equipment JSON is invalid: " + e.message);
    }
    try {
      adapter.credentials = JSON.parse($("#f-creds").value || "{}");
    } catch (e) {
      throw new Error("Credentials JSON is invalid: " + e.message);
    }
    return adapter;
  }

  async function renderAdapters() {
    const [ad, proto] = await Promise.all([IcpApi.adapters(), IcpApi.protocols()]);
    state.protocols = (proto.data && proto.data.protocols) || [];
    const adapters = (ad.data && ad.data.adapters) || [];
    let html = `<div class="toolbar">
      <button class="primary" data-action="add-adapter">Add Adapter</button>
      <select id="new-protocol">${state.protocols
        .map((p) => `<option value="${esc(p.id)}">${esc(p.label)}</option>`)
        .join("")}</select>
    </div>`;

    if (!adapters.length) {
      html += `<div class="empty"><strong>No adapters configured</strong>Choose a protocol and click Add Adapter.</div>`;
    } else {
      html += `<div class="panel"><table><thead><tr>
        <th>Adapter</th><th>Protocol</th><th>State</th><th>Enabled</th><th>Equipment</th><th>Error</th><th>Actions</th>
      </tr></thead><tbody>`;
      html += adapters
        .map(
          (a) => `<tr>
          <td><a href="#/adapters/${esc(a.adapterId)}">${esc(a.adapterId)}</a></td>
          <td>${esc(a.protocol)}</td>
          <td>${statusBadge(a.connectionState)}</td>
          <td>${a.enabled ? "yes" : "no"}</td>
          <td>${esc(a.equipmentCount)}</td>
          <td class="mono">${esc(a.lastError || "")}</td>
          <td>
            <button data-action="connect" data-id="${esc(a.adapterId)}">Connect</button>
            <button data-action="disconnect" data-id="${esc(a.adapterId)}">Disconnect</button>
            <button data-action="reconnect" data-id="${esc(a.adapterId)}">Reconnect</button>
            <button data-action="edit-adapter" data-id="${esc(a.adapterId)}">Edit</button>
            <button class="danger" data-action="remove-adapter" data-id="${esc(
              a.adapterId
            )}">Remove</button>
          </td>
        </tr>`
        )
        .join("");
      html += `</tbody></table></div>`;
    }

    if (state._editingAdapter) {
      html += adapterFormHtml(state._editingAdapter, state.protocols);
    }
    return html;
  }

  async function renderEquipment() {
    if (state.equipmentId) {
      const res = await IcpApi.equipmentById(state.equipmentId);
      if (!res.ok) {
        return `<div class="empty"><strong>Equipment not found</strong>${esc(
          state.equipmentId
        )}<div style="margin-top:0.75rem"><a href="#/equipment">Back</a></div></div>`;
      }
      const e = res.data;
      const tel =
        (e.telemetry || [])
          .map(
            (t) =>
              `<tr><td>${esc(t.name)}</td><td>${esc(t.value)}</td><td>${esc(
                t.unit || ""
              )}</td></tr>`
          )
          .join("") || `<tr><td colspan="3" class="muted">No telemetry from backend</td></tr>`;
      const canCommand = e.communicationState === "CONNECTED" && !e.stale;
      const cmdBar = canCommand
        ? `<div class="toolbar">
            <button data-action="eq-cmd" data-id="${esc(e.equipmentId)}" data-cmd="start">Start</button>
            <button data-action="eq-cmd" data-id="${esc(e.equipmentId)}" data-cmd="stop">Stop</button>
          </div>`
        : `<p class="muted">Commands available when communication is CONNECTED.</p>`;
      return `<div class="toolbar"><a href="#/equipment">← Equipment list</a></div>
        <div class="panel">
          <h2>Equipment ${esc(e.equipmentId)}</h2>
          <dl class="kv">
            <dt>Type</dt><dd>${esc(e.type)}</dd>
            <dt>Adapter</dt><dd>${esc(e.adapterId)}</dd>
            <dt>Protocol</dt><dd>${esc(e.protocol)}</dd>
            <dt>Communication</dt><dd>${statusBadge(e.communicationState)}</dd>
            <dt>Machine state</dt><dd>${statusBadge(e.machineState)}</dd>
            <dt>Machine fault</dt><dd>${
              e.machineFault ? statusBadge("FAULTED") : statusBadge("NONE")
            }</dd>
            <dt>Stale</dt><dd>${e.stale ? statusBadge("STALE") : "no"}</dd>
            <dt>Observed</dt><dd class="mono">${esc(e.observedAtUtc || "")}</dd>
            <dt>Last error</dt><dd class="mono">${esc(e.lastError || "")}</dd>
          </dl>
          <h3>Telemetry</h3>
          <table><thead><tr><th>Name</th><th>Value</th><th>Unit</th></tr></thead><tbody>${tel}</tbody></table>
          <h3>Commands</h3>
          ${cmdBar}
          <p class="muted">Communication fault is distinct from machine fault.</p>
        </div>`;
    }

    const res = await IcpApi.equipment();
    const list = (res.data && res.data.equipment) || [];
    if (!list.length) {
      return `<div class="empty"><strong>No equipment</strong>Connect a configured adapter (e.g. Mock) to populate live equipment.</div>`;
    }
    return `<div class="panel"><table><thead><tr>
      <th>Equipment</th><th>Adapter</th><th>Protocol</th><th>Communication</th>
      <th>Machine</th><th>Machine fault</th><th>Stale</th><th>Observed</th>
    </tr></thead><tbody>${list
      .map(
        (e) => `<tr>
        <td><a href="#/equipment/${esc(e.equipmentId)}">${esc(e.equipmentId)}</a></td>
        <td>${esc(e.adapterId)}</td>
        <td>${esc(e.protocol)}</td>
        <td>${statusBadge(e.communicationState)}</td>
        <td>${statusBadge(e.machineState)}</td>
        <td>${e.machineFault ? statusBadge("FAULTED") : "NONE"}</td>
        <td>${e.stale ? statusBadge("STALE") : "no"}</td>
        <td class="mono">${esc(e.observedAtUtc || "")}</td>
      </tr>`
      )
      .join("")}</tbody></table></div>`;
  }

  async function renderConnections() {
    const res = await IcpApi.adapters();
    const adapters = (res.data && res.data.adapters) || [];
    if (!adapters.length) {
      return `<div class="empty"><strong>No connections</strong>Configure an adapter first.</div>`;
    }
    return `<div class="panel"><table><thead><tr>
      <th>Adapter</th><th>Protocol</th><th>Connection</th><th>Error</th><th>Actions</th>
    </tr></thead><tbody>${adapters
      .map(
        (a) => `<tr>
        <td>${esc(a.adapterId)}</td>
        <td>${esc(a.protocol)}</td>
        <td>${statusBadge(a.connectionState)}</td>
        <td class="mono">${esc(a.lastError || "")}</td>
        <td>
          <button data-action="connect" data-id="${esc(a.adapterId)}">Connect</button>
          <button data-action="disconnect" data-id="${esc(a.adapterId)}">Disconnect</button>
          <button data-action="reconnect" data-id="${esc(a.adapterId)}">Reconnect</button>
        </td>
      </tr>`
      )
      .join("")}</tbody></table>
      <p class="muted">Reconnect is explicit disconnect then connect. Background auto-reconnect is not enabled.</p>
    </div>`;
  }

  async function renderConfiguration() {
    const [cfg, val] = await Promise.all([
      IcpApi.configuration(),
      IcpApi.validateConfiguration(),
    ]);
    const text = JSON.stringify(cfg.data || {}, null, 2);
    return `<div class="toolbar">
      <button class="primary" data-action="cfg-validate">Validate</button>
      <button data-action="cfg-save">Save</button>
      <button data-action="cfg-load">Load</button>
      <button data-action="cfg-apply">Apply editor</button>
      <button data-action="cfg-export">Export</button>
    </div>
    <div class="split">
      <div class="panel">
        <h2>Configuration document</h2>
        <textarea id="cfg-editor" class="mono" rows="28" style="width:100%">${esc(
          text
        )}</textarea>
      </div>
      <div class="panel">
        <h2>Validation</h2>
        <div id="cfg-validation">${
          val.data && val.data.ok
            ? statusBadge("ok") + " " + esc(val.data.message || "ok")
            : formatIssues(val.data)
        }</div>
        <h3>Import</h3>
        <textarea id="cfg-import" class="mono" rows="8" style="width:100%" placeholder="Paste ICP-1B JSON"></textarea>
        <div class="toolbar"><button data-action="cfg-import">Import</button></div>
        <p class="muted">Backend validation is authoritative. Persistence uses the existing versioned JSON format.</p>
      </div>
    </div>`;
  }

  async function renderMappings() {
    const res = await IcpApi.mappings();
    const mappings = (res.data && res.data.mappings) || [];
    if (!mappings.length) {
      return `<div class="empty"><strong>No mappings</strong>Mappings come from the ICP-1B configuration model.</div>`;
    }
    let html = "";
    for (const a of mappings) {
      html += `<div class="panel"><h2>${esc(a.adapterId)} · ${esc(a.protocol)}</h2>`;
      for (const eq of a.equipment || []) {
        html += `<h3>${esc(eq.equipmentId)}</h3>
          <table><thead><tr><th>Kind</th><th>Name</th><th>Address / source</th><th>Offset</th><th>Type</th><th>Direction</th></tr></thead><tbody>`;
        for (const t of eq.telemetry || []) {
          html += `<tr><td>Telemetry</td><td>${esc(t.name)}</td><td class="mono">${esc(
            t.address
          )}</td><td>${esc(t.inputByteOffset)}</td><td>${esc(t.valueType)}</td><td>${esc(
            t.direction
          )}</td></tr>`;
        }
        for (const c of eq.commands || []) {
          html += `<tr><td>Command</td><td>${esc(c.command)}</td><td class="mono">${esc(
            c.address
          )}</td><td>${esc(c.outputByteOffset)}</td><td>${esc(c.valueType)}</td><td>${esc(
            c.direction
          )}</td></tr>`;
        }
        if (eq.state && eq.state.mapped) {
          html += `<tr><td>State</td><td>state</td><td class="mono">${esc(
            eq.state.address
          )}</td><td>${esc(eq.state.inputByteOffset)}</td><td>${esc(
            eq.state.valueType
          )}</td><td>input</td></tr>`;
        }
        if (eq.fault && eq.fault.mapped) {
          html += `<tr><td>Fault</td><td>fault</td><td class="mono">${esc(
            eq.fault.address
          )}</td><td>${esc(eq.fault.inputByteOffset)}</td><td>${esc(
            eq.fault.valueType
          )}</td><td>input</td></tr>`;
        }
        html += `</tbody></table>`;
      }
      html += `</div>`;
    }
    return html;
  }

  async function renderDiagnostics() {
    const res = await IcpApi.diagnostics();
    if (!res.ok) {
      return `<div class="empty"><strong>Diagnostics unavailable</strong></div>`;
    }
    const d = res.data;
    const h = d.hilscher || {};
    const hw =
      h.hardware === "DETECTED"
        ? statusBadge("DETECTED")
        : statusBadge("HARDWARE_NOT_AVAILABLE");
    return `<div class="grid stats">
        <div class="stat"><div class="label">Scheduler</div><div class="value">${
          d.runtime && d.runtime.schedulerRunning ? "ON" : "OFF"
        }</div></div>
        <div class="stat"><div class="label">Connected</div><div class="value">${esc(
          (d.runtime && d.runtime.connectedAdapters) || 0
        )}</div></div>
        <div class="stat"><div class="label">Faulted</div><div class="value">${esc(
          (d.runtime && d.runtime.faultedAdapters) || 0
        )}</div></div>
        <div class="stat"><div class="label">Hilscher HW</div><div class="value" style="font-size:1rem">${hw}</div></div>
      </div>
      <div class="panel">
        <h2>Hilscher readiness</h2>
        <dl class="kv">
          <dt>SDK compiled in</dt><dd>${h.compiledIn ? "yes" : "no"}</dd>
          <dt>Readiness</dt><dd>${esc(h.readinessState || "")}</dd>
          <dt>Hardware</dt><dd>${hw}</dd>
          <dt>Driver</dt><dd class="mono">${esc(h.driverVersion || "")}</dd>
          <dt>Boards</dt><dd>${esc(h.boardCount || 0)}</dd>
          <dt>Selected board</dt><dd>${esc(h.selectedBoard || "")}</dd>
          <dt>Firmware</dt><dd>${esc(h.selectedFirmware || "")}</dd>
          <dt>Summary</dt><dd>${esc(h.summary || "")}</dd>
        </dl>
        <p class="muted">SDK installed without a card does not mean READY or CONNECTED.</p>
      </div>
      <div class="panel">
        <h2>Configuration errors</h2>
        ${
          d.configurationValidation && d.configurationValidation.ok
            ? statusBadge("ok")
            : formatIssues(d.configurationValidation)
        }
      </div>
      <div class="panel">
        <h2>Stale equipment</h2>
        ${
          !(d.staleEquipment || []).length
            ? `<p class="muted">None</p>`
            : `<table><thead><tr><th>Equipment</th><th>Adapter</th><th>Communication</th><th>Machine fault</th><th>Error</th></tr></thead><tbody>${(
                d.staleEquipment || []
              )
                .map(
                  (s) => `<tr><td>${esc(s.equipmentId)}</td><td>${esc(
                    s.adapterId
                  )}</td><td>${statusBadge(s.communicationState)}</td><td>${
                    s.machineFault ? "yes" : "no"
                  }</td><td class="mono">${esc(s.lastError || "")}</td></tr>`
                )
                .join("")}</tbody></table>`
        }
      </div>`;
  }

  async function renderEvents() {
    const res = await IcpApi.events(200);
    const events = (res.data && res.data.events) || [];
    if (!events.length) {
      return `<div class="empty"><strong>No events</strong></div>`;
    }
    return `<div class="panel"><table><thead><tr><th>Time</th><th>Level</th><th>Category</th><th>Adapter</th><th>Message</th></tr></thead><tbody>${events
      .slice()
      .reverse()
      .map(
        (e) => `<tr><td class="mono">${esc(e.atUtc)}</td><td>${esc(e.level)}</td><td>${esc(
          e.category
        )}</td><td>${esc(e.adapterId || "")}</td><td>${esc(e.message)}</td></tr>`
      )
      .join("")}</tbody></table></div>`;
  }

  async function renderSettings() {
    const st = await IcpApi.status();
    const s = st.data || {};
    return `<div class="panel">
      <h2>ICP settings</h2>
      <dl class="kv">
        <dt>Product</dt><dd>${esc(s.product)}</dd>
        <dt>Version</dt><dd>${esc(s.version)}</dd>
        <dt>API</dt><dd>${esc(s.apiVersion)}</dd>
        <dt>Config path</dt><dd class="mono">${esc(s.configurationPath)}</dd>
        <dt>Config state</dt><dd>${esc(s.configurationLoadState || (s.configurationLoaded ? "loaded" : "unknown"))}</dd>
        <dt>Live updates</dt><dd>HTTP polling (~2s)</dd>
        <dt>MES</dt><dd>not required</dd>
        <dt>CIC</dt><dd>not required</dd>
        <dt>Designer</dt><dd>not implemented (nav disabled)</dd>
      </dl>
    </div>`;
  }

  async function render() {
    const route = state.route;
    $("#page-title").textContent = titles[route] || "ICP";
    document.querySelectorAll(".nav a").forEach((a) => {
      a.classList.toggle("active", a.dataset.route === route);
    });
    let html = "";
    try {
      if (route === "dashboard") html = await renderDashboard();
      else if (route === "adapters") html = await renderAdapters();
      else if (route === "equipment") html = await renderEquipment();
      else if (route === "connections") html = await renderConnections();
      else if (route === "configuration") html = await renderConfiguration();
      else if (route === "mappings") html = await renderMappings();
      else if (route === "diagnostics") html = await renderDiagnostics();
      else if (route === "events") html = await renderEvents();
      else if (route === "settings") html = await renderSettings();
      else html = `<div class="empty">Unknown route</div>`;
    } catch (e) {
      html = `<div class="empty"><strong>Render error</strong>${esc(e.message)}</div>`;
    }
    $("#content").innerHTML = html;
    const st = await IcpApi.status();
    if (st.ok) {
      $("#runtime-pill").textContent = st.data.schedulerRunning
        ? "ICP RUNNING"
        : "ICP STOPPED";
    }
  }

  function parseHash() {
    const h = location.hash.replace(/^#\/?/, "") || "dashboard";
    const parts = h.split("/");
    state.route = parts[0] || "dashboard";
    state.equipmentId = null;
    state.adapterId = null;
    if (state.route === "equipment" && parts[1]) state.equipmentId = decodeURIComponent(parts[1]);
    if (state.route === "adapters" && parts[1]) state.adapterId = decodeURIComponent(parts[1]);
  }

  async function onAction(action, id, el) {
    flash("");
    try {
      if (action === "goto-adapters") {
        location.hash = "#/adapters";
        return;
      }
      if (action === "add-adapter") {
        const protocol = ($("#new-protocol") && $("#new-protocol").value) || "mock";
        state._editingAdapter = defaultAdapter(protocol);
        state._editingAdapter._edit = false;
        await render();
        const editor = document.getElementById("adapter-editor");
        if (editor) {
          editor.scrollIntoView({ behavior: "smooth", block: "start" });
        }
        flash("Adapter editor opened — configure and click Validate & Save adapter", "ok");
        return;
      }
      if (action === "cancel-editor") {
        state._editingAdapter = null;
        await render();
        return;
      }
      if (action === "edit-adapter") {
        const detail = await IcpApi.adapter(id);
        if (!detail.ok) {
          flash((detail.data && detail.data.message) || "Adapter not found", "error");
          return;
        }
        state._editingAdapter = detail.data.configuration || {
          adapterId: id,
          protocol: detail.data.protocol,
          enabled: detail.data.enabled,
          description: detail.data.description,
          connection: {},
          credentials: {},
          equipment: [],
        };
        state._editingAdapter._edit = true;
        await render();
        return;
      }
      if (action === "save-adapter") {
        const adapter = readAdapterForm();
        const res = await IcpApi.upsertAdapter(adapter);
        const box = $("#editor-result");
        if (!res.ok || (res.data && res.data.ok === false)) {
          if (box) box.innerHTML = formatIssues(res.data);
          flash((res.data && res.data.message) || "Validation failed", "error");
          return;
        }
        // Persist immediately so restart survives.
        const save = await IcpApi.saveConfiguration();
        state._editingAdapter = null;
        flash(
          save.ok ? "Adapter saved and configuration persisted" : "Adapter saved in memory; persist failed",
          save.ok ? "ok" : "error"
        );
        await render();
        return;
      }
      if (action === "eq-cmd") {
        const cmd = btn.dataset.cmd;
        const res = await IcpApi.executeCommand(id, cmd, 0);
        flash(
          (res.data && res.data.message) || (res.ok ? cmd + " ok" : cmd + " failed"),
          res.ok && res.data && res.data.ok !== false ? "ok" : "error"
        );
        await render();
        return;
      }
      if (action === "remove-adapter") {
        if (!confirm("Remove adapter " + id + "? This cannot be undone.")) return;
        await IcpApi.removeAdapter(id);
        await IcpApi.saveConfiguration();
        flash("Adapter removed", "ok");
        await render();
        return;
      }
      if (action === "connect" || action === "disconnect" || action === "reconnect") {
        const fn =
          action === "connect"
            ? IcpApi.connectAdapter
            : action === "disconnect"
            ? IcpApi.disconnectAdapter
            : IcpApi.reconnectAdapter;
        const res = await fn(id);
        if (!res.ok || (res.data && res.data.ok === false)) {
          flash((res.data && res.data.message) || action + " failed", "error");
        } else {
          flash(action + " succeeded", "ok");
        }
        await render();
        return;
      }
      if (action === "cfg-validate") {
        const res = await IcpApi.validateConfiguration();
        $("#cfg-validation").innerHTML = res.data.ok
          ? statusBadge("ok") + " " + esc(res.data.message || "ok")
          : formatIssues(res.data);
        return;
      }
      if (action === "cfg-save") {
        const res = await IcpApi.saveConfiguration();
        flash(res.data.message || (res.ok ? "Saved" : "Save failed"), res.ok ? "ok" : "error");
        return;
      }
      if (action === "cfg-load") {
        const res = await IcpApi.loadConfiguration();
        flash(res.data.message || (res.ok ? "Loaded" : "Load failed"), res.ok ? "ok" : "error");
        await render();
        return;
      }
      if (action === "cfg-apply") {
        const text = $("#cfg-editor").value;
        const doc = JSON.parse(text);
        const res = await IcpApi.putConfiguration(doc);
        flash(res.data.message || (res.ok ? "Applied" : "Apply failed"), res.ok ? "ok" : "error");
        if (!res.ok) $("#cfg-validation").innerHTML = formatIssues(res.data);
        else await render();
        return;
      }
      if (action === "cfg-import") {
        const text = $("#cfg-import").value;
        const res = await IcpApi.importConfiguration(text);
        flash(res.data.message || (res.ok ? "Imported" : "Import failed"), res.ok ? "ok" : "error");
        await render();
        return;
      }
      if (action === "cfg-export") {
        const res = await IcpApi.exportConfiguration();
        $("#cfg-editor").value = JSON.stringify(res.data, null, 2);
        flash("Exported into editor", "ok");
        return;
      }
    } catch (e) {
      flash(e.message || String(e), "error");
    }
  }

  document.addEventListener("click", (ev) => {
    const btn = ev.target.closest("[data-action]");
    if (!btn) return;
    onAction(btn.dataset.action, btn.dataset.id, btn);
  });

  $("#btn-refresh").addEventListener("click", () => render());

  window.addEventListener("hashchange", () => {
    parseHash();
    render();
  });

  function startPolling() {
    if (state.pollTimer) clearInterval(state.pollTimer);
    state.pollTimer = setInterval(() => {
      if (document.hidden) return;
      if (["dashboard", "equipment", "connections", "diagnostics", "events", "adapters"].includes(
        state.route
      )) {
        render();
      }
    }, 2000);
  }

  parseHash();
  render();
  startPolling();
})();
