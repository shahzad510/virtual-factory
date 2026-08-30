/* ICP Application API client (v1). No secrets; credential refs only. */
(function (global) {
  const API = "/api/v1";

  async function request(path, options = {}) {
    const opts = Object.assign(
      { headers: { Accept: "application/json" } },
      options
    );
    if (opts.body && typeof opts.body === "object" && !(opts.body instanceof FormData)) {
      opts.headers["Content-Type"] = "application/json";
      opts.body = JSON.stringify(opts.body);
    }
    const res = await fetch(API + path, opts);
    const text = await res.text();
    let data = null;
    try {
      data = text ? JSON.parse(text) : null;
    } catch (e) {
      data = { ok: false, message: text || "non-JSON response" };
    }
    return { ok: res.ok, status: res.status, data };
  }

  global.IcpApi = {
    status: () => request("/status"),
    protocols: () => request("/protocols"),
    configuration: () => request("/configuration"),
    putConfiguration: (doc) => request("/configuration", { method: "PUT", body: doc }),
    validateConfiguration: () => request("/configuration/validate", { method: "POST" }),
    saveConfiguration: () => request("/configuration/save", { method: "POST" }),
    loadConfiguration: () => request("/configuration/load", { method: "POST" }),
    importConfiguration: (text) =>
      request("/configuration/import", { method: "POST", body: text, headers: { "Content-Type": "application/json", Accept: "application/json" } }),
    exportConfiguration: () => request("/configuration/export"),
    adapters: () => request("/adapters"),
    adapter: (id) => request("/adapters/" + encodeURIComponent(id)),
    upsertAdapter: (adapter) => request("/adapters", { method: "POST", body: adapter }),
    updateAdapter: (id, adapter) =>
      request("/adapters/" + encodeURIComponent(id), { method: "PUT", body: adapter }),
    removeAdapter: (id) =>
      request("/adapters/" + encodeURIComponent(id), { method: "DELETE" }),
    connectAdapter: (id) =>
      request("/adapters/" + encodeURIComponent(id) + "/connect", { method: "POST" }),
    disconnectAdapter: (id) =>
      request("/adapters/" + encodeURIComponent(id) + "/disconnect", { method: "POST" }),
    reconnectAdapter: (id) =>
      request("/adapters/" + encodeURIComponent(id) + "/reconnect", { method: "POST" }),
    equipment: () => request("/equipment"),
    equipmentById: (id) => request("/equipment/" + encodeURIComponent(id)),
    mappings: () => request("/mappings"),
    diagnostics: () => request("/diagnostics"),
    events: (limit = 100) => request("/events?limit=" + limit),
    health: () => request("/health"),
  };
})(window);
