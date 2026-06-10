// Shell wiring — wires the built-in /api/status Alpine card, runs the
// tab discoverer over injected project panels, exposes a small
// `WebShell` alias for backward compatibility with panels written
// against the pre-toolkit API.
//
// The lib's behavioural primitives (theme switch, fetchAuth, toast,
// tab manager, poll factory) live in webui.js and are loaded by
// shell.html before this file. We just compose them here.

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
        // Project-specific fields the server appended via the status
        // provider hook.
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
          this.raw = await webui.fetchAuth("/api/status");
          this.error = "";
          this.fetched_at = Date.now();
          // Drive the lib shell's header from the status payload.
          const title = $("#webui-title");
          if (title) {
            const name = this.raw.device_name || this.raw.firmware_version || "Web UI";
            title.textContent = name;
            document.title = name;
          }
          const dot = $("#webui-status-dot");
          if (dot) dot.classList.toggle("on", true);
        } catch (e) {
          this.error = "status fetch failed: " + e.message;
          const dot = $("#webui-status-dot");
          if (dot) dot.classList.toggle("on", false);
        }
      },

      init() {
        this.load();
        setInterval(() => this.load(), 5000);
        setInterval(() => { this.fetched_at = this.fetched_at; }, 1000);
      },
    }));
  });

  // Backward-compatibility surface. Pre-toolkit panel scripts call
  // window.WebShell.fetch_json + window.WebShell.toast; alias them
  // onto the new webui.* primitives so they keep working.
  window.WebShell = {
    fetch_json: (...args) => webui.fetchAuth(...args),
    toast:      (msg, kind = "ok") =>
                  kind === "ok" ? webui.toast.ok(msg) : webui.toast.err(msg)
  };

  // Pull project-specific HTML + JS once Alpine is up.
  //
  // Order matters: panels.js MUST finish executing before we inject
  // panels.html into the DOM. Alpine has a global MutationObserver
  // that automatically walks newly-inserted nodes; the moment an
  // x-data="thing" attribute lands in the document, Alpine looks up
  // Alpine.data("thing", ...) and throws "thing is not defined" if
  // the registration hasn't happened yet. innerHTML assignment is
  // itself what triggers the walk — calling Alpine.initTree
  // afterwards is too late.
  //
  // So: kick off both fetches in parallel, await the script, then
  // inject the HTML, then run the tab discoverer over the injected
  // subtree.
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
      const host = $("#project-panels");
      host.innerHTML = html;

      // If the project declared [data-tab] panels, build the tab nav.
      // Otherwise leave the nav hidden and let panels render in flow.
      webui.tabs.discover($("#webui-tab-nav"), host);
    } catch (_) { /* panels are optional */ }
  }

  // Alpine fires 'alpine:initialized' once it's done bootstrapping.
  document.addEventListener("alpine:initialized", load_project_panels);
})();
