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
  };

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

  function statusBadge(value) {
    const v = value || "UNKNOWN";
    return `<span class="status ${esc(v)}">${esc(v.replace(/_/g, " "))}</span>`;
  }

  function adapterConnectionBadge(adapter) {
    const display =
      adapter.connectionStateDisplay || adapter.connectionState || "UNKNOWN";
    return statusBadge(display);
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
          <p class="muted">Select the communication method.</p>
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

  function openAddAdapterEditor(protocol, implementation) {
    state._chooser = null;
    const opts =
      implementation && (protocol === "profinet" || protocol === "profibus")
        ? { implementation }
        : {};
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

  function telemetryRowHtml(adapter, tel, eqIdx, telIdx, errorMap) {
    const fields = Form.telemetryFieldsFor(editorContext(adapter));
    const protocol = editorContext(adapter).protocol || "mock";
    const cells = fields
      .map((f) => {
        const id = `f-eq-${eqIdx}-tel-${telIdx}-${f.key}`;
        const req = f.required ? ' <span class="req">*</span>' : "";
        const help = f.help ? `<span class="field-help">${esc(f.help)}</span>` : "";
        const control = mappingFieldControl(
          f,
          id,
          tel[f.key],
          `data-tel-field="${esc(f.key)}"`
        );
        return `<label class="${fieldErrorClass(id, errorMap)}">${esc(f.label)}${req}${control}${help}${fieldErrorMsg(
          id,
          errorMap
        )}</label>`;
      })
      .join("");
    const addressingNote =
      protocol === "modbus"
        ? `<p class="field-help full">Addresses are 0-based protocol addresses. Vendor manuals may show 40001-style numbers; configure the zero-based wire address here.</p>`
        : "";
    return `<div class="tel-row" data-eq="${eqIdx}" data-tel="${telIdx}">${addressingNote}${cells}
      <div class="row-actions"><button type="button" data-action="remove-telemetry" data-eq="${eqIdx}" data-tel="${telIdx}">Remove telemetry</button></div>
    </div>`;
  }

  function commandRowHtml(adapter, cmd, eqIdx, cmdIdx, errorMap) {
    const fields = Form.commandFieldsFor(editorContext(adapter));
    const cells = fields
      .map((f) => {
        const id = `f-eq-${eqIdx}-cmd-${cmdIdx}-${f.key}`;
        const req = f.required ? ' <span class="req">*</span>' : "";
        const help = f.help ? `<span class="field-help">${esc(f.help)}</span>` : "";
        const control = mappingFieldControl(
          f,
          id,
          cmd[f.key],
          `data-cmd-field="${esc(f.key)}"`
        );
        return `<label class="${fieldErrorClass(id, errorMap)}">${esc(f.label)}${req}${control}${help}${fieldErrorMsg(
          id,
          errorMap
        )}</label>`;
      })
      .join("");
    return `<div class="cmd-row" data-eq="${eqIdx}" data-cmd="${cmdIdx}">${cells}
      <div class="row-actions"><button type="button" data-action="remove-command" data-eq="${eqIdx}" data-cmd="${cmdIdx}">Remove command</button></div>
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
      <h4>Telemetry</h4>
      ${tel || '<p class="muted">No telemetry mappings. Add points that this equipment should publish.</p>'}
      <div class="row-actions"><button type="button" data-action="add-telemetry" data-eq="${eqIdx}">Add telemetry</button></div>
      <h4>Commands</h4>
      ${cmds || '<p class="muted">No commands. Add commands this equipment can execute.</p>'}
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
      ? `<label>Implementation<input id="f-implementation" readonly value="${esc(
          implementationLabel(impl)
        )}"/></label>
         <input type="hidden" id="f-implementation-value" value="${esc(impl)}"/>`
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
            <label class="${fieldErrorClass("f-id", errorMap)}">Adapter name <span class="req">*</span>
              <input id="f-id" value="${esc(adapter.adapterId)}" ${
                adapter._edit ? "readonly" : ""
              }/>${fieldErrorMsg("f-id", errorMap)}</label>
            <label>Enabled<select id="f-enabled"><option value="true" ${
              adapter.enabled !== false ? "selected" : ""
            }>true</option><option value="false" ${
              adapter.enabled === false ? "selected" : ""
            }>false</option></select></label>
            <label class="${fieldErrorClass("f-protocol", errorMap)}">Protocol <span class="req">*</span>
              <select id="f-protocol">${options}</select>${fieldErrorMsg("f-protocol", errorMap)}</label>
            ${implHtml}
            <label class="full">Description<input id="f-desc" value="${esc(
              adapter.description || ""
            )}"/></label>
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
            <p class="muted">Expert view of the same adapter record the backend stores. Use GUI fields for normal setup.</p>
            <textarea id="f-advanced-json" class="mono" rows="14">${esc(
              JSON.stringify(Form.adapterToConfigJson(adapter), null, 2)
            )}</textarea>
            <div class="toolbar">
              <button type="button" data-action="gui-to-json">GUI → JSON</button>
              <button type="button" data-action="json-to-gui">JSON → GUI</button>
            </div>
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
        <th>Adapter</th><th>Protocol</th><th>Implementation</th><th>State</th><th>Enabled</th><th>Equipment</th><th>Error</th><th>Actions</th>
      </tr></thead><tbody>`;
      html += adapters
        .map(
          (a) => `<tr>
          <td><a href="#/adapters/${esc(a.adapterId)}">${esc(a.adapterId)}</a></td>
          <td>${esc(a.protocol)}</td>
          <td>${esc(implementationLabel(a.implementation))}</td>
          <td>${adapterConnectionBadge(a)}</td>
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

  function renderImplementationAdapterTable(adapters, title) {
    if (!adapters || !adapters.length) {
      return `<p class="muted">No adapters in this implementation group.</p>`;
    }
    return `<table><thead><tr><th>Adapter</th><th>Protocol</th><th>Implementation</th><th>Endpoint</th><th>State</th><th>Runtime</th><th>Equipment</th><th>Last error</th></tr></thead><tbody>${adapters
      .map(
        (a) => `<tr>
        <td>${esc(a.adapterId)}</td>
        <td>${esc(a.protocol)}</td>
        <td>${esc(implementationLabel(a.implementation))}</td>
        <td class="mono">${esc(a.connectionSummary || "")}</td>
        <td>${statusBadge(a.connectionStateDisplay || a.connectionState)}</td>
        <td>${a.runtimePresent ? "yes" : "no"}</td>
        <td>${esc(a.equipmentCount || 0)}</td>
        <td class="mono">${esc(a.lastError || "")}</td>
      </tr>`
      )
      .join("")}</tbody></table>`;
  }

  async function renderDiagnostics() {
    const res = await IcpApi.diagnostics();
    if (!res.ok) {
      return `<div class="empty"><strong>Diagnostics unavailable</strong></div>`;
    }
    const d = res.data;
    const rt = d.runtime || {};
    const adapters = d.adapters || [];
    const equipment = d.equipment || [];
    const recentErrors = d.recentErrors || [];
    const dist = rt.protocolDistribution || {};
    const impl = d.implementations || {};
    const gateway = impl.gateway;
    const hilscher = impl.hilscher_native;
    const softing = impl.softing_native;

    let html = `<div class="grid stats">
        <div class="stat"><div class="label">Scheduler</div><div class="value">${
          rt.schedulerRunning ? "ON" : "OFF"
        }</div></div>
        <div class="stat"><div class="label">Connected</div><div class="value">${esc(
          rt.connectedAdapters || 0
        )}</div></div>
        <div class="stat"><div class="label">Faulted</div><div class="value">${esc(
          rt.faultedAdapters || 0
        )}</div></div>
        <div class="stat"><div class="label">Stale eq.</div><div class="value">${esc(
          rt.staleEquipment || 0
        )}</div></div>
        <div class="stat"><div class="label">Active comms</div><div class="value">${esc(
          rt.activeCommunications || 0
        )}</div></div>
      </div>
      <p class="muted">Diagnostics reflect what you configured — implementation-specific sections appear only for active adapter types.</p>
      <div class="panel">
        <h2>Runtime health</h2>
        <dl class="kv">
          <dt>Configured adapters</dt><dd>${esc(rt.configuredAdapterCount || 0)}</dd>
          <dt>Runtime adapters</dt><dd>${esc(rt.runtimeAdapterCount || 0)}</dd>
          <dt>Disconnected</dt><dd>${esc(rt.disconnectedAdapters || 0)}</dd>
          <dt>Equipment</dt><dd>${esc(rt.equipmentCount || 0)}</dd>
          <dt>Machine faults</dt><dd>${esc(rt.machineFaultEquipment || 0)}</dd>
          <dt>Config path</dt><dd class="mono">${esc(rt.configurationPath || "")}</dd>
          <dt>Config state</dt><dd>${esc(rt.configurationLoadState || "")}</dd>
        </dl>
        <h3>Protocol distribution</h3>
        ${
          Object.keys(dist).length
            ? `<table><thead><tr><th>Protocol</th><th>Count</th></tr></thead><tbody>${Object.keys(
                dist
              )
                .map((k) => `<tr><td>${esc(k)}</td><td>${esc(dist[k])}</td></tr>`)
                .join("")}</tbody></table>`
            : `<p class="muted">No adapters configured.</p>`
        }
      </div>`;

    if (gateway && gateway.active) {
      html += `<div class="panel">
        <h2>Gateway implementation</h2>
        <p class="muted">${esc(gateway.label || "Industrial gateway protocols")}</p>
        <dl class="kv">
          <dt>Adapters</dt><dd>${esc(gateway.adapterCount || 0)}</dd>
          <dt>Connected</dt><dd>${esc(gateway.connectedCount || 0)}</dd>
          <dt>Faulted</dt><dd>${esc(gateway.faultedCount || 0)}</dd>
          <dt>Protocols</dt><dd>${esc(
            (gateway.protocols || []).join(", ")
          )}</dd>
        </dl>
        ${renderImplementationAdapterTable(gateway.adapters)}
        <p class="muted">Field equipment reached via OPC UA, Modbus, MQTT, REST, or EtherNet/IP — not native PROFINET/PROFIBUS stacks.</p>
      </div>`;
    }

    if (hilscher && hilscher.active) {
      const hw = hilscher.hardware || {};
      const hwBadge =
        hw.hardware === "DETECTED"
          ? statusBadge("DETECTED")
          : statusBadge("HARDWARE_NOT_AVAILABLE");
      html += `<div class="panel">
        <h2>Hilscher native fieldbus</h2>
        <p class="muted">${esc(hilscher.label || "PROFINET / PROFIBUS via Hilscher CIFX")}</p>
        ${renderImplementationAdapterTable(hilscher.adapters)}
        <h3>Hardware readiness</h3>
        <dl class="kv">
          <dt>SDK compiled in</dt><dd>${hw.compiledIn ? "yes" : "no"}</dd>
          <dt>Readiness</dt><dd>${esc(hw.readinessState || "")}</dd>
          <dt>Hardware</dt><dd>${hwBadge}</dd>
          <dt>Driver</dt><dd class="mono">${esc(hw.driverVersion || "")}</dd>
          <dt>Boards</dt><dd>${esc(hw.boardCount || 0)}</dd>
          <dt>Selected board</dt><dd>${esc(hw.selectedBoard || "")}</dd>
          <dt>Firmware</dt><dd>${esc(hw.selectedFirmware || "")}</dd>
          <dt>Summary</dt><dd>${esc(hw.summary || "")}</dd>
        </dl>
        <p class="muted">Shown because a Hilscher native adapter is configured.</p>
      </div>`;
    }

    if (softing && softing.active) {
      html += `<div class="panel">
        <h2>Softing native fieldbus</h2>
        <p class="muted">${esc(softing.label || "Softing stack")}</p>
        ${renderImplementationAdapterTable(softing.adapters)}
      </div>`;
    }

    html += `<div class="panel">
        <h2>All configured adapters</h2>
        ${
          !adapters.length
            ? `<p class="muted">No adapters.</p>`
            : `<table><thead><tr><th>Adapter</th><th>Protocol</th><th>Implementation</th><th>Endpoint</th><th>State</th><th>Runtime</th><th>Equipment</th><th>Last error</th></tr></thead><tbody>${adapters
                .map(
                  (a) => `<tr>
              <td>${esc(a.adapterId)}</td>
              <td>${esc(a.protocol)}</td>
              <td>${esc(implementationLabel(a.implementation))}</td>
              <td class="mono">${esc(a.connectionSummary || "")}</td>
              <td>${statusBadge(
                a.connectionStateDisplay || a.connectionState
              )}</td>
              <td>${a.runtimePresent ? "yes" : "no"}</td>
              <td>${esc(a.equipmentCount || 0)}</td>
              <td class="mono">${esc(a.lastError || "")}</td>
            </tr>`
                )
                .join("")}</tbody></table>`
        }
      </div>
      <div class="panel">
        <h2>Equipment communication</h2>
        ${
          !equipment.length
            ? `<p class="muted">No equipment in live cache. Connect an adapter to receive telemetry.</p>`
            : `<table><thead><tr><th>Equipment</th><th>Adapter</th><th>Comm</th><th>Machine</th><th>Stale</th><th>Last telemetry</th><th>Error</th></tr></thead><tbody>${equipment
                .map(
                  (e) => `<tr>
              <td>${esc(e.equipmentId)}</td>
              <td>${esc(e.adapterId)}</td>
              <td>${statusBadge(
                e.communicationStateDisplay || e.communicationState
              )}</td>
              <td>${esc(e.machineState || "")}${
                    e.machineFault ? " / FAULT" : ""
                  }</td>
              <td>${e.stale ? "yes" : "no"}</td>
              <td class="mono">${esc(
                e.lastSuccessfulTelemetryUtc || e.observedAtUtc || "No telemetry received yet"
              )}</td>
              <td class="mono">${esc(e.lastError || "")}</td>
            </tr>`
                )
                .join("")}</tbody></table>`
        }
      </div>
      <div class="panel">
        <h2>Configuration errors</h2>
        ${
          d.configurationValidation && d.configurationValidation.ok
            ? statusBadge("ok") + " Configuration validates."
            : formatIssues(d.configurationValidation)
        }
      </div>
      <div class="panel">
        <h2>Recent warnings / errors</h2>
        ${
          !recentErrors.length
            ? `<p class="muted">None in recent event buffer.</p>`
            : `<table><thead><tr><th>Time</th><th>Level</th><th>Adapter</th><th>Message</th></tr></thead><tbody>${recentErrors
                .slice()
                .reverse()
                .map(
                  (e) =>
                    `<tr><td class="mono">${esc(e.atUtc)}</td><td>${esc(
                      e.level
                    )}</td><td>${esc(e.adapterId || "")}</td><td>${esc(
                      e.message
                    )}</td></tr>`
                )
                .join("")}</tbody></table>`
        }
      </div>`;
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
          flash("Modbus RTU / RS-485 is coming soon and is not available in this release.", "ok");
          state._chooser = null;
          await render({ skipDraftCapture: true });
          return;
        }
        openAddAdapterEditor("modbus");
        await render({ skipDraftCapture: true });
        flash("Modbus TCP adapter editor opened — configure and click Validate & Save adapter", "ok");
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
          flash("GUI synchronized into Advanced JSON", "ok");
        }
        return;
      }
      if (action === "json-to-gui") {
        const ta = document.getElementById("f-advanced-json");
        if (!ta) return;
        try {
          const parsed = Form.parseAdapterJson(ta.value);
          parsed._edit = state._editingAdapter && state._editingAdapter._edit;
          parsed._advancedOpen = true;
          parsed._validationIssues = undefined;
          state._editingAdapter = parsed;
          await render({ skipDraftCapture: true });
          flash("Advanced JSON applied to GUI fields", "ok");
        } catch (e) {
          flash("Invalid adapter JSON: " + e.message, "error");
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
      if (["dashboard", "equipment", "connections", "diagnostics", "events", "adapters"].includes(
        state.route
      )) {
        render({ skipDraftCapture: false });
      }
    }, 2000);
  }

  parseHash();
  render();
  startPolling();
})();
