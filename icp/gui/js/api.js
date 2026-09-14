/* ICP Application API client (v1). No secrets; credential refs only. */
(function (global) {
  const API = "/api/v1";

  /** Hard upper bound for every GUI control-plane fetch (ms). */
  const DEFAULT_TIMEOUT_MS = 6000;

  /** Optional AbortSignal installed by the app render lifecycle. */
  let activeAbortSignal = null;

  function setActiveAbortSignal(signal) {
    activeAbortSignal = signal || null;
  }

  function clearActiveAbortSignal(signal) {
    if (!signal || activeAbortSignal === signal) {
      activeAbortSignal = null;
    }
  }

  async function request(path, options = {}) {
    const timeoutMs =
      typeof options.timeoutMs === "number" && options.timeoutMs > 0
        ? options.timeoutMs
        : DEFAULT_TIMEOUT_MS;

    const opts = Object.assign(
      { headers: { Accept: "application/json" } },
      options
    );
    delete opts.timeoutMs;

    if (opts.body && typeof opts.body === "object" && !(opts.body instanceof FormData)) {
      opts.headers = Object.assign({}, opts.headers, {
        "Content-Type": "application/json",
      });
      opts.body = JSON.stringify(opts.body);
    }

    const controller = new AbortController();
    let timedOut = false;
    const timer = setTimeout(function () {
      timedOut = true;
      try {
        controller.abort();
      } catch (_) {
        /* ignore */
      }
    }, timeoutMs);

    const externalSignal = opts.signal || activeAbortSignal || null;
    delete opts.signal;
    if (externalSignal) {
      if (externalSignal.aborted) {
        controller.abort();
      } else {
        externalSignal.addEventListener(
          "abort",
          function () {
            try {
              controller.abort();
            } catch (_) {
              /* ignore */
            }
          },
          { once: true }
        );
      }
    }
    opts.signal = controller.signal;

    try {
      const res = await fetch(API + path, opts);
      const text = await res.text();
      let data = null;
      try {
        data = text ? JSON.parse(text) : null;
      } catch (e) {
        data = { ok: false, message: text || "non-JSON response" };
      }
      return { ok: res.ok, status: res.status, data, timedOut: false };
    } catch (err) {
      const aborted = err && (err.name === "AbortError" || err.code === 20);
      if (timedOut) {
        return {
          ok: false,
          status: 0,
          data: {
            ok: false,
            message: "Request timed out after " + timeoutMs + "ms",
          },
          timedOut: true,
          error: "timeout",
        };
      }
      if (aborted) {
        return {
          ok: false,
          status: 0,
          data: { ok: false, message: "Request aborted" },
          timedOut: false,
          aborted: true,
          error: "aborted",
        };
      }
      return {
        ok: false,
        status: 0,
        data: {
          ok: false,
          message: (err && err.message) || "Network error",
        },
        timedOut: false,
        error: "network",
      };
    } finally {
      clearTimeout(timer);
    }
  }

  global.IcpApi = {
    DEFAULT_TIMEOUT_MS: DEFAULT_TIMEOUT_MS,
    setActiveAbortSignal: setActiveAbortSignal,
    clearActiveAbortSignal: clearActiveAbortSignal,
    status: () => request("/status"),
    protocols: () => request("/protocols"),
    configuration: () => request("/configuration"),
    putConfiguration: (doc) => request("/configuration", { method: "PUT", body: doc }),
    validateConfiguration: () => request("/configuration/validate", { method: "POST" }),
    saveConfiguration: () => request("/configuration/save", { method: "POST" }),
    loadConfiguration: () => request("/configuration/load", { method: "POST" }),
    importConfiguration: (text) =>
      request("/configuration/import", {
        method: "POST",
        body: text,
        headers: { "Content-Type": "application/json", Accept: "application/json" },
      }),
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
    executeCommand: (id, command, parameter) =>
      request("/equipment/" + encodeURIComponent(id) + "/command", {
        method: "POST",
        body: { command: command, parameter: parameter == null ? 0 : parameter },
      }),
    mappings: () => request("/mappings"),
    diagnostics: () => request("/diagnostics"),
    events: (limit = 100) => request("/events?limit=" + limit),
    history: (params = {}) => {
      const q = new URLSearchParams();
      Object.keys(params).forEach(function (k) {
        const v = params[k];
        if (v === undefined || v === null || v === "") return;
        q.set(k, String(v));
      });
      const qs = q.toString();
      return request("/history" + (qs ? "?" + qs : ""));
    },
    acknowledgeAlarmOccurrence: (occurrenceId, actorId) =>
      request("/alarms/occurrences/" + encodeURIComponent(occurrenceId) + "/acknowledge", {
        method: "POST",
        body: actorId ? { actorId: actorId } : {},
      }),
    historyExportUrl: (params = {}) => {
      const q = new URLSearchParams();
      Object.keys(params).forEach(function (k) {
        const v = params[k];
        if (v === undefined || v === null || v === "") return;
        q.set(k, String(v));
      });
      const qs = q.toString();
      return API + "/history/export" + (qs ? "?" + qs : "");
    },
    health: () => request("/health"),
  };
})(window);
