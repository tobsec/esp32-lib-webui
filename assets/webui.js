// esp32-lib-webui — behavioural primitives.
//
// Exposes `window.webui` with a small, opinionated surface used by
// the shell, by example panels, and by consumer-project panels.
//
//   webui.theme           — get/set/toggle the active theme; persists
//                           the choice to localStorage so the inline
//                           boot script can apply it before paint on
//                           subsequent loads.
//   webui.fetchAuth(url)  — fetch wrapper that surfaces non-2xx as
//                           exceptions and parses JSON. credentials:
//                           'include' so the browser's Basic-Auth
//                           cache rides along automatically.
//   webui.toast.ok/.err   — top-right toast notifications.
//   webui.tabs.discover() — scans a container for [data-tab] elements
//                           after panel injection, builds the tab nav,
//                           and wires activation + hash-sync.
//   webui.poll({...})     — Alpine x-data factory that polls a JSON
//                           endpoint at a fixed interval and exposes
//                           {data, error, freshness, refresh()}.
//
// More component factories (logViewer, atConsole, deviceTable, ...)
// will land in webui.components in a follow-up commit when the first
// consumer project actually composes them.

(function () {
  const win = window;

  const theme = {
    get current() {
      return document.documentElement.dataset.theme || "dark";
    },
    set(name) {
      document.documentElement.dataset.theme = name;
      try { localStorage.setItem("webui-theme", name); } catch (_) {}
      // Tell anyone listening the theme changed (e.g. a project widget
      // that wants to re-render a chart with new colours).
      win.dispatchEvent(new CustomEvent("webui:theme", { detail: name }));
    },
    toggle() { this.set(this.current === "dark" ? "light" : "dark"); }
  };

  async function fetchAuth(url, init = {}) {
    const r = await fetch(url, { credentials: "include", ...init });
    let body = null;
    try { body = await r.json(); } catch (_) { /* empty body or non-JSON */ }
    if (!r.ok) {
      const reason = (body && body.reason) || r.statusText;
      throw new Error(`${r.status} ${reason}`);
    }
    return body;
  }

  // ---- Toast --------------------------------------------------------

  function toast_root() {
    let host = document.querySelector(".webui-toasts");
    if (!host) {
      host = document.createElement("div");
      host.className = "webui-toasts";
      document.body.appendChild(host);
    }
    return host;
  }

  function toast_emit(msg, kind) {
    const el = document.createElement("div");
    el.className = "webui-toast " + kind;
    el.textContent = msg;
    toast_root().appendChild(el);
    setTimeout(() => el.remove(), 4000);
  }

  const toast = {
    ok(msg)  { toast_emit(msg, "ok"); },
    err(msg) { toast_emit(msg, "error"); }
  };

  // ---- Tab manager --------------------------------------------------
  //
  // Opt-in: if the panel container has no children with [data-tab]
  // attributes, discover() leaves the nav hidden and the panels render
  // in their natural document order (the lib's pre-tab behaviour).
  // Projects that want tabs declare them on top-level panel elements:
  //
  //   <section data-tab="status"  data-tab-label="Status">…</section>
  //   <section data-tab="devices" data-tab-label="Devices">…</section>
  //
  // The discover() call builds the tab nav from those, wires click
  // activation, and syncs the URL hash so deep-linking works.

  const tabs = {
    _nav: null,
    _container: null,
    _ids: [],

    discover(nav, container) {
      this._nav = nav;
      this._container = container;
      const panels = container.querySelectorAll("[data-tab]");
      if (panels.length === 0) {
        nav.style.display = "none";
        return false;
      }
      this._ids = [];
      nav.style.display = "";
      nav.innerHTML = "";
      panels.forEach(p => {
        const id = p.dataset.tab;
        const label = p.dataset.tabLabel || id;
        const btn = document.createElement("button");
        btn.type = "button";
        btn.className = "webui-tab";
        btn.dataset.tabTarget = id;
        btn.textContent = label;
        btn.addEventListener("click", () => this.activate(id));
        nav.appendChild(btn);
        p.classList.add("webui-panel");
        this._ids.push(id);
      });
      const from_hash = (location.hash || "").replace(/^#/, "");
      const initial = this._ids.includes(from_hash) ? from_hash : this._ids[0];
      this.activate(initial);
      return true;
    },

    activate(id) {
      if (!this._ids.includes(id)) return;
      this._container.querySelectorAll("[data-tab]").forEach(p => {
        p.classList.toggle("active", p.dataset.tab === id);
      });
      this._nav.querySelectorAll(".webui-tab").forEach(b => {
        b.classList.toggle("active", b.dataset.tabTarget === id);
      });
      try { history.replaceState(null, "", "#" + id); } catch (_) {}
      win.dispatchEvent(new CustomEvent("webui:tab", { detail: id }));
    },

    current() {
      const active = this._nav && this._nav.querySelector(".webui-tab.active");
      return active ? active.dataset.tabTarget : null;
    }
  };

  // ---- Polling x-data factory --------------------------------------
  //
  // Usage:
  //   <article x-data="webui.poll({endpoint:'/api/status',
  //                                intervalMs: 5000})">
  //     <pre x-text="JSON.stringify(data, null, 2)"></pre>
  //     <small x-text="freshness"></small>
  //   </article>
  //
  // The factory returns a plain object with reactive fields Alpine
  // will hoist into its component scope. init() and destroy() match
  // Alpine's component lifecycle hooks.

  function poll(opts) {
    const { endpoint, intervalMs = 5000 } = opts || {};
    return {
      data: {},
      error: "",
      fetched_at: 0,
      _timer: null,
      _tick: null,

      get freshness() {
        if (!this.fetched_at) return "";
        const age = Math.round((Date.now() - this.fetched_at) / 1000);
        return `updated ${age} s ago`;
      },

      async refresh() {
        try {
          this.data = await fetchAuth(endpoint);
          this.error = "";
          this.fetched_at = Date.now();
        } catch (e) {
          this.error = e.message;
        }
      },

      init() {
        this.refresh();
        this._timer = setInterval(() => this.refresh(), intervalMs);
        // Re-touch fetched_at every second so the freshness getter
        // re-evaluates without re-fetching the endpoint.
        this._tick = setInterval(() => {
          this.fetched_at = this.fetched_at;
        }, 1000);
      },

      destroy() {
        if (this._timer) { clearInterval(this._timer); this._timer = null; }
        if (this._tick)  { clearInterval(this._tick);  this._tick  = null; }
      }
    };
  }

  win.webui = { theme, fetchAuth, toast, tabs, poll, components: {} };
})();
