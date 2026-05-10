// Shared shell — wires Alpine components for the common /api/status
// card, exposes a tiny WebShell helper API for project panels, and
// fetches project-specific HTML/JS into <div id="project-panels">.
//
// Project glue serves /panels.html and /panels.js. We inject the HTML,
// then let Alpine.initTree(...) discover the new x-* directives on
// the freshly-mounted subtree, then load /panels.js so it can register
// stores against window.Alpine.

(function () {
  const $ = (sel) => document.querySelector(sel);

  function format_uptime(s) {
    if (!Number.isFinite(s)) return "—";
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    const sec = Math.floor(s % 60);
    if (d > 0) return `${d}d ${h}h ${m}m`;
    if (h > 0) return `${h}h ${m}m`;
    if (m > 0) return `${m}m ${sec}s`;
    return `${sec}s`;
  }

  function format_bytes(n) {
    if (!Number.isFinite(n)) return "—";
    if (n >= 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MB";
    if (n >= 1024)        return (n / 1024).toFixed(1) + " KB";
    return n + " B";
  }

  // Toast notifications. Stacks top-right, auto-dismisses after 4 s.
  function ensure_toast_root() {
    let host = $("#toasts");
    if (!host) {
      host = document.createElement("div");
      host.id = "toasts";
      document.body.appendChild(host);
    }
    return host;
  }
  function toast(msg, kind = "ok") {
    const host = ensure_toast_root();
    const el = document.createElement("div");
    el.className = "toast " + kind;
    el.textContent = msg;
    host.appendChild(el);
    setTimeout(() => el.remove(), 4000);
  }

  // Wrapper around fetch that surfaces non-2xx as exceptions and
  // returns parsed JSON when possible. Used by the status component
  // here and by project panels via window.WebShell.fetch_json.
  async function fetch_json(url, init = {}) {
    const r = await fetch(url, init);
    let body = null;
    try { body = await r.json(); } catch (_) { /* empty body or non-JSON */ }
    if (!r.ok) {
      const reason = (body && body.reason) || r.statusText;
      throw new Error(`${r.status} ${reason}`);
    }
    return body;
  }

  // Common / project-agnostic fields exposed on the status card.
  // Anything else the server appends is shown verbatim under these.
  const STATIC_LABELS = {
    firmware_version: "Firmware",
    uptime_s:         "Uptime",
    free_heap:        "Free heap",
    min_free_heap:    "Min heap",
    reset_reason:     "Last reset",
  };

  document.addEventListener("alpine:init", () => {
    Alpine.data("status", () => ({
      raw: {},
      error: "",
      fetched_at: 0,

      get entries() {
        const out = [];
        for (const [k, label] of Object.entries(STATIC_LABELS)) {
          if (!(k in this.raw)) continue;
          let v = this.raw[k];
          if (k === "uptime_s")     v = format_uptime(v);
          if (k === "free_heap")    v = format_bytes(v);
          if (k === "min_free_heap")v = format_bytes(v);
          out.push([label, v ?? "—"]);
        }
        // Project-specific fields the server appended via the provider.
        for (const [k, v] of Object.entries(this.raw)) {
          if (k in STATIC_LABELS) continue;
          out.push([k, String(v)]);
        }
        return out;
      },

      get freshness() {
        if (!this.fetched_at) return "";
        const age = Math.round((Date.now() - this.fetched_at) / 1000);
        return `updated ${age} s ago`;
      },

      async load() {
        try {
          this.raw = await fetch_json("/api/status");
          this.error = "";
          this.fetched_at = Date.now();
          $("#fw-version").textContent = this.raw.firmware_version || "—";
          if (this.raw.deployment) {
            $("#fw-version").textContent += ` · ${this.raw.deployment}`;
          }
        } catch (e) {
          this.error = "status fetch failed: " + e.message;
        }
      },

      init() {
        this.load();
        setInterval(() => this.load(), 5000);
        // Re-render the freshness label every second without re-fetching.
        setInterval(() => { this.fetched_at = this.fetched_at; }, 1000);
      },
    }));
  });

  // API surface for project panel scripts.
  window.WebShell = { fetch_json, toast };

  // Pull project-specific HTML + JS once Alpine is up.
  //
  // Order matters: panels.js MUST finish executing before we inject
  // panels.html into the DOM. Alpine has a global MutationObserver
  // that automatically walks newly-inserted nodes; the moment an
  // x-data="uids" attribute lands in the document, Alpine looks up
  // Alpine.data("uids", ...) and throws "uids is not defined" if
  // the registration hasn't happened yet. innerHTML assignment
  // *itself* is what triggers the walk — calling Alpine.initTree
  // afterwards is too late.
  //
  // So: kick off both fetches in parallel, await the script, *then*
  // inject the HTML.
  async function load_project_panels() {
    try {
      const html_p = fetch("/panels.html").then(r => r.ok ? r.text() : null);
      const js_p = new Promise((resolve) => {
        const s = document.createElement("script");
        s.src = "/panels.js";
        s.onload  = resolve;
        s.onerror = resolve;  // 404 is fine; no project-specific Alpine.data
        document.body.appendChild(s);
      });

      await js_p;

      const html = await html_p;
      if (html === null) return;
      $("#project-panels").innerHTML = html;
    } catch (_) { /* panels are optional */ }
  }

  // Alpine fires 'alpine:initialized' once it's done bootstrapping.
  document.addEventListener("alpine:initialized", load_project_panels);
})();
