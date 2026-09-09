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
    _editingAdapter: null,
    _cfgDraft: null,
    _cfgEditorDirty: false,
    _lastRefreshAt: null,
    _renderGeneration: 0,
    _chooser: null,
    _diagAdapterId: null,
    _diagEquipmentId: null,
    _diagSeverityFilter: "all",
    _diagAdapterFilter: "all",
  };

  const APPEARANCE_STORAGE_KEY = "icp.gui.appearance";
  const DEFAULT_APPEARANCE = Object.freeze({
    bg: "#0a0e14",
    bgPanel: "#141b24",
    bgElevated: "#1c2531",
    bgInput: "#0c1118",
    border: "#3d4b5c",
    text: "#f2f6fa",
    muted: "#aebccd",
    accent: "#2f9e7a",
    fontFamily: '"IBM Plex Sans", "Segoe UI", sans-serif',
    monoFamily: '"IBM Plex Mono", "Consolas", monospace',
    fontScale: "1",
  });

  let renderChain = Promise.resolve();

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

  function normalizeHexColor(value, fallback) {
    const raw = String(value == null ? "" : value).trim();
    if (/^#[0-9a-fA-F]{6}$/.test(raw)) return raw.toLowerCase();
    if (/^[0-9a-fA-F]{6}$/.test(raw)) return ("#" + raw).toLowerCase();
    return fallback;
  }

  function normalizeFontScale(value) {
    const n = Number(value);
    if (!Number.isFinite(n) || n < 0.85 || n > 1.25) return DEFAULT_APPEARANCE.fontScale;
    return String(Math.round(n * 100) / 100);
  }

  function sanitizeAppearance(raw) {
    const src = raw && typeof raw === "object" ? raw : {};
    return {
      bg: normalizeHexColor(src.bg, DEFAULT_APPEARANCE.bg),
      bgPanel: normalizeHexColor(src.bgPanel, DEFAULT_APPEARANCE.bgPanel),
      bgElevated: normalizeHexColor(src.bgElevated, DEFAULT_APPEARANCE.bgElevated),
      bgInput: normalizeHexColor(src.bgInput, DEFAULT_APPEARANCE.bgInput),
      border: normalizeHexColor(src.border, DEFAULT_APPEARANCE.border),
      text: normalizeHexColor(src.text, DEFAULT_APPEARANCE.text),
      muted: normalizeHexColor(src.muted, DEFAULT_APPEARANCE.muted),
      accent: normalizeHexColor(src.accent, DEFAULT_APPEARANCE.accent),
      fontFamily: String(src.fontFamily || DEFAULT_APPEARANCE.fontFamily).trim() ||
        DEFAULT_APPEARANCE.fontFamily,
      monoFamily: String(src.monoFamily || DEFAULT_APPEARANCE.monoFamily).trim() ||
        DEFAULT_APPEARANCE.monoFamily,
      fontScale: normalizeFontScale(src.fontScale),
    };
  }

  function loadAppearance() {
    try {
      const raw = localStorage.getItem(APPEARANCE_STORAGE_KEY);
      if (!raw) return sanitizeAppearance(DEFAULT_APPEARANCE);
      return sanitizeAppearance(JSON.parse(raw));
    } catch (_) {
      return sanitizeAppearance(DEFAULT_APPEARANCE);
    }
  }

  function saveAppearance(appearance) {
    const next = sanitizeAppearance(appearance);
    localStorage.setItem(APPEARANCE_STORAGE_KEY, JSON.stringify(next));
    return next;
  }

  function applyAppearance(appearance) {
    const a = sanitizeAppearance(appearance);
    const root = document.documentElement;
    root.style.setProperty("--bg", a.bg);
    root.style.setProperty("--bg-panel", a.bgPanel);
    root.style.setProperty("--bg-elevated", a.bgElevated);
    root.style.setProperty("--bg-input", a.bgInput);
    root.style.setProperty("--border", a.border);
    root.style.setProperty("--text", a.text);
    root.style.setProperty("--muted", a.muted);
    root.style.setProperty("--accent", a.accent);
    root.style.setProperty("--font", a.fontFamily);
    root.style.setProperty("--mono", a.monoFamily);
    root.style.setProperty("--font-scale", a.fontScale);
    root.style.setProperty("--ui-bg", a.bg);
    root.style.setProperty("--ui-bg-panel", a.bgPanel);
    root.style.setProperty("--ui-bg-elevated", a.bgElevated);
    root.style.setProperty("--ui-bg-input", a.bgInput);
    root.style.setProperty("--ui-border", a.border);
    root.style.setProperty("--ui-text", a.text);
    root.style.setProperty("--ui-muted", a.muted);
    root.style.setProperty("--ui-accent", a.accent);
    root.style.setProperty("--ui-font", a.fontFamily);
    root.style.setProperty("--ui-mono", a.monoFamily);
    return a;
  }

  function readAppearanceForm() {
    const get = (id) => {
      const el = document.getElementById(id);
      return el ? el.value : "";
    };
    return sanitizeAppearance({
      bg: get("ap-bg-hex") || get("ap-bg"),
      bgPanel: get("ap-bg-panel-hex") || get("ap-bg-panel"),
      bgElevated: get("ap-bg-elevated-hex") || get("ap-bg-elevated"),
      bgInput: get("ap-bg-input-hex") || get("ap-bg-input"),
      border: get("ap-border-hex") || get("ap-border"),
      text: get("ap-text-hex") || get("ap-text"),
      muted: get("ap-muted-hex") || get("ap-muted"),
      accent: get("ap-accent-hex") || get("ap-accent"),
      fontFamily: get("ap-font"),
      monoFamily: get("ap-mono"),
      fontScale: get("ap-font-scale"),
    });
  }

  function appearanceColorField(id, label, value) {
    return `<div class="field">
      <label class="field-label" for="${id}-hex">${esc(label)}</label>
      <div class="field-hex">
        <input type="color" id="${id}" data-appearance-color="${id}" value="${esc(value)}" aria-label="${esc(label)} color"/>
        <input type="text" id="${id}-hex" data-appearance-hex="${id}" value="${esc(value)}" spellcheck="false"/>
      </div>
    </div>`;
  }

  function appearanceFormHtml(appearance) {
    const a = sanitizeAppearance(appearance);
    const scales = ["0.9", "0.95", "1", "1.05", "1.1", "1.15"];
    const scaleOpts = scales
      .map((s) => {
        const sel = String(a.fontScale) === s ? " selected" : "";
        const pct = Math.round(Number(s) * 100);
        return `<option value="${s}"${sel}>${pct}%</option>`;
      })
      .join("");
    return `<div class="panel" id="appearance-settings">
      <h2>Appearance</h2>
      <p class="appearance-lead">Appearance settings affect this browser only. They are saved locally and do not change ICP configuration, adapters, or runtime behavior.</p>
      <div class="form-grid appearance-grid">
        ${appearanceColorField("ap-bg", "Page background", a.bg)}
        ${appearanceColorField("ap-bg-panel", "Panel background", a.bgPanel)}
        ${appearanceColorField("ap-bg-elevated", "Elevated / button surface", a.bgElevated)}
        ${appearanceColorField("ap-bg-input", "Input background", a.bgInput)}
        ${appearanceColorField("ap-border", "Border", a.border)}
        ${appearanceColorField("ap-text", "Primary text", a.text)}
        ${appearanceColorField("ap-muted", "Muted text", a.muted)}
        ${appearanceColorField("ap-accent", "Accent", a.accent)}
        <div class="field full">
          <label class="field-label" for="ap-font">UI font family</label>
          <input id="ap-font" type="text" value="${esc(a.fontFamily)}" spellcheck="false"/>
        </div>
        <div class="field full">
          <label class="field-label" for="ap-mono">Monospace font family</label>
          <input id="ap-mono" type="text" value="${esc(a.monoFamily)}" spellcheck="false"/>
        </div>
        <div class="field">
          <label class="field-label" for="ap-font-scale">UI scale</label>
          <select id="ap-font-scale">${scaleOpts}</select>
        </div>
      </div>
      <div class="row-actions">
        <button type="button" class="primary" data-action="appearance-apply">Apply</button>
        <button type="button" data-action="appearance-reset">Reset to Default</button>
      </div>
    </div>`;
  }

  function bindAppearanceHandlers() {
    const root = document.getElementById("appearance-settings");
    if (!root || root.dataset.bound === "1") return;
    root.dataset.bound = "1";

    root.addEventListener("input", (ev) => {
      const t = ev.target;
      if (!t) return;
      if (t.dataset.appearanceColor) {
        const hex = document.getElementById(t.id + "-hex");
        if (hex) hex.value = t.value;
      } else if (t.dataset.appearanceHex) {
        const normalized = normalizeHexColor(t.value, "");
        if (!normalized) return;
        const color = document.getElementById(t.dataset.appearanceHex);
        if (color) color.value = normalized;
        t.value = normalized;
      }
    });
  }

  function statusBadge(value) {
    const v = value || "UNKNOWN";
    return `<span class="status ${esc(v)}">${esc(v.replace(/_/g, " "))}</span>`;
  }

  function adapterConnectionBadge(adapter) {
    const display =
      adapter.connectionStateDisplay || adapter.connectionState || "UNKNOWN";
    return statusBadge(display);
  }

  /** Normalize adapter connection state for UI action enablement (GUI-only). */
  function adapterUiConnectionKind(adapter) {
    const raw = String(
      adapter.connectionState || adapter.connectionStateDisplay || "UNKNOWN"
    )
      .trim()
      .toUpperCase()
      .replace(/\s+/g, "_");
    if (raw === "CONNECTED" || raw === "SIMULATED_ACTIVE") return "connected";
    if (raw === "CONNECTING") return "connecting";
    if (raw === "FAULTED" || raw === "FAILED") return "faulted";
    return "disconnected";
  }

  /**
   * Connect / Disconnect / Reconnect (and optional Edit/Remove) reflecting
   * live adapter state. Do not disable recovery merely because state is FAULTED.
   */
  function adapterLifecycleButtonsHtml(adapter, options) {
    const opts = options || {};
    const id = esc(adapter.adapterId);
    const kind = adapterUiConnectionKind(adapter);
    const connectDisabled = kind === "connected" || kind === "connecting";
    // Disconnect must remain available for FAULTED so operators can clear a
    // broken session before Connect/Reconnect.
    const disconnectDisabled = kind === "disconnected";
    const connectAttr = connectDisabled
      ? ' disabled aria-disabled="true" title="Already connected"'
      : "";
    const disconnectAttr = disconnectDisabled
      ? ' disabled aria-disabled="true" title="Not connected"'
      : "";
    // Reconnect remains available: existing semantics are explicit disconnect+connect.
    let html =
      `<button type="button" data-action="connect" data-id="${id}"${connectAttr}>Connect</button>` +
      `<button type="button" data-action="disconnect" data-id="${id}"${disconnectAttr}>Disconnect</button>` +
      `<button type="button" data-action="reconnect" data-id="${id}">Reconnect</button>`;
    if (opts.includeEditorActions) {
      html +=
        `<button type="button" data-action="edit-adapter" data-id="${id}">Edit</button>` +
        `<button type="button" class="danger" data-action="remove-adapter" data-id="${id}">Remove</button>`;
    }
    return html;
  }

  function equipmentCommBadge(equipment) {
    const display =
      equipment.communicationStateDisplay || equipment.communicationState || "UNKNOWN";
    return statusBadge(display);
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

  const Form = window.IcpAdapterForm;

  function editorContext(adapter) {
    return adapter || state._editingAdapter || { protocol: "mock", connection: {} };
  }

  function implementationLabel(impl) {
    return Form.IMPLEMENTATION_LABELS[impl] || impl || "—";
  }

  function renderChooserOverlay() {
    if (!state._chooser) return "";
    if (state._chooser.step === "protocol") {
      const catalog = state.protocols.length
        ? state.protocols.map((p) => p.id)
        : Form.protocolsList();
      const items = catalog.map((id) => {
        const info = Form.PROTOCOL_INFO[id] || {};
        const label = Form.PROTOCOL_LABELS[id] ||
          (state.protocols.find((p) => p.id === id) || {}).label ||
          id;
        let badgeClass = "ok";
        if (info.support === "Gateway") badgeClass = "gateway";
        else if (info.next === "implementation") badgeClass = "muted";
        return `<button type="button" class="chooser-item" data-action="choose-protocol" data-protocol="${esc(
          id
        )}">
          <span class="chooser-item-head">
            <span class="chooser-title">${esc(label)}</span>
            <span class="badge ${badgeClass}">${esc(info.support || "Supported")}</span>
          </span>
          <span class="chooser-desc">${esc(info.description || "")}</span>
        </button>`;
      });
      return `<div class="modal-backdrop" data-action="close-chooser">
        <div class="modal chooser-modal" role="dialog" aria-labelledby="chooser-title" data-action="stop-modal">
          <h2 id="chooser-title">Add Industrial Adapter</h2>
          <p class="muted">Select the industrial protocol for this connection.</p>
          <div class="chooser-grid">${items.join("")}</div>
          <div class="row-actions"><button type="button" data-action="close-chooser">Cancel</button></div>
        </div>
      </div>`;
    }
    if (state._chooser.step === "transport" && state._chooser.protocol === "modbus") {
      const transports = Form.MODBUS_TRANSPORT_INFO || {};
      const items = Object.keys(transports).map((key) => {
        const info = transports[key];
        const disabled = !info.available;
        return `<button type="button" class="chooser-item${disabled ? " disabled" : ""}" data-action="choose-modbus-transport" data-transport="${esc(
          key
        )}" ${disabled ? "aria-disabled=\"true\"" : ""}>
          <span class="chooser-item-head">
            <span class="chooser-title">${esc(info.title)}</span>
            <span class="badge ${info.available ? "ok" : "muted"}">${esc(info.support)}</span>
          </span>
          <span class="chooser-desc">${esc(info.description)}</span>
        </button>`;
      });
      return `<div class="modal-backdrop" data-action="close-chooser">
        <div class="modal chooser-modal" role="dialog" aria-labelledby="transport-title" data-action="stop-modal">
          <h2 id="transport-title">Modbus</h2>
          <p class="muted">Select the communication method. Modbus RTU is the serial protocol commonly deployed over RS-485 physical networks.</p>
          <div class="chooser-list">${items.join("")}</div>
          <div class="row-actions">
            <button type="button" data-action="back-chooser">Back</button>
            <button type="button" data-action="close-chooser">Cancel</button>
          </div>
        </div>
      </div>`;
    }
    if (state._chooser.step === "implementation") {
      const proto = state._chooser.protocol;
      return `<div class="modal-backdrop" data-action="close-chooser">
        <div class="modal chooser-modal" role="dialog" aria-labelledby="impl-title" data-action="stop-modal">
          <h2 id="impl-title">${esc(Form.PROTOCOL_LABELS[proto] || proto)}</h2>
          <p class="muted">Gateway Integration — ICP communicates with an industrial gateway over a supported northbound interface. Native fieldbus adapters are not available for live use in this release.</p>
          <div class="chooser-list">
            <button type="button" class="chooser-item" data-action="choose-implementation" data-implementation="gateway">
              <span class="chooser-item-head">
                <span class="chooser-title">Gateway</span>
                <span class="badge ok">Available</span>
              </span>
              <span class="chooser-desc">Configure integration through a supported industrial gateway. ICP uses the gateway’s northbound interface (OPC UA by default).</span>
            </button>
            <button type="button" class="chooser-item disabled" data-action="choose-implementation" data-implementation="hilscher_native">
              <span class="chooser-item-head">
                <span class="chooser-title">Hilscher native</span>
                <span class="badge muted">Coming Soon</span>
              </span>
              <span class="chooser-desc">Native fieldbus via Hilscher hardware. Not available for configuration in this GUI milestone.</span>
            </button>
            <button type="button" class="chooser-item disabled" data-action="choose-implementation" data-implementation="softing_native">
              <span class="chooser-item-head">
                <span class="chooser-title">Softing native</span>
                <span class="badge muted">Coming Soon</span>
              </span>
              <span class="chooser-desc">Native fieldbus via Softing. Not available in this ICP release.</span>
            </button>
          </div>
          <div class="row-actions">
            <button type="button" data-action="back-chooser">Back</button>
            <button type="button" data-action="close-chooser">Cancel</button>
          </div>
        </div>
      </div>`;
    }
    return "";
  }

  function openAddAdapterEditor(protocol, implementation, transport) {
    state._chooser = null;
    const opts = {};
    if (implementation && (protocol === "profinet" || protocol === "profibus")) {
      opts.implementation = implementation;
    }
    if (protocol === "modbus" && transport) {
      opts.transport = transport;
    }
    state._editingAdapter = Form.defaultAdapter(protocol, undefined, opts);
    state._editingAdapter._edit = false;
  }

  function fieldErrorClass(fieldId, errorMap) {
    return errorMap && errorMap[fieldId] ? " field-error" : "";
  }

  function fieldErrorMsg(fieldId, errorMap) {
    if (!errorMap || !errorMap[fieldId]) return "";
    return `<div class="field-error-msg">${esc(errorMap[fieldId].join("; "))}</div>`;
  }

  function inputVal(obj, key) {
    const v = obj && obj[key];
    if (v == null || v === false) return "";
    if (typeof v === "boolean") return v ? "true" : "false";
    return String(v);
  }

  function captureAdapterFormDraft() {
    if (!state._editingAdapter) {
      return;
    }
    if (!document.getElementById("adapter-editor")) {
      return;
    }
    try {
      const draft = readAdapterForm();
      draft._edit = state._editingAdapter._edit;
      draft._validationIssues = state._editingAdapter._validationIssues;
      draft._advancedOpen = state._editingAdapter._advancedOpen;
      state._editingAdapter = draft;
    } catch (_) {
      /* keep prior draft if form temporarily invalid */
    }
  }

  function applyProtocolChange(newProtocol) {
    const prev = state._editingAdapter;
    if (!prev) {
      state._editingAdapter = Form.defaultAdapter(newProtocol);
      return;
    }
    if (prev.protocol === newProtocol) {
      return;
    }
    // Capture identity fields from DOM before reset.
    const idEl = document.getElementById("f-id");
    const descEl = document.getElementById("f-desc");
    const enEl = document.getElementById("f-enabled");
    const kept = {
      adapterId: idEl ? idEl.value.trim() : prev.adapterId,
      description: descEl ? descEl.value : prev.description,
      enabled: enEl ? enEl.value === "true" : prev.enabled !== false,
      credentials: prev.credentials || {},
      _edit: prev._edit,
      _advancedOpen: prev._advancedOpen,
    };
    const merged = Form.applyProtocolChange(
      Object.assign({}, prev, kept),
      newProtocol
    );
    state._editingAdapter = merged;
  }

  function captureConfigurationDraft() {
    const editor = document.getElementById("cfg-editor");
    if (editor && state._cfgEditorDirty) {
      state._cfgDraft = editor.value;
    }
  }

  function connectionFieldsHtml(adapter, connection, errorMap) {
    const fields = Form.connectionFieldsFor(editorContext(adapter));
    if (!fields.length) {
      return `<p class="protocol-hint full">Mock adapters have no external connection parameters (simulation).</p>`;
    }
    return fields
      .map((f) => {
        const id = "f-conn-" + f.key;
        const req = f.required ? ' <span class="req">*</span>' : "";
        const help = f.help ? `<span class="field-help">${esc(f.help)}</span>` : "";
        const val = connection && connection[f.key];
        let control;
        if (f.type === "bool") {
          const on = val === true || val === "true";
          control = `<select id="${id}" data-conn="${esc(f.key)}"><option value="false" ${
            !on ? "selected" : ""
          }>No</option><option value="true" ${on ? "selected" : ""}>Yes</option></select>`;
        } else {
          control = `<input id="${id}" data-conn="${esc(f.key)}" type="${
            f.type === "number" ? "number" : "text"
          }" value="${esc(inputVal(connection, f.key))}" placeholder="${esc(
            f.placeholder || ""
          )}"/>`;
        }
        return `<label class="${fieldErrorClass(id, errorMap)}">${esc(f.label)}${req}${control}${help}${fieldErrorMsg(
          id,
          errorMap
        )}</label>`;
      })
      .join("");
  }

  function mappingFieldControl(f, id, value, dataAttr) {
    if (f.options && Array.isArray(f.options)) {
      const opts = f.options
        .map((opt) => {
          const v = typeof opt === "string" ? opt : opt.value;
          const lab = typeof opt === "string" ? opt : opt.label;
          const sel = String(value == null ? "" : value) === String(v) ? " selected" : "";
          return `<option value="${esc(v)}"${sel}>${esc(lab)}</option>`;
        })
        .join("");
      return `<select id="${id}" ${dataAttr}>${opts}</select>`;
    }
    return `<input id="${id}" ${dataAttr} type="${
      f.type === "number" ? "number" : "text"
    }" value="${esc(value == null ? "" : value)}" placeholder="${esc(f.placeholder || "")}"/>`;
  }

  function usesOpcUaMappingLayout(adapter) {
    const ctx = editorContext(adapter);
    const protocol = ctx.protocol || "mock";
    if (protocol === "opcua") return true;
    if (
      (protocol === "profinet" || protocol === "profibus") &&
      Form.resolveImplementation(ctx) === "gateway"
    ) {
      return true;
    }
    return false;
  }

  function opcUaMappingHeaderHtml(kind) {
    if (kind === "command") {
      return `<div class="mapping-header mapping-layout-opcua cmd-header" role="row">
        <div class="mapping-col-head">Command <span class="req">*</span></div>
        <div class="mapping-col-head">NodeId</div>
        <div class="mapping-col-head mapping-col-action">Action</div>
      </div>`;
    }
    return `<div class="mapping-header mapping-layout-opcua tel-header" role="row">
      <div class="mapping-col-head">Name <span class="req">*</span></div>
      <div class="mapping-col-head">Unit</div>
      <div class="mapping-col-head">NodeId <span class="req">*</span></div>
      <div class="mapping-col-head mapping-col-action">Action</div>
    </div>`;
  }

  function telemetryRowHtml(adapter, tel, eqIdx, telIdx, errorMap) {
    const fields = Form.telemetryFieldsFor(editorContext(adapter));
    const protocol = editorContext(adapter).protocol || "mock";
    const opcLayout = usesOpcUaMappingLayout(adapter);
    const cells = fields
      .map((f) => {
        const id = `f-eq-${eqIdx}-tel-${telIdx}-${f.key}`;
        const req = f.required ? ' <span class="req">*</span>' : "";
        const help =
          !opcLayout && f.help ? `<span class="field-help">${esc(f.help)}</span>` : "";
        const control = mappingFieldControl(
          f,
          id,
          tel[f.key],
          `data-tel-field="${esc(f.key)}"`
        );
        const wideClass = f.wide ? " mapping-field-wide" : "";
        if (opcLayout) {
          return `<div class="mapping-cell${wideClass}${fieldErrorClass(
            id,
            errorMap
          )}" data-label="${esc(f.label)}${f.required ? " *" : ""}">${control}${fieldErrorMsg(
            id,
            errorMap
          )}</div>`;
        }
        return `<label class="mapping-field${wideClass} ${fieldErrorClass(id, errorMap)}">${esc(
          f.label
        )}${req}${control}${help}${fieldErrorMsg(id, errorMap)}</label>`;
      })
      .join("");
    const addressingNote =
      protocol === "modbus"
        ? `<p class="field-help full">Addresses are 0-based protocol addresses. Vendor manuals may show 40001-style numbers; configure the zero-based wire address here.</p>`
        : "";
    const layoutClass = opcLayout ? " mapping-layout-opcua protocol-opcua" : ` protocol-${esc(protocol)}`;
    return `<div class="tel-row${layoutClass}" data-eq="${eqIdx}" data-tel="${telIdx}">${addressingNote}${cells}
      <div class="row-actions mapping-actions"><button type="button" class="btn-row-danger" data-action="remove-telemetry" data-eq="${eqIdx}" data-tel="${telIdx}">Remove telemetry</button></div>
    </div>`;
  }

  function commandRowHtml(adapter, cmd, eqIdx, cmdIdx, errorMap) {
    const fields = Form.commandFieldsFor(editorContext(adapter));
    const protocol = editorContext(adapter).protocol || "mock";
    const opcLayout = usesOpcUaMappingLayout(adapter);
    const cells = fields
      .map((f) => {
        const id = `f-eq-${eqIdx}-cmd-${cmdIdx}-${f.key}`;
        const req = f.required ? ' <span class="req">*</span>' : "";
        const help =
          !opcLayout && f.help ? `<span class="field-help">${esc(f.help)}</span>` : "";
        const control = mappingFieldControl(
          f,
          id,
          cmd[f.key],
          `data-cmd-field="${esc(f.key)}"`
        );
        const wideClass = f.wide ? " mapping-field-wide" : "";
        if (opcLayout) {
          return `<div class="mapping-cell${wideClass}${fieldErrorClass(
            id,
            errorMap
          )}" data-label="${esc(f.label)}${f.required ? " *" : ""}">${control}${fieldErrorMsg(
            id,
            errorMap
          )}</div>`;
        }
        return `<label class="mapping-field${wideClass} ${fieldErrorClass(id, errorMap)}">${esc(
          f.label
        )}${req}${control}${help}${fieldErrorMsg(id, errorMap)}</label>`;
      })
      .join("");
    const layoutClass = opcLayout ? " mapping-layout-opcua protocol-opcua" : ` protocol-${esc(protocol)}`;
    return `<div class="cmd-row${layoutClass}" data-eq="${eqIdx}" data-cmd="${cmdIdx}">${cells}
      <div class="row-actions mapping-actions"><button type="button" class="btn-row-danger" data-action="remove-command" data-eq="${eqIdx}" data-cmd="${cmdIdx}">Remove command</button></div>
    </div>`;
  }

  function equipmentCardHtml(adapter, eq, eqIdx, errorMap) {
    const ctx = editorContext(adapter);
    const protocol = ctx.protocol || "mock";
    const impl = Form.resolveImplementation(ctx);
    const extras = Form.equipmentExtraFields(ctx);
    const extraHtml = extras
      .map((f) => {
        const id = `f-eq-${eqIdx}-${f.key}`;
        const req = f.required ? ' <span class="req">*</span>' : "";
        return `<label class="${fieldErrorClass(id, errorMap)}">${esc(f.label)}${req}<input id="${id}" data-eq-field="${esc(
          f.key
        )}" type="${f.type === "number" ? "number" : "text"}" value="${esc(
          inputVal(eq, f.key)
        )}" placeholder="${esc(f.placeholder || "")}"/>${fieldErrorMsg(id, errorMap)}</label>`;
      })
      .join("");
    const idEq = `f-eq-${eqIdx}-equipmentId`;
    const idType = `f-eq-${eqIdx}-type`;
    const idCaps = `f-eq-${eqIdx}-capabilities`;
    const tel = (eq.telemetry || [])
      .map((t, i) => telemetryRowHtml(adapter, t, eqIdx, i, errorMap))
      .join("");
    const cmds = (eq.commands || [])
      .map((c, i) => commandRowHtml(adapter, c, eqIdx, i, errorMap))
      .join("");
    const opcLayout = usesOpcUaMappingLayout(adapter);
    const telHeader = opcLayout && tel ? opcUaMappingHeaderHtml("telemetry") : "";
    const cmdHeader = opcLayout && cmds ? opcUaMappingHeaderHtml("command") : "";
    const opcHelp = opcLayout
      ? `<p class="mapping-section-help">Expanded NodeId (<code>ns=N;s=Identifier</code>). Namespace is taken from the NodeId.</p>`
      : "";
    let modulesHtml = "";
    if (protocol === "profinet" && impl === "hilscher_native") {
      modulesHtml = `<label class="full">Submodules JSON<textarea id="f-eq-${eqIdx}-submodules" class="mono" rows="3">${esc(
        JSON.stringify(eq.submodules || [], null, 2)
      )}</textarea></label>`;
    } else if (protocol === "profibus" && impl === "hilscher_native") {
      modulesHtml = `<label class="full">Modules JSON<textarea id="f-eq-${eqIdx}-modules" class="mono" rows="3">${esc(
        JSON.stringify(eq.modules || [], null, 2)
      )}</textarea></label>`;
    }
    return `<div class="eq-card" data-eq-index="${eqIdx}">
      <h4>Equipment ${eqIdx + 1}</h4>
      <div class="form-grid">
        <label class="${fieldErrorClass(idEq, errorMap)}">Equipment ID <span class="req">*</span>
          <input id="${idEq}" data-eq-field="equipmentId" value="${esc(eq.equipmentId || "")}"/>${fieldErrorMsg(idEq, errorMap)}</label>
        <label class="${fieldErrorClass(idType, errorMap)}">Type <span class="req">*</span>
          <input id="${idType}" data-eq-field="type" value="${esc(eq.type || "")}"/>${fieldErrorMsg(idType, errorMap)}</label>
        <label class="full">Capabilities (comma-separated)
          <input id="${idCaps}" data-eq-field="capabilities" value="${esc(
      (eq.capabilities || []).join(", ")
    )}"/></label>
        ${extraHtml}
        ${modulesHtml}
      </div>
      <h4 class="mapping-heading">Telemetry</h4>
      ${opcHelp}
      ${
        tel
          ? `<div class="mapping-list${opcLayout ? " mapping-layout-opcua" : ""} protocol-${esc(
              protocol
            )}">${telHeader}${tel}</div>`
          : '<p class="muted">No telemetry mappings. Add points that this equipment should publish.</p>'
      }
      <div class="row-actions"><button type="button" data-action="add-telemetry" data-eq="${eqIdx}">Add telemetry</button></div>
      <h4 class="mapping-heading">Commands</h4>
      ${opcLayout && cmds ? opcHelp : ""}
      ${
        cmds
          ? `<div class="mapping-list${opcLayout ? " mapping-layout-opcua" : ""} protocol-${esc(
              protocol
            )}">${cmdHeader}${cmds}</div>`
          : '<p class="muted">No commands. Add commands this equipment can execute.</p>'
      }
      <div class="row-actions">
        <button type="button" data-action="add-command" data-eq="${eqIdx}">Add command</button>
        <button type="button" class="danger" data-action="remove-equipment" data-eq="${eqIdx}">Remove equipment</button>
      </div>
    </div>`;
  }

  function adapterFormHtml(adapter, protocols) {
    const Form = window.IcpAdapterForm;
    const errorMap = Form.fieldIdsForIssues(adapter._validationIssues || []);
    const options = protocols
      .map(
        (p) =>
          `<option value="${esc(p.id)}" ${
            adapter.protocol === p.id ? "selected" : ""
          }>${esc(p.label)}</option>`
      )
      .join("");
    const protocol = adapter.protocol || "mock";
    const impl = Form.resolveImplementation(adapter);
    const equipment = adapter.equipment || [];
    const advancedOpen = !!adapter._advancedOpen;
    const creds = adapter.credentials || {};
    const showImpl = protocol === "profinet" || protocol === "profibus";
    const implHtml = showImpl
      ? `<div class="field">
          <label class="field-label" for="f-implementation">Implementation</label>
          <input id="f-implementation" readonly value="${esc(implementationLabel(impl))}"/>
          <input type="hidden" id="f-implementation-value" value="${esc(impl)}"/>
        </div>`
      : "";
    const gatewayNote =
      showImpl && impl === "gateway"
        ? `<p class="protocol-hint">Gateway integration configures the industrial gateway’s northbound interface (OPC UA endpoint by default). ICP does not run a native PROFINET/PROFIBUS stack for this path. For live communication, use a Supported OPC UA, Modbus, MQTT, or REST adapter to that gateway.</p>`
        : "";
    return `
      <div id="adapter-editor">
        <h2>${adapter._edit ? "Edit Adapter" : "Add Adapter"}</h2>
        <section class="editor-section">
          <h3>Basic Information</h3>
          <div class="form-grid">
            <div class="field${fieldErrorClass("f-id", errorMap)}">
              <label class="field-label" for="f-id">Adapter name <span class="req">*</span></label>
              <input id="f-id" value="${esc(adapter.adapterId)}" ${
                adapter._edit ? "readonly" : ""
              }/>${fieldErrorMsg("f-id", errorMap)}
            </div>
            <div class="field">
              <label class="field-label" for="f-enabled">Enabled</label>
              <select id="f-enabled"><option value="true" ${
                adapter.enabled !== false ? "selected" : ""
              }>true</option><option value="false" ${
                adapter.enabled === false ? "selected" : ""
              }>false</option></select>
            </div>
            <div class="field${fieldErrorClass("f-protocol", errorMap)}">
              <label class="field-label" for="f-protocol">Protocol <span class="req">*</span></label>
              <select id="f-protocol">${options}</select>${fieldErrorMsg("f-protocol", errorMap)}
            </div>
            ${implHtml}
            <div class="field full">
              <label class="field-label" for="f-desc">Description</label>
              <input id="f-desc" value="${esc(adapter.description || "")}"/>
            </div>
          </div>
          ${gatewayNote}
        </section>
        <section class="editor-section">
          <h3>Connection</h3>
          <div class="form-grid">
            ${connectionFieldsHtml(adapter, adapter.connection || {}, errorMap)}
          </div>
        </section>
        ${
          (() => {
            const credSupport = Form.credentialsSupport
              ? Form.credentialsSupport(protocol)
              : { username: true, passwordRef: true, tokenRef: true };
            if (!credSupport.username && !credSupport.passwordRef && !credSupport.tokenRef) {
              return "";
            }
            return `<section class="editor-section">
          <h3>Credentials</h3>
          <p class="muted">Optional identity for protocols that support it. Secret references are stored only; live secret resolution is not enabled in this release.</p>
          <div class="form-grid">
          ${
            credSupport.username
              ? `<label class="${fieldErrorClass("f-cred-username", errorMap)}">Username
            <input id="f-cred-username" value="${esc(creds.username || "")}"/>${fieldErrorMsg(
                  "f-cred-username",
                  errorMap
                )}</label>`
              : ""
          }
          ${
            credSupport.passwordRef
              ? `<label class="${fieldErrorClass("f-cred-passwordRef", errorMap)}">Password ref
            <input id="f-cred-passwordRef" value="${esc(creds.passwordRef || "")}" placeholder="env:ICP_PASS"/>${fieldErrorMsg(
                  "f-cred-passwordRef",
                  errorMap
                )}</label>`
              : ""
          }
          ${
            credSupport.tokenRef
              ? `<label class="${fieldErrorClass("f-cred-tokenRef", errorMap)}">Token ref
            <input id="f-cred-tokenRef" value="${esc(creds.tokenRef || "")}" placeholder="env:ICP_TOKEN"/>${fieldErrorMsg(
                  "f-cred-tokenRef",
                  errorMap
                )}</label>`
              : ""
          }
          </div>
        </section>`;
          })()
        }
        <section class="editor-section">
          <h3>Equipment</h3>
          ${
            equipment.length
              ? equipment.map((eq, i) => equipmentCardHtml(adapter, eq, i, errorMap)).join("")
              : '<p class="muted">No equipment configured. Add equipment, then telemetry and commands on each card.</p>'
          }
          <div class="row-actions">
            <button type="button" data-action="add-equipment">Add equipment</button>
          </div>
        </section>
        <section class="editor-section advanced-json">
          <h3>Advanced Configuration
            <button type="button" data-action="toggle-advanced">${
              advancedOpen ? "Hide JSON" : "Show JSON"
            }</button>
          </h3>
          <div id="advanced-json-body" class="${advancedOpen ? "" : "hidden"}">
            <p class="advanced-lead">
              <strong>Expert configuration</strong>
              Edit or inspect the complete adapter configuration as JSON.
              Configure using the form above, or edit the JSON directly and load it back into the form.
            </p>
            <div class="advanced-json-toolbar">
              <button type="button" data-action="gui-to-json" title="Create the adapter configuration JSON from the fields above">
                Generate JSON from Form
              </button>
              <button type="button" data-action="json-to-gui" title="Read the JSON below and populate the editable form fields">
                Load Form from JSON
              </button>
            </div>
            <p class="field-help advanced-btn-help">
              <span><strong>Generate JSON from Form</strong> — create JSON from the fields above.</span>
              <span><strong>Load Form from JSON</strong> — populate the form from the JSON below.</span>
            </p>
            <div class="json-editor-shell">
              <textarea id="f-advanced-json" class="mono json-editor" spellcheck="false" rows="18">${esc(
                JSON.stringify(Form.adapterToConfigJson(adapter), null, 2)
              )}</textarea>
            </div>
            <div id="advanced-json-status" class="json-status muted" role="status" aria-live="polite">JSON status will update as you edit.</div>
          </div>
        </section>
        <div class="editor-actions">
          <button data-action="cancel-editor">Cancel</button>
          <button class="primary" data-action="save-adapter">Save Adapter</button>
        </div>
        <div id="editor-result" class="muted"></div>
      </div>`;
  }

  function readScalar(el) {
    if (!el) return undefined;
    if (el.type === "number") {
      const n = parseInt(el.value, 10);
      return Number.isNaN(n) ? undefined : n;
    }
    const v = el.value.trim();
    return v === "" ? undefined : v;
  }

  function readConnectionFromForm(adapter) {
    const connection = {};
    if ((adapter.protocol || "") === "modbus") {
      connection.transport =
        (adapter.connection && adapter.connection.transport) || "tcp";
    }
    const fields = Form.connectionFieldsFor(editorContext(adapter));
    fields.forEach((f) => {
      const el = document.getElementById("f-conn-" + f.key);
      if (!el) return;
      if (f.type === "bool") {
        connection[f.key] = el.value === "true";
      } else if (f.type === "number") {
        const n = parseInt(el.value, 10);
        if (!Number.isNaN(n)) connection[f.key] = n;
      } else {
        const v = el.value.trim();
        if (v) connection[f.key] = v;
      }
    });
    return connection;
  }

  function readEquipmentFromForm(adapter) {
    const ctx = editorContext(adapter);
    const protocol = ctx.protocol || "mock";
    const impl = Form.resolveImplementation(ctx);
    const cards = [...document.querySelectorAll(".eq-card")];
    return cards.map((card, eqIdx) => {
      const eq = {
        equipmentId: "",
        type: "",
        capabilities: [],
        telemetry: [],
        commands: [],
        state: { mapped: false },
        fault: { mapped: false },
      };
      const idEl = document.getElementById(`f-eq-${eqIdx}-equipmentId`);
      const typeEl = document.getElementById(`f-eq-${eqIdx}-type`);
      const capsEl = document.getElementById(`f-eq-${eqIdx}-capabilities`);
      eq.equipmentId = idEl ? idEl.value.trim() : "";
      eq.type = typeEl ? typeEl.value.trim() : "";
      if (capsEl && capsEl.value.trim()) {
        eq.capabilities = capsEl.value
          .split(",")
          .map((s) => s.trim())
          .filter(Boolean);
      }
      Form.equipmentExtraFields(ctx).forEach((f) => {
        const el = document.getElementById(`f-eq-${eqIdx}-${f.key}`);
        if (!el) return;
        if (f.type === "number") {
          const n = parseInt(el.value, 10);
          if (!Number.isNaN(n)) eq[f.key] = n;
        } else {
          const v = el.value.trim();
          if (v) eq[f.key] = v;
        }
      });
      if (protocol === "profinet" && impl === "hilscher_native") {
        const sub = document.getElementById(`f-eq-${eqIdx}-submodules`);
        if (sub) {
          try {
            eq.submodules = JSON.parse(sub.value || "[]");
          } catch (e) {
            throw new Error("Equipment " + (eqIdx + 1) + " submodules JSON invalid: " + e.message);
          }
        }
      }
      if (protocol === "profibus" && impl === "hilscher_native") {
        const mod = document.getElementById(`f-eq-${eqIdx}-modules`);
        if (mod) {
          try {
            eq.modules = JSON.parse(mod.value || "[]");
          } catch (e) {
            throw new Error("Equipment " + (eqIdx + 1) + " modules JSON invalid: " + e.message);
          }
        }
      }
      const telFields = Form.telemetryFieldsFor(ctx);
      const telRows = card.querySelectorAll(".tel-row");
      telRows.forEach((row, telIdx) => {
        const t = {};
        telFields.forEach((f) => {
          const el = document.getElementById(`f-eq-${eqIdx}-tel-${telIdx}-${f.key}`);
          const v = readScalar(el);
          if (v !== undefined) t[f.key] = v;
        });
        eq.telemetry.push(t);
      });
      const cmdFields = Form.commandFieldsFor(ctx);
      const cmdRows = card.querySelectorAll(".cmd-row");
      cmdRows.forEach((row, cmdIdx) => {
        const c = {};
        cmdFields.forEach((f) => {
          const el = document.getElementById(`f-eq-${eqIdx}-cmd-${cmdIdx}-${f.key}`);
          const v = readScalar(el);
          if (v !== undefined) c[f.key] = v;
        });
        eq.commands.push(c);
      });
      return eq;
    });
  }

  function readAdapterForm() {
    const protocol = ($("#f-protocol") && $("#f-protocol").value) || "mock";
    const implEl = document.getElementById("f-implementation-value");
    const adapter = {
      adapterId: ($("#f-id") && $("#f-id").value.trim()) || "",
      protocol: protocol,
      implementation: implEl ? implEl.value : "",
      description: ($("#f-desc") && $("#f-desc").value) || "",
      enabled: !$("#f-enabled") || $("#f-enabled").value === "true",
      connection: {},
      credentials: {},
      equipment: [],
    };
    adapter.connection = readConnectionFromForm(adapter);
    adapter.equipment = readEquipmentFromForm(adapter);
    const user = document.getElementById("f-cred-username");
    const pass = document.getElementById("f-cred-passwordRef");
    const tok = document.getElementById("f-cred-tokenRef");
    if (user && user.value.trim()) adapter.credentials.username = user.value.trim();
    if (pass && pass.value.trim()) adapter.credentials.passwordRef = pass.value.trim();
    if (tok && tok.value.trim()) adapter.credentials.tokenRef = tok.value.trim();
    return adapter;
  }

  function setAdvancedJsonStatus(ok, message) {
    const el = document.getElementById("advanced-json-status");
    if (!el) return;
    el.className = "json-status " + (ok ? "json-ok" : "json-bad");
    el.textContent = message;
  }

  function validateAdvancedJsonText(text) {
    try {
      const parsed = JSON.parse(text);
      if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
        setAdvancedJsonStatus(false, "✕ Invalid JSON: adapter configuration must be an object");
        return { ok: false, error: "adapter configuration must be an object" };
      }
      setAdvancedJsonStatus(true, "✓ Valid JSON");
      return { ok: true, parsed };
    } catch (e) {
      setAdvancedJsonStatus(false, "✕ Invalid JSON: " + e.message);
      return { ok: false, error: e.message };
    }
  }

  function bindAdvancedJsonEditor() {
    const ta = document.getElementById("f-advanced-json");
    if (!ta || ta.dataset.boundJsonStatus) return;
    ta.dataset.boundJsonStatus = "1";
    const refresh = () => validateAdvancedJsonText(ta.value);
    ta.addEventListener("input", refresh);
    ta.addEventListener("blur", refresh);
    refresh();
  }

  function bindPostRenderHandlers() {
    const protoEl = document.getElementById("f-protocol");
    if (protoEl && !protoEl.dataset.bound) {
      protoEl.dataset.bound = "1";
      protoEl.addEventListener("change", async () => {
        applyProtocolChange(protoEl.value);
        await render({ skipDraftCapture: true });
        flash("Protocol changed to " + protoEl.value + " — connection/equipment reset to defaults", "ok");
      });
    }

    const editor = document.getElementById("adapter-editor");
    if (editor && !editor.dataset.boundDraft) {
      editor.dataset.boundDraft = "1";
      editor.addEventListener("input", (ev) => {
        if (ev.target && ev.target.id === "f-advanced-json") return;
        captureAdapterFormDraft();
      });
      editor.addEventListener("change", (ev) => {
        if (ev.target && ev.target.id === "f-advanced-json") return;
        captureAdapterFormDraft();
      });
    }

    const cfgEditor = document.getElementById("cfg-editor");
    if (cfgEditor && !cfgEditor.dataset.bound) {
      cfgEditor.dataset.bound = "1";
      cfgEditor.addEventListener("input", () => {
        state._cfgEditorDirty = true;
        state._cfgDraft = cfgEditor.value;
      });
    }

    bindAdvancedJsonEditor();
    bindAppearanceHandlers();

    const sev = document.getElementById("diag-severity-filter");
    if (sev && !sev.dataset.bound) {
      sev.dataset.bound = "1";
      sev.addEventListener("change", async () => {
        state._diagSeverityFilter = sev.value || "all";
        await render({ skipDraftCapture: true });
      });
    }
    const adp = document.getElementById("diag-adapter-filter");
    if (adp && !adp.dataset.bound) {
      adp.dataset.bound = "1";
      adp.addEventListener("change", async () => {
        state._diagAdapterFilter = adp.value || "all";
        await render({ skipDraftCapture: true });
      });
    }

    // Highlight validation issues after render.
    if (state._editingAdapter && state._editingAdapter._validationIssues) {
      const box = $("#editor-result");
      if (box) {
        box.innerHTML =
          "<strong>Validation failed</strong>" +
          formatIssues({ issues: state._editingAdapter._validationIssues });
      }
    }
  }

  function defaultAdapter(protocol) {
    return Form.defaultAdapter(protocol);
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
        <div class="row-actions"><button class="primary" data-action="goto-adapters">+ Add Adapter</button></div>
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

  async function renderAdapters() {
    const [ad, proto] = await Promise.all([IcpApi.adapters(), IcpApi.protocols()]);
    state.protocols = (proto.data && proto.data.protocols) || [];
    const adapters = (ad.data && ad.data.adapters) || [];
    let html = `<div class="toolbar">
      <button class="primary" data-action="open-add-chooser">+ Add Adapter</button>
      <span class="muted">Choose protocol and implementation from the Add Industrial Adapter dialog.</span>
    </div>`;

    if (!adapters.length) {
      html += `<div class="empty"><strong>No adapters configured</strong>Click <strong>+ Add Adapter</strong> to create one.</div>`;
    } else {
      html += `<div class="panel"><table><thead><tr>
        <th>Adapter</th><th>Protocol</th><th>Transport</th><th>Implementation</th><th>State</th><th>Enabled</th><th>Equipment</th><th>Error</th><th>Actions</th>
      </tr></thead><tbody>`;
      html += adapters
        .map((a) => {
          const transportLabel =
            a.protocol === "modbus"
              ? a.transport === "rtu"
                ? "RTU / RS-485"
                : "TCP"
              : "—";
          const implLabel =
            a.protocol === "modbus"
              ? a.transport === "rtu"
                ? "Modbus RTU"
                : "Modbus TCP"
              : implementationLabel(a.implementation);
          return `<tr>
          <td><a href="#/adapters/${esc(a.adapterId)}">${esc(a.adapterId)}</a></td>
          <td>${esc(a.protocol === "modbus" ? "Modbus" : a.protocol)}</td>
          <td>${esc(transportLabel)}</td>
          <td>${esc(implLabel)}</td>
          <td>${adapterConnectionBadge(a)}</td>
          <td>${a.enabled ? "yes" : "no"}</td>
          <td>${esc(a.equipmentCount)}</td>
          <td class="mono">${esc(a.lastError || "")}</td>
          <td class="table-actions">${adapterLifecycleButtonsHtml(a, {
            includeEditorActions: true,
          })}</td>
        </tr>`;
        })
        .join("");
      html += `</tbody></table></div>`;
    }

    if (state._editingAdapter) {
      html += adapterFormHtml(state._editingAdapter, state.protocols);
    }
    html += renderChooserOverlay();
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
          .join("") || `<tr><td colspan="3" class="muted">No telemetry received yet</td></tr>`;
      const canCommand =
        (e.communicationState === "CONNECTED" ||
          e.communicationStateDisplay === "SIMULATED_ACTIVE") &&
        !e.stale;
      const cmdBar = canCommand
        ? `<div class="toolbar">
            <button data-action="eq-cmd" data-id="${esc(e.equipmentId)}" data-cmd="start">Start</button>
            <button data-action="eq-cmd" data-id="${esc(e.equipmentId)}" data-cmd="stop">Stop</button>
          </div>`
        : `<p class="muted">Commands available when communication is active (connected or simulated).</p>`;
      return `<div class="toolbar"><a href="#/equipment">← Equipment list</a></div>
        <div class="panel">
          <h2>Equipment ${esc(e.equipmentId)}</h2>
          <dl class="kv">
            <dt>Type</dt><dd>${esc(e.type)}</dd>
            <dt>Adapter</dt><dd>${esc(e.adapterId)}</dd>
            <dt>Protocol</dt><dd>${esc(e.protocol)}${e.protocol === "mock" ? " (simulation)" : ""}</dd>
            <dt>Communication</dt><dd>${equipmentCommBadge(e)}</dd>
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
        <td>${equipmentCommBadge(e)}</td>
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
        <td>${adapterConnectionBadge(a)}</td>
        <td class="mono">${esc(a.lastError || "")}</td>
        <td class="table-actions">${adapterLifecycleButtonsHtml(a)}</td>
      </tr>`
      )
      .join("")}</tbody></table>
      <p class="muted">Reconnect is explicit disconnect then connect. Background auto-reconnect is not enabled.</p>
    </div>`;
  }

  async function renderConfiguration() {
    const [cfg, val, st] = await Promise.all([
      IcpApi.configuration(),
      IcpApi.validateConfiguration(),
      IcpApi.status(),
    ]);
    const status = (st.ok && st.data) || {};
    const text =
      state._cfgEditorDirty && state._cfgDraft != null
        ? state._cfgDraft
        : JSON.stringify(cfg.data || {}, null, 2);
    const loaded = !!status.configurationLoaded;
    const path = status.configurationPath || "";
    const name = status.configurationName || "(unnamed)";
    const loadState = status.configurationLoadState || (loaded ? "loaded" : "not loaded");
    return `<div class="panel">
      <h2>Configuration</h2>
      <p class="muted">This page manages the ICP configuration document — adapters, equipment mappings, and connection settings. It is not MES and not CIC.</p>
      <dl class="kv">
        <dt>Current configuration</dt><dd class="mono">${esc(path || name)}</dd>
        <dt>Name</dt><dd>${esc(name)}</dd>
        <dt>Status</dt><dd>${loaded ? statusBadge("ok") : statusBadge("NOT_CONFIGURED")} ${esc(
          loadState
        )}</dd>
      </dl>
    </div>
    <div class="config-actions">
      <div class="panel">
        <h3>Save configuration</h3>
        <p class="muted">Persist the currently active configuration to the file shown above.</p>
        <button class="primary" data-action="cfg-save">Save Configuration</button>
      </div>
      <div class="panel">
        <h3>Load configuration</h3>
        <p class="muted">Load the configured configuration file from disk into the application.</p>
        <button data-action="cfg-load">Load Configuration</button>
      </div>
      <div class="panel">
        <h3>Import configuration</h3>
        <p class="muted">Bring a configuration JSON document into the application (replaces the in-memory document after validation).</p>
        <textarea id="cfg-import" class="mono" rows="6" placeholder="Paste ICP configuration JSON"></textarea>
        <div class="row-actions"><button data-action="cfg-import">Import Configuration</button></div>
      </div>
      <div class="panel">
        <h3>Export configuration</h3>
        <p class="muted">Download or copy the current configuration as JSON. Secrets are stored as references only.</p>
        <button data-action="cfg-export">Export Configuration</button>
      </div>
    </div>
    <section class="editor-section">
      <h3>Validate</h3>
      <p class="muted">Check the active configuration against ICP-1B rules without writing to disk.</p>
      <div class="row-actions"><button data-action="cfg-validate">Validate</button></div>
      <div id="cfg-validation">${
        val.data && val.data.ok
          ? statusBadge("ok") + " " + esc(val.data.message || "Configuration is valid.")
          : formatIssues(val.data)
      }</div>
    </section>
    <section class="editor-section advanced-json">
      <h3>Advanced document editor</h3>
      <p class="muted">Direct JSON of the whole configuration. Prefer the Adapters page for normal edits. Apply editor writes the JSON into memory after validation; Save writes it to disk.</p>
      <textarea id="cfg-editor" class="mono" rows="18">${esc(text)}</textarea>
      <div class="row-actions"><button data-action="cfg-apply">Apply editor</button></div>
    </section>`;
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

  function formatDurationMs(ms) {
    if (ms == null || ms === "" || Number(ms) < 0 || !Number.isFinite(Number(ms))) {
      return "N/A";
    }
    let sec = Math.floor(Number(ms) / 1000);
    const days = Math.floor(sec / 86400);
    sec %= 86400;
    const hours = Math.floor(sec / 3600);
    sec %= 3600;
    const mins = Math.floor(sec / 60);
    sec %= 60;
    if (days > 0) return days + "d " + hours + "h";
    if (hours > 0) return hours + "h " + mins + "m";
    if (mins > 0) return mins + "m " + sec + "s";
    return sec + "s";
  }

  function na(value, fallback) {
    if (value === null || value === undefined || value === "") {
      return fallback != null ? fallback : "Not available";
    }
    return value;
  }

  function diagSelectHint(kind) {
    const label =
      kind === "equipment"
        ? "SELECT AN EQUIPMENT ROW TO VIEW DETAILED DIAGNOSTICS — Click an equipment row for details."
        : "SELECT AN ADAPTER ROW TO VIEW DETAILED DIAGNOSTICS — Click an adapter row for details.";
    return `<div class="diag-select-hint" role="note">
      <span class="diag-select-hint-icon" aria-hidden="true">▾</span>
      <span class="diag-select-hint-text">${label}</span>
    </div>`;
  }

  function diagBarChart(segments) {
    const total = segments.reduce((sum, s) => sum + (Number(s.value) || 0), 0);
    if (!total) {
      return `<div class="diag-chart"><div class="diag-chart-empty">Insufficient data</div></div>`;
    }
    const bars = segments
      .map((s) => {
        const v = Number(s.value) || 0;
        const pct = Math.max(0, Math.round((v / total) * 100));
        return `<div class="diag-bar-seg diag-bar-${esc(s.tone || "unknown")}" style="width:${pct}%" title="${esc(
          s.label
        )}: ${v}"></div>`;
      })
      .join("");
    const legend = segments
      .map(
        (s) =>
          `<span class="diag-legend-item"><span class="diag-swatch diag-bar-${esc(
            s.tone || "unknown"
          )}"></span>${esc(s.label)} (${esc(
            s.display != null ? s.display : s.value || 0
          )})</span>`
      )
      .join("");
    return `<div class="diag-chart">
      <div class="diag-bar">${bars}</div>
      <div class="diag-legend">${legend}</div>
    </div>`;
  }

  function diagMetricBars(items) {
    const max = Math.max(1, ...items.map((i) => Number(i.value) || 0));
    if (!items.some((i) => (Number(i.value) || 0) > 0)) {
      return `<div class="diag-chart"><div class="diag-chart-empty">Insufficient data</div></div>`;
    }
    return `<div class="diag-metric-bars">${items
      .map((i) => {
        const v = Number(i.value) || 0;
        const pct = Math.round((v / max) * 100);
        return `<div class="diag-metric-row">
          <span class="diag-metric-label">${esc(i.label)}</span>
          <span class="diag-metric-track"><span class="diag-metric-fill diag-bar-${esc(
            i.tone || "unknown"
          )}" style="width:${pct}%"></span></span>
          <span class="diag-metric-value">${esc(v)}</span>
        </div>`;
      })
      .join("")}</div>`;
  }

  function diagUptimeBars(adapters) {
    let connected = 0;
    let disconnected = 0;
    (adapters || []).forEach((a) => {
      connected += Number(a.cumulativeConnectedMs) || 0;
      disconnected += Number(a.cumulativeDisconnectedMs) || 0;
    });
    if (connected + disconnected <= 0) {
      return `<div class="diag-chart"><div class="diag-chart-empty">Insufficient data</div></div>`;
    }
    // Guard against legacy epoch-derived garbage if an old process is still running.
    const oneWeekMs = 7 * 24 * 60 * 60 * 1000;
    if (connected > oneWeekMs || disconnected > oneWeekMs) {
      return `<div class="diag-chart"><div class="diag-chart-empty">Insufficient data</div></div>`;
    }
    return diagBarChart([
      {
        label: "Connected",
        value: connected,
        display: formatDurationMs(connected),
        tone: "healthy",
      },
      {
        label: "Disconnected",
        value: disconnected,
        display: formatDurationMs(disconnected),
        tone: "unknown",
      },
    ]);
  }

  function healthBadge(value) {
    const v = String(value || "UNKNOWN").toUpperCase();
    const cls =
      v === "HEALTHY" || v === "OK"
        ? "ok"
        : v === "DEGRADED" || v === "WARNING"
          ? "warn"
          : v === "FAILED" || v === "FAULTED" || v === "CRITICAL" || v === "ERROR"
            ? "error"
            : "";
    return `<span class="status ${cls} ${esc(v)}">${esc(v.replace(/_/g, " "))}</span>`;
  }

  function severityBadge(value) {
    const v = String(value || "INFO").toUpperCase();
    const cls =
      v === "CRITICAL" || v === "ERROR"
        ? "error"
        : v === "WARNING"
          ? "warn"
          : v === "INFO"
            ? "ok"
            : "";
    return `<span class="status ${cls}">${esc(v)}</span>`;
  }

  function relativeTimeLabel(iso) {
    if (!iso) return "Not available";
    const t = Date.parse(iso);
    if (!Number.isFinite(t)) return esc(iso);
    const delta = Math.max(0, Date.now() - t);
    if (delta < 5000) return "just now";
    return formatDurationMs(delta) + " ago";
  }

  function renderImplementationAdapterTable(adapters, title) {
    if (!adapters || !adapters.length) {
      return `<p class="muted">No adapters in this implementation group.</p>`;
    }
    return `<table class="diag-table"><thead><tr><th>Adapter</th><th>Protocol</th><th>Transport</th><th>Implementation</th><th>Endpoint</th><th>State</th><th>Runtime</th><th>Equipment</th><th>Last error</th></tr></thead><tbody>${adapters
      .map((a) => {
        const transportLabel =
          a.protocol === "modbus"
            ? a.transport === "rtu"
              ? "RTU / RS-485"
              : "TCP"
            : "—";
        return `<tr>
        <td>${esc(a.adapterId)}</td>
        <td>${esc(a.protocol)}</td>
        <td>${esc(transportLabel)}</td>
        <td>${esc(implementationLabel(a.implementation))}</td>
        <td class="mono">${esc(a.connectionSummary || "")}</td>
        <td>${statusBadge(a.connectionStateDisplay || a.connectionState)}</td>
        <td>${a.runtimePresent ? "yes" : "no"}</td>
        <td>${esc(a.equipmentCount || 0)}</td>
        <td class="mono">${esc(a.lastError || "")}</td>
      </tr>`;
      })
      .join("")}</tbody></table>`;
  }

  function diagnosticsDetailPanel(selectedAdapter, selectedEquipment, d) {
    const protocolWording =
      "The adapter provides the common diagnostics shown above; no additional protocol-specific metrics are exposed by the runtime by this adapter.";
    if (selectedEquipment) {
      const eq = (d.equipment || []).find((e) => e.equipmentId === selectedEquipment);
      if (!eq) {
        return `<div class="panel diag-details-panel" id="diag-details">
          <h2>Selected Equipment Details</h2>
          <p class="muted">Equipment not found in live cache.</p>
        </div>`;
      }
      const telRows = (eq.telemetry || [])
        .map(
          (t) =>
            `<tr><td>${esc(t.name)}</td><td class="mono">${esc(t.value)}</td><td>${esc(
              t.unit || ""
            )}</td></tr>`
        )
        .join("");
      const cmds = eq.configuredCommands || [];
      const opState =
        eq.operationalStateDisplay || eq.operationalState || eq.machineState || "UNKNOWN";
      return `<div class="panel diag-details-panel" id="diag-details">
        <h2>Selected Equipment Details</h2>
        <dl class="kv">
          <dt>Equipment</dt><dd>${esc(eq.equipmentId)}</dd>
          <dt>Type</dt><dd>${esc(na(eq.type))}</dd>
          <dt>Adapter</dt><dd>${esc(eq.adapterId)}</dd>
          <dt>Protocol</dt><dd>${esc(na(eq.protocol))}</dd>
          <dt>Communication</dt><dd>${statusBadge(
            eq.communicationLifecycleState ||
              eq.communicationStateDisplay ||
              eq.communicationState
          )}</dd>
          <dt>Health</dt><dd>${healthBadge(eq.health)}${
            eq.healthReason
              ? ` <span class="muted diag-inline-reason">${esc(eq.healthReason)}</span>`
              : ""
          }</dd>
          <dt>Operational state</dt><dd>${esc(opState)}</dd>
          <dt>Last telemetry</dt><dd class="mono">${
            eq.hasSuccessfulCommunication
              ? esc(eq.lastSuccessfulTelemetryUtc) +
                " (" +
                relativeTimeLabel(eq.lastSuccessfulTelemetryUtc) +
                ")"
              : "Not available"
          }</dd>
          <dt>Last error</dt><dd class="mono">${esc(na(eq.lastError, "—"))}</dd>
        </dl>
        <h3>Telemetry</h3>
        ${
          telRows
            ? `<table class="diag-table"><thead><tr><th>Name</th><th>Value</th><th>Unit</th></tr></thead><tbody>${telRows}</tbody></table>`
            : `<p class="muted">No telemetry points in cache.</p>`
        }
        <h3>Commands</h3>
        ${
          cmds.length
            ? `<ul class="diag-cmd-list">${cmds
                .map((c) => `<li><code>${esc(c)}</code></li>`)
                .join("")}</ul>
               <p class="muted">Command runtime state: ${esc(
                 eq.commandRuntimeState || "Not available"
               )}</p>`
            : `<p class="muted">Command information not available.</p>`
        }
        <p class="muted">${esc(protocolWording)}</p>
        <div class="row-actions"><button type="button" data-action="diag-clear-detail">Close details</button></div>
      </div>`;
    }
    if (selectedAdapter) {
      const a = (d.adapters || []).find((x) => x.adapterId === selectedAdapter);
      if (!a) {
        return `<div class="panel diag-details-panel" id="diag-details">
          <h2>Selected Adapter Details</h2>
          <p class="muted">Adapter not found.</p>
        </div>`;
      }
      const mtbf =
        a.sessionMtbfStatus === "session_only"
          ? formatDurationMs(a.sessionMtbfMs) + " (session only)"
          : "Insufficient historical data";
      const assoc = a.associatedEquipment || [];
      const eqById = {};
      (d.equipment || []).forEach((e) => {
        eqById[e.equipmentId] = e;
      });
      const equipmentBlocks = (assoc.length ? assoc : (d.equipment || []).filter(
        (e) => e.adapterId === a.adapterId
      ))
        .map((ref) => {
          const eq = eqById[ref.equipmentId] || ref;
          const tel = (eq.telemetry || [])
            .slice(0, 8)
            .map(
              (t) =>
                `<tr><td>${esc(t.name)}</td><td class="mono">${esc(t.value)}</td><td>${esc(
                  t.unit || ""
                )}</td></tr>`
            )
            .join("");
          const cmds = eq.configuredCommands || [];
          const op =
            eq.operationalStateDisplay ||
            eq.operationalState ||
            eq.machineState ||
            "UNKNOWN";
          return `<div class="diag-assoc-equipment">
            <h4>${esc(eq.equipmentId || ref.equipmentId)}</h4>
            <dl class="kv compact">
              <dt>Type</dt><dd>${esc(na(eq.type || ref.type))}</dd>
              <dt>Communication</dt><dd>${statusBadge(
                eq.communicationLifecycleState ||
                  eq.communicationStateDisplay ||
                  eq.communicationState ||
                  ref.communicationState
              )}</dd>
              <dt>Health</dt><dd>${healthBadge(eq.health || "UNKNOWN")}</dd>
              <dt>Operational state</dt><dd>${esc(op)}</dd>
              <dt>Last telemetry</dt><dd class="mono">${
                eq.hasSuccessfulCommunication
                  ? relativeTimeLabel(eq.lastSuccessfulTelemetryUtc)
                  : "Not available"
              }</dd>
              <dt>Last error</dt><dd class="mono">${esc(na(eq.lastError || ref.lastError, "—"))}</dd>
            </dl>
            ${
              tel
                ? `<table class="diag-table"><thead><tr><th>Telemetry</th><th>Value</th><th>Unit</th></tr></thead><tbody>${tel}</tbody></table>`
                : `<p class="muted">No telemetry points in cache.</p>`
            }
            ${
              cmds.length
                ? `<p class="muted">Configured commands: ${cmds
                    .map((c) => `<code>${esc(c)}</code>`)
                    .join(", ")}. Runtime command state: ${esc(
                    eq.commandRuntimeState || "Not available"
                  )}.</p>`
                : `<p class="muted">Command information not available.</p>`
            }
          </div>`;
        })
        .join("");
      const protoDetail =
        (a.protocolSpecific && a.protocolSpecific.detail) || protocolWording;
      return `<div class="panel diag-details-panel" id="diag-details">
        <h2>Selected Adapter Details</h2>
        <h3>Identity</h3>
        <dl class="kv">
          <dt>Adapter name</dt><dd>${esc(a.adapterId)}</dd>
          <dt>Protocol</dt><dd>${esc(a.protocol)}</dd>
          <dt>Implementation</dt><dd>${esc(implementationLabel(a.implementation))}</dd>
          <dt>Enabled</dt><dd>${a.enabled ? "yes" : "no"}</dd>
          <dt>Endpoint / transport</dt><dd class="mono">${esc(
            na(a.connectionSummary)
          )}</dd>
        </dl>
        <h3>Communication</h3>
        <dl class="kv">
          <dt>Communication state</dt><dd>${statusBadge(
            a.communicationLifecycleState || a.connectionStateDisplay || a.connectionState
          )}</dd>
          <dt>Health</dt><dd>${healthBadge(a.health || a.communicationHealth)}${
            a.healthReason
              ? ` <span class="muted diag-inline-reason">${esc(a.healthReason)}</span>`
              : ""
          }</dd>
          <dt>State duration</dt><dd>${formatDurationMs(a.currentStateDurationMs)}</dd>
          <dt>Connected-for duration</dt><dd>${formatDurationMs(a.uptimeMs)}</dd>
          <dt>Disconnected-for duration</dt><dd>${formatDurationMs(a.downtimeMs)}</dd>
          <dt>Session connected total</dt><dd>${formatDurationMs(
            a.cumulativeConnectedMs
          )}</dd>
          <dt>Session disconnected total</dt><dd>${formatDurationMs(
            a.cumulativeDisconnectedMs
          )}</dd>
          <dt>Connection attempts</dt><dd>${esc(a.connectionAttempts || 0)}</dd>
          <dt>Successful connections</dt><dd>${esc(a.successfulConnections || 0)}</dd>
          <dt>Failed connections</dt><dd>${esc(a.failedConnections || 0)}</dd>
          <dt>Reconnect count</dt><dd>${esc(a.reconnectCount || 0)}</dd>
          <dt>Communication failure count</dt><dd>${esc(
            a.communicationFailureCount || 0
          )}</dd>
          <dt>Last successful communication</dt><dd class="mono">${
            a.lastSuccessfulCommunicationUtc
              ? esc(a.lastSuccessfulCommunicationUtc) +
                " (" +
                relativeTimeLabel(a.lastSuccessfulCommunicationUtc) +
                ")"
              : "Not available"
          }</dd>
          <dt>Last error</dt><dd class="mono">${esc(na(a.lastError, "—"))}</dd>
        </dl>
        <h3>Reliability</h3>
        <dl class="kv">
          <dt>Session faults</dt><dd>${esc(a.faultCount || 0)}</dd>
          <dt>Session warnings</dt><dd>${esc(a.warningCount || 0)}</dd>
          <dt>Current early warning</dt><dd>${esc(a.earlyWarning || "None")}</dd>
          <dt>Session MTBF</dt><dd>${esc(mtbf)}</dd>
        </dl>
        <h3>Equipment</h3>
        ${
          equipmentBlocks ||
          `<p class="muted">No equipment associated with this adapter in the live cache.</p>`
        }
        <p class="muted">${esc(protoDetail)}</p>
        <div class="row-actions"><button type="button" data-action="diag-clear-detail">Close details</button></div>
      </div>`;
    }
    return "";
  }

  function diagnosticsControlsBusy() {
    if (state.route !== "diagnostics") return false;
    const ae = document.activeElement;
    if (!ae) return false;
    if (ae.id === "diag-severity-filter" || ae.id === "diag-adapter-filter") return true;
    if (ae.tagName === "SELECT" && ae.closest("#content")) return true;
    return false;
  }

  async function renderDiagnostics() {
    const res = await IcpApi.diagnostics();
    if (!res.ok) {
      return `<div class="empty"><strong>Diagnostics unavailable</strong></div>`;
    }
    const d = res.data;
    const rt = d.runtime || {};
    const sys = d.system || {};
    const icp = d.icp || {};
    const adapters = d.adapters || [];
    const equipment = d.equipment || [];
    const activeAlarms = d.activeAlarms || [];
    const recentEvents = d.recentEvents || d.recentErrors || [];
    const impl = d.implementations || {};
    const selectedAdapter = state._diagAdapterId || null;
    const selectedEquipment = state._diagEquipmentId || null;

    const severityFilter = state._diagSeverityFilter || "all";
    const adapterFilter = state._diagAdapterFilter || "all";
    const filteredEvents = recentEvents.filter((e) => {
      const sev = String(e.severity || e.level || "").toUpperCase();
      if (severityFilter !== "all") {
        const want = severityFilter.toUpperCase();
        if (want === "WARNING" && !(sev === "WARNING" || sev === "WARN")) return false;
        if (want !== "WARNING" && sev !== want) return false;
      }
      if (adapterFilter !== "all" && (e.adapterId || "") !== adapterFilter) return false;
      return true;
    });

    const adapterFilterOpts =
      `<option value="all">All adapters</option>` +
      adapters
        .map(
          (a) =>
            `<option value="${esc(a.adapterId)}" ${
              adapterFilter === a.adapterId ? "selected" : ""
            }>${esc(a.adapterId)}</option>`
        )
        .join("");

    const healthyN = sys.healthyAdapters || 0;
    const degradedN = sys.degradedAdapters || 0;
    const faultedHealthN =
      sys.faultedHealthAdapters != null ? sys.faultedHealthAdapters : sys.failedAdapters || 0;
    const unknownHealthN = sys.unknownHealthAdapters || 0;

    let successConn = 0;
    let failedConn = 0;
    let reconnects = 0;
    let commFails = 0;
    adapters.forEach((a) => {
      successConn += Number(a.successfulConnections) || 0;
      failedConn += Number(a.failedConnections) || 0;
      reconnects += Number(a.reconnectCount) || 0;
      commFails += Number(a.communicationFailureCount) || 0;
    });

    const attentionNeeded =
      activeAlarms.length > 0 ||
      faultedHealthN > 0 ||
      degradedN > 0 ||
      (sys.faultedAdapters || 0) > 0 ||
      (icp.overallHealth && icp.overallHealth !== "HEALTHY");

    let html = `
      <div class="panel diag-header-panel">
        <div class="diag-summary-head">
          <h2>Diagnostics</h2>
          <span class="muted mono">${esc(d.generatedAtUtc || "")}</span>
        </div>
        <p class="diag-attention ${attentionNeeded ? "needs-attention" : "all-clear"}">
          ${
            attentionNeeded
              ? "Operator attention may be required — review ICP health, active alarms, and adapter status below."
              : "No immediate operator attention indicated."
          }
        </p>
      </div>

      <div class="panel diag-icp-panel">
        <div class="diag-summary-head">
          <h2>ICP System Health</h2>
          ${healthBadge(icp.overallHealth || "UNKNOWN")}
        </div>
        <p class="muted diag-note">Software self-diagnostics for the ICP runtime — independent of whether industrial adapters are connected.</p>
        <div class="grid stats diag-summary-stats">
          <div class="stat"><div class="label">Service</div><div class="value">${esc(
            icp.serviceStatus || "UNKNOWN"
          )}</div></div>
          <div class="stat"><div class="label">HTTP / API</div><div class="value">${esc(
            icp.httpApiStatus || "UNKNOWN"
          )}</div></div>
          <div class="stat"><div class="label">Scheduler</div><div class="value">${esc(
            icp.schedulerStatus || "UNKNOWN"
          )}</div></div>
          <div class="stat"><div class="label">Adapter manager</div><div class="value">${esc(
            icp.adapterManagerStatus || "UNKNOWN"
          )}</div></div>
          <div class="stat"><div class="label">Live-state cache</div><div class="value">${esc(
            icp.liveStateCacheStatus || "UNKNOWN"
          )}</div></div>
          <div class="stat"><div class="label">Configuration</div><div class="value">${esc(
            icp.configurationStatus || "UNKNOWN"
          )}</div></div>
          <div class="stat"><div class="label">Event system</div><div class="value">${esc(
            icp.eventSystemStatus || "UNKNOWN"
          )}</div></div>
          <div class="stat"><div class="label">Self-test</div><div class="value">${esc(
            icp.selfTestResult || "UNKNOWN"
          )}</div></div>
          <div class="stat"><div class="label">Uptime</div><div class="value">${esc(
            formatDurationMs(icp.applicationUptimeMs)
          )}</div></div>
        </div>
        ${
          Array.isArray(icp.checks) && icp.checks.length
            ? `<ul class="diag-check-list">${icp.checks
                .map((c) => `<li>${esc(c)}</li>`)
                .join("")}</ul>`
            : ""
        }
        <p class="muted">${esc(
          icp.selfTestDetail ||
            icp.notes ||
            "ICP System Health reflects ICP software subsystems only."
        )}</p>
      </div>

      <div class="panel diag-summary">
        <div class="diag-summary-head">
          <h2>Industrial communication summary</h2>
          ${healthBadge(sys.overallHealth || "UNKNOWN")}
        </div>
        <div class="grid stats diag-summary-stats">
          <div class="stat"><div class="label">Adapters</div><div class="value">${esc(
            sys.configuredAdapters != null ? sys.configuredAdapters : rt.configuredAdapterCount || 0
          )}</div></div>
          <div class="stat"><div class="label">Connected</div><div class="value">${esc(
            sys.connectedAdapters != null ? sys.connectedAdapters : rt.connectedAdapters || 0
          )}</div></div>
          <div class="stat"><div class="label">Disconnected</div><div class="value">${esc(
            sys.disconnectedAdapters != null
              ? sys.disconnectedAdapters
              : rt.disconnectedAdapters || 0
          )}</div></div>
          <div class="stat"><div class="label">Connection faulted</div><div class="value">${esc(
            sys.faultedAdapters != null ? sys.faultedAdapters : rt.faultedAdapters || 0
          )}</div></div>
          <div class="stat"><div class="label">Active alarms</div><div class="value">${esc(
            sys.activeAlarmCount != null ? sys.activeAlarmCount : activeAlarms.length
          )}</div></div>
        </div>
        <div class="diag-summary-grid">
          <div>
            <h3>Adapter health</h3>
            <dl class="kv">
              <dt>Healthy</dt><dd>${esc(healthyN)}</dd>
              <dt>Degraded</dt><dd>${esc(degradedN)}</dd>
              <dt>Faulted</dt><dd>${esc(faultedHealthN)}</dd>
              <dt>Unknown</dt><dd>${esc(unknownHealthN)}</dd>
            </dl>
            ${diagBarChart([
              { label: "Healthy", value: healthyN, tone: "healthy" },
              { label: "Degraded", value: degradedN, tone: "degraded" },
              { label: "Faulted", value: faultedHealthN, tone: "faulted" },
              { label: "Unknown", value: unknownHealthN, tone: "unknown" },
            ])}
          </div>
          <div>
            <h3>Equipment</h3>
            <dl class="kv">
              <dt>Healthy</dt><dd>${esc(sys.healthyEquipment || 0)}</dd>
              <dt>Degraded</dt><dd>${esc(sys.degradedEquipment || 0)}</dd>
              <dt>Faulted</dt><dd>${esc(sys.faultedEquipment || 0)}</dd>
            </dl>
          </div>
          <div>
            <h3>Communication reliability</h3>
            ${diagMetricBars([
              { label: "Successful connections", value: successConn, tone: "healthy" },
              { label: "Failed connections", value: failedConn, tone: "faulted" },
              { label: "Reconnects", value: reconnects, tone: "degraded" },
              { label: "Comm. failures", value: commFails, tone: "faulted" },
            ])}
            <h3 class="diag-subhead">Session uptime vs downtime</h3>
            ${diagUptimeBars(adapters)}
            <p class="muted diag-note">${esc(
              sys.mtbfNote ||
                "Long-term MTBF requires persistent history across sessions."
            )}</p>
          </div>
        </div>
      </div>

      <div class="panel">
        <h2>Active alarms</h2>
        <p class="muted diag-note">Current conditions only. Historical faults remain in Recent events.</p>
        ${
          !activeAlarms.length
            ? `<p class="diag-ok-banner">No active alarms</p>`
            : `<table class="diag-table"><thead><tr><th>Severity</th><th>Source</th><th>Protocol</th><th>Problem</th><th>Since</th></tr></thead><tbody>${activeAlarms
                .map(
                  (a) => `<tr>
              <td>${severityBadge(a.severity)}</td>
              <td>${esc(a.sourceType || "")}: <strong>${esc(a.sourceId || "")}</strong></td>
              <td>${esc(a.protocol || "—")}</td>
              <td>${esc(a.message || "")}</td>
              <td class="mono">${esc(a.sinceUtc || "")}</td>
            </tr>`
                )
                .join("")}</tbody></table>`
        }
      </div>

      <div class="panel">
        <h2>Adapter Health</h2>
        ${diagSelectHint("adapter")}
        ${
          !adapters.length
            ? `<p class="muted">No adapters configured.</p>`
            : `<table class="diag-table diag-adapters diag-selectable"><thead><tr>
                <th>Adapter</th><th>Protocol</th><th>Communication</th><th>Health</th><th>Connected for</th><th>Reconnects</th><th>Faults</th><th>Last communication</th><th>Last error</th>
              </tr></thead><tbody>${adapters
                .map((a) => {
                  const selected =
                    selectedAdapter === a.adapterId ? " diag-row-selected" : "";
                  return `<tr class="diag-row-clickable${selected}" data-action="diag-select-adapter" data-id="${esc(
                    a.adapterId
                  )}" title="View detailed diagnostics">
                  <td><strong>${esc(a.adapterId)}</strong></td>
                  <td>${esc(a.protocol)}</td>
                  <td>${statusBadge(
                    a.communicationLifecycleState ||
                      a.connectionStateDisplay ||
                      a.connectionState
                  )}</td>
                  <td>${healthBadge(a.health || a.communicationHealth)}${
                    a.healthReason
                      ? `<div class="muted diag-cell-reason">${esc(a.healthReason)}</div>`
                      : ""
                  }</td>
                  <td>${formatDurationMs(a.uptimeMs)}</td>
                  <td>${esc(a.reconnectCount || 0)}</td>
                  <td>${esc(a.faultCount || 0)}</td>
                  <td class="mono">${
                    a.lastSuccessfulCommunicationUtc
                      ? relativeTimeLabel(a.lastSuccessfulCommunicationUtc)
                      : "Not available"
                  }</td>
                  <td class="mono">${esc(na(a.lastError, "—"))}</td>
                </tr>`;
                })
                .join("")}</tbody></table>`
        }
      </div>

      ${
        selectedAdapter
          ? diagnosticsDetailPanel(selectedAdapter, null, d)
          : ""
      }

      <div class="panel">
        <h2>Equipment Health</h2>
        ${diagSelectHint("equipment")}
        ${
          !equipment.length
            ? `<p class="muted">No equipment in live cache. Connect an adapter to populate communication health.</p>`
            : `<table class="diag-table diag-equipment diag-selectable"><thead><tr>
                <th>Equipment</th><th>Adapter</th><th>Communication</th><th>Health</th><th>Operational</th><th>Last telemetry</th><th>Error</th>
              </tr></thead><tbody>${equipment
                .map((e) => {
                  const selected =
                    selectedEquipment === e.equipmentId ? " diag-row-selected" : "";
                  const op =
                    e.operationalStateDisplay ||
                    e.operationalState ||
                    e.machineState ||
                    "UNKNOWN";
                  return `<tr class="diag-row-clickable${selected}" data-action="diag-select-equipment" data-id="${esc(
                    e.equipmentId
                  )}" title="View detailed diagnostics">
                  <td><strong>${esc(e.equipmentId)}</strong></td>
                  <td>${esc(e.adapterId)}</td>
                  <td>${statusBadge(
                    e.communicationLifecycleState ||
                      e.communicationStateDisplay ||
                      e.communicationState
                  )}</td>
                  <td>${healthBadge(e.health)}${
                    e.healthReason
                      ? `<div class="muted diag-cell-reason">${esc(e.healthReason)}</div>`
                      : ""
                  }</td>
                  <td>${esc(op)}</td>
                  <td class="mono">${
                    e.hasSuccessfulCommunication
                      ? relativeTimeLabel(e.lastSuccessfulTelemetryUtc)
                      : "Not available"
                  }</td>
                  <td class="mono">${esc(na(e.lastError, "—"))}</td>
                </tr>`;
                })
                .join("")}</tbody></table>`
        }
      </div>

      ${
        selectedEquipment
          ? diagnosticsDetailPanel(null, selectedEquipment, d)
          : ""
      }

      <div class="panel">
        <h2>Recent events</h2>
        <p class="muted diag-note">Historical faults and events (bounded). Not the same as active alarms.</p>
        <div class="toolbar diag-filters">
          <label class="field">
            <span class="field-label">Severity</span>
            <select id="diag-severity-filter">
              <option value="all" ${severityFilter === "all" ? "selected" : ""}>All</option>
              <option value="INFO" ${severityFilter === "INFO" ? "selected" : ""}>INFO</option>
              <option value="WARNING" ${
                severityFilter === "WARNING" ? "selected" : ""
              }>WARNING</option>
              <option value="ERROR" ${severityFilter === "ERROR" ? "selected" : ""}>ERROR</option>
              <option value="CRITICAL" ${
                severityFilter === "CRITICAL" ? "selected" : ""
              }>CRITICAL</option>
            </select>
          </label>
          <label class="field">
            <span class="field-label">Adapter</span>
            <select id="diag-adapter-filter">${adapterFilterOpts}</select>
          </label>
        </div>
        ${
          !filteredEvents.length
            ? `<p class="muted">No events match the current filters.</p>`
            : `<table class="diag-table"><thead><tr><th>Time</th><th>Severity</th><th>Category</th><th>Source</th><th>Message</th></tr></thead><tbody>${filteredEvents
                .slice(0, 50)
                .map((e) => {
                  const source = e.equipmentId
                    ? "equipment:" + e.equipmentId
                    : e.adapterId
                      ? "adapter:" + e.adapterId
                      : "system";
                  return `<tr>
                  <td class="mono">${esc(e.atUtc || "")}</td>
                  <td>${severityBadge(e.severity || e.level)}</td>
                  <td>${esc(e.category || "")}</td>
                  <td>${esc(source)}</td>
                  <td>${esc(e.message || "")}</td>
                </tr>`;
                })
                .join("")}</tbody></table>`
        }
      </div>

      <div class="panel">
        <h2>Configuration / self-test</h2>
        ${
          d.configurationValidation && d.configurationValidation.ok
            ? statusBadge("ok") + " Configuration validates."
            : formatIssues(d.configurationValidation)
        }
        <dl class="kv" style="margin-top:0.75rem">
          <dt>ICP self-test</dt><dd>${healthBadge(icp.selfTestResult || "UNKNOWN")}</dd>
          <dt>Event buffer</dt><dd>${esc(icp.eventBufferSize || 0)} / ${esc(
      icp.eventBufferCapacity || 0
    )}</dd>
          <dt>Runtime adapters</dt><dd>${esc(icp.runtimeAdapterCount || 0)} / ${esc(
      icp.configuredAdapterCount || 0
    )} configured</dd>
        </dl>
      </div>`;

    if (impl.gateway && impl.gateway.active) {
      html += `<div class="panel">
        <h2>Gateway implementation</h2>
        <p class="muted">${esc(impl.gateway.label || "Industrial gateway protocols")}</p>
        ${renderImplementationAdapterTable(impl.gateway.adapters)}
      </div>`;
    }
    if (impl.hilscher_native && impl.hilscher_native.active) {
      const hw = impl.hilscher_native.hardware || {};
      html += `<div class="panel">
        <h2>Hilscher native fieldbus</h2>
        ${renderImplementationAdapterTable(impl.hilscher_native.adapters)}
        <dl class="kv">
          <dt>Hardware</dt><dd>${esc(hw.hardware || hw.readinessState || "—")}</dd>
          <dt>Summary</dt><dd>${esc(hw.summary || "")}</dd>
        </dl>
      </div>`;
    }

    return html;
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
    const appearance = loadAppearance();
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
    </div>
    ${appearanceFormHtml(appearance)}`;
  }

  async function render(options) {
    options = options || {};
    const run = async () => {
      const generation = ++state._renderGeneration;
      if (!options.skipDraftCapture) {
        captureAdapterFormDraft();
        captureConfigurationDraft();
      }

      const route = state.route;
      if (generation !== state._renderGeneration) {
        return;
      }
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
      if (generation !== state._renderGeneration) {
        return;
      }
      $("#content").innerHTML = html;
      bindPostRenderHandlers();
      state._lastRefreshAt = new Date().toISOString();
      const st = await IcpApi.status();
      if (generation !== state._renderGeneration) {
        return;
      }
      if (st.ok) {
        $("#runtime-pill").textContent = st.data.schedulerRunning
          ? "ICP RUNNING"
          : "ICP STOPPED";
      }
      const pollEl = $("#poll-indicator");
      if (pollEl && options.manualRefresh) {
        pollEl.textContent = "Live";
        pollEl.classList.remove("paused");
      }
    };
    renderChain = renderChain.then(run, run);
    return renderChain;
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
      if (action === "appearance-apply") {
        const next = saveAppearance(readAppearanceForm());
        applyAppearance(next);
        flash("Appearance applied (saved in this browser only)", "ok");
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "appearance-reset") {
        localStorage.removeItem(APPEARANCE_STORAGE_KEY);
        applyAppearance(DEFAULT_APPEARANCE);
        flash("Appearance reset to default", "ok");
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "diag-select-adapter") {
        state._diagAdapterId = id || null;
        state._diagEquipmentId = null;
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "diag-select-equipment") {
        state._diagEquipmentId = id || null;
        state._diagAdapterId = null;
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "diag-clear-detail") {
        state._diagAdapterId = null;
        state._diagEquipmentId = null;
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "stop-modal") {
        return;
      }
      if (action === "goto-adapters") {
        location.hash = "#/adapters";
        return;
      }
      if (action === "open-add-chooser") {
        state._chooser = { step: "protocol" };
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "close-chooser") {
        state._chooser = null;
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "back-chooser") {
        state._chooser = { step: "protocol" };
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "choose-protocol") {
        const protocol = el && el.dataset.protocol;
        if (!protocol) return;
        const info = Form.PROTOCOL_INFO[protocol] || {};
        if (info.next === "implementation") {
          state._chooser = { step: "implementation", protocol };
          await render({ skipDraftCapture: true });
          return;
        }
        if (info.next === "transport") {
          state._chooser = { step: "transport", protocol };
          await render({ skipDraftCapture: true });
          return;
        }
        openAddAdapterEditor(protocol);
        await render({ skipDraftCapture: true });
        flash("Adapter editor opened — configure and click Validate & Save adapter", "ok");
        return;
      }
      if (action === "choose-modbus-transport") {
        const transport = el && el.dataset.transport;
        const info =
          (Form.MODBUS_TRANSPORT_INFO && Form.MODBUS_TRANSPORT_INFO[transport]) || null;
        if (!info) return;
        if (!info.available) {
          flash("Selected Modbus transport is not available in this release.", "ok");
          state._chooser = null;
          await render({ skipDraftCapture: true });
          return;
        }
        openAddAdapterEditor("modbus", undefined, transport);
        await render({ skipDraftCapture: true });
        flash(
          (transport === "rtu" ? "Modbus RTU / RS-485" : "Modbus TCP") +
            " adapter editor opened — configure and click Validate & Save adapter",
          "ok"
        );
        return;
      }
      if (action === "choose-implementation") {
        const impl = el && el.dataset.implementation;
        const protocol = state._chooser && state._chooser.protocol;
        if (!protocol || !impl) return;
        if (impl === "hilscher_native" || impl === "softing_native") {
          flash(
            (impl === "hilscher_native" ? "Hilscher" : "Softing") +
              " native implementation is coming soon.",
            "ok"
          );
          state._chooser = null;
          await render({ skipDraftCapture: true });
          return;
        }
        if (impl === "gateway") {
          openAddAdapterEditor(protocol, "gateway");
          await render({ skipDraftCapture: true });
          flash("Gateway adapter editor opened — configure the gateway northbound endpoint", "ok");
          return;
        }
        return;
      }
      if (action === "add-adapter") {
        state._chooser = { step: "protocol" };
        await render({ skipDraftCapture: true });
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
          implementation: detail.data.implementation || "",
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
        state._renderGeneration++;
        let adapter;
        try {
          adapter = readAdapterForm();
        } catch (e) {
          flash(e.message || String(e), "error");
          return;
        }
        const res = await IcpApi.upsertAdapter(adapter);
        const box = $("#editor-result");
        if (!res.ok || (res.data && res.data.ok === false)) {
          const issues = (res.data && res.data.issues) || [];
          adapter._edit = state._editingAdapter && state._editingAdapter._edit;
          adapter._advancedOpen =
            state._editingAdapter && state._editingAdapter._advancedOpen;
          adapter._validationIssues = issues;
          state._editingAdapter = adapter;
          await render({ skipDraftCapture: true });
          flash((res.data && res.data.message) || "Validation failed — fix highlighted fields", "error");
          return;
        }
        // Persist immediately so restart survives.
        const save = await IcpApi.saveConfiguration();
        state._renderGeneration++;
        state._editingAdapter = null;
        flash(
          save.ok ? "Adapter saved and configuration persisted" : "Adapter saved in memory; persist failed",
          save.ok ? "ok" : "error"
        );
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "add-equipment") {
        captureAdapterFormDraft();
        const a = state._editingAdapter;
        if (!a) return;
        a.equipment = a.equipment || [];
        const proto = a.protocol || "mock";
        const impl = Form.resolveImplementation(a);
        const sample =
          Form.defaultAdapter(proto, a.adapterId, {
            implementation: impl === "gateway" ? "gateway" : "",
          }).equipment[0] || {
          equipmentId: a.adapterId + "-EQ-" + (a.equipment.length + 1),
          type: "device",
          capabilities: [],
          telemetry: [],
          commands: [],
          state: { mapped: false },
          fault: { mapped: false },
        };
        sample.equipmentId = a.adapterId + "-EQ-" + (a.equipment.length + 1);
        a.equipment.push(sample);
        a._validationIssues = undefined;
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "remove-equipment") {
        captureAdapterFormDraft();
        const eqIdx = parseInt(el && el.dataset.eq, 10);
        const a = state._editingAdapter;
        if (!a || Number.isNaN(eqIdx)) return;
        a.equipment.splice(eqIdx, 1);
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "add-telemetry") {
        captureAdapterFormDraft();
        const eqIdx = parseInt(el && el.dataset.eq, 10);
        const a = state._editingAdapter;
        if (!a || !a.equipment[eqIdx]) return;
        const fields = Form.telemetryFieldsFor(a);
        const tel = {};
        fields.forEach((f) => {
          if (f.key === "name") tel.name = "point" + ((a.equipment[eqIdx].telemetry || []).length + 1);
        });
        a.equipment[eqIdx].telemetry = a.equipment[eqIdx].telemetry || [];
        a.equipment[eqIdx].telemetry.push(tel);
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "remove-telemetry") {
        captureAdapterFormDraft();
        const eqIdx = parseInt(el && el.dataset.eq, 10);
        const telIdx = parseInt(el && el.dataset.tel, 10);
        const a = state._editingAdapter;
        if (!a || !a.equipment[eqIdx]) return;
        a.equipment[eqIdx].telemetry.splice(telIdx, 1);
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "add-command") {
        captureAdapterFormDraft();
        const eqIdx = parseInt(el && el.dataset.eq, 10);
        const a = state._editingAdapter;
        if (!a || !a.equipment[eqIdx]) return;
        a.equipment[eqIdx].commands = a.equipment[eqIdx].commands || [];
        a.equipment[eqIdx].commands.push({ command: "cmd" + (a.equipment[eqIdx].commands.length + 1) });
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "remove-command") {
        captureAdapterFormDraft();
        const eqIdx = parseInt(el && el.dataset.eq, 10);
        const cmdIdx = parseInt(el && el.dataset.cmd, 10);
        const a = state._editingAdapter;
        if (!a || !a.equipment[eqIdx]) return;
        a.equipment[eqIdx].commands.splice(cmdIdx, 1);
        await render({ skipDraftCapture: true });
        return;
      }
      if (action === "toggle-advanced") {
        if (!state._editingAdapter) return;
        state._editingAdapter._advancedOpen = !state._editingAdapter._advancedOpen;
        const body = document.getElementById("advanced-json-body");
        const btn = el;
        if (body) {
          body.classList.toggle("hidden", !state._editingAdapter._advancedOpen);
        }
        if (btn) {
          btn.textContent = state._editingAdapter._advancedOpen ? "Hide JSON" : "Show JSON";
        }
        const ta = document.getElementById("f-advanced-json");
        if (ta && state._editingAdapter._advancedOpen) {
          ta.value = JSON.stringify(
            Form.adapterToConfigJson(state._editingAdapter),
            null,
            2
          );
          validateAdvancedJsonText(ta.value);
          bindAdvancedJsonEditor();
        }
        return;
      }
      if (action === "gui-to-json") {
        captureAdapterFormDraft();
        const ta = document.getElementById("f-advanced-json");
        if (ta && state._editingAdapter) {
          ta.value = JSON.stringify(
            Form.adapterToConfigJson(state._editingAdapter),
            null,
            2
          );
          validateAdvancedJsonText(ta.value);
          flash("Generated JSON from form fields", "ok");
        }
        return;
      }
      if (action === "json-to-gui") {
        const ta = document.getElementById("f-advanced-json");
        if (!ta) return;
        const checked = validateAdvancedJsonText(ta.value);
        if (!checked.ok) {
          flash("Could not load form from JSON: " + checked.error, "error");
          return;
        }
        try {
          const parsed = Form.parseAdapterJson(ta.value);
          parsed._edit = state._editingAdapter && state._editingAdapter._edit;
          parsed._advancedOpen = true;
          parsed._validationIssues = undefined;
          state._editingAdapter = parsed;
          await render({ skipDraftCapture: true });
          flash("Loaded form fields from JSON", "ok");
        } catch (e) {
          setAdvancedJsonStatus(false, "✕ Invalid JSON: " + e.message);
          flash("Could not load form from JSON: " + e.message, "error");
        }
        return;
      }
      if (action === "eq-cmd") {
        const cmd = el.dataset.cmd;
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
        state._cfgEditorDirty = false;
        state._cfgDraft = null;
        flash(res.data.message || (res.ok ? "Loaded" : "Load failed"), res.ok ? "ok" : "error");
        await render();
        return;
      }
      if (action === "cfg-apply") {
        const text = $("#cfg-editor").value;
        const doc = JSON.parse(text);
        const res = await IcpApi.putConfiguration(doc);
        state._cfgEditorDirty = false;
        state._cfgDraft = null;
        flash(res.data.message || (res.ok ? "Applied" : "Apply failed"), res.ok ? "ok" : "error");
        if (!res.ok) $("#cfg-validation").innerHTML = formatIssues(res.data);
        else await render();
        return;
      }
      if (action === "cfg-import") {
        const text = $("#cfg-import").value;
        const res = await IcpApi.importConfiguration(text);
        state._cfgEditorDirty = false;
        state._cfgDraft = null;
        flash(res.data.message || (res.ok ? "Imported" : "Import failed"), res.ok ? "ok" : "error");
        await render();
        return;
      }
      if (action === "cfg-export") {
        const res = await IcpApi.exportConfiguration();
        const jsonText = JSON.stringify(res.data, null, 2);
        const editor = $("#cfg-editor");
        if (editor) editor.value = jsonText;
        const blob = new Blob([jsonText], { type: "application/json" });
        const url = URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = url;
        a.download = "icp-config.json";
        a.click();
        URL.revokeObjectURL(url);
        flash("Configuration exported", "ok");
        return;
      }
    } catch (e) {
      flash(e.message || String(e), "error");
    }
  }

  document.addEventListener("click", (ev) => {
    const btn = ev.target.closest("[data-action]");
    if (!btn) return;
    // Native form controls must not go through action dispatch (breaks selects).
    if (btn.tagName === "SELECT" || btn.tagName === "OPTION" || btn.tagName === "INPUT") {
      return;
    }
    void onAction(btn.dataset.action, btn.dataset.id, btn).catch((e) => {
      flash(e.message || String(e), "error");
    });
  });

  $("#btn-refresh").addEventListener("click", async () => {
    const pollEl = $("#poll-indicator");
    if (pollEl) {
      pollEl.textContent = "Refreshing…";
      pollEl.classList.add("paused");
    }
    await render({ manualRefresh: true, skipDraftCapture: false });
    flash("View refreshed at " + new Date().toLocaleTimeString(), "ok");
  });

  window.addEventListener("hashchange", () => {
    parseHash();
    render();
  });

  function startPolling() {
    if (state.pollTimer) clearInterval(state.pollTimer);
    state.pollTimer = setInterval(() => {
      if (document.hidden) return;
      // Do not overwrite unsaved adapter editor or configuration editor during poll.
      if (state._editingAdapter && state.route === "adapters") return;
      if (state._cfgEditorDirty && state.route === "configuration") return;
      // Do not recreate diagnostics DOM while a filter <select> is open/focused.
      if (diagnosticsControlsBusy()) return;
      if (["dashboard", "equipment", "connections", "diagnostics", "events", "adapters"].includes(
        state.route
      )) {
        render({ skipDraftCapture: false });
      }
    }, 2000);
  }

  applyAppearance(loadAppearance());
  parseHash();
  render();
  startPolling();
})();
