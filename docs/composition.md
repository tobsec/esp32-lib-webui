# Composition cookbook

How to assemble a project-specific admin UI from the toolkit
primitives the lib ships. The lib gives you:

* a tab-capable HTML shell (header + nav + container + built-in
  `/api/status` card)
* a CSS layer (`webui.css`) of visual primitives — header, tab nav,
  cards via Pico, log pane, signal bar, AT-console grid, toasts,
  spinner — all coloured via `var(--*)` tokens
* two themes (`dark`, `light`) and a `data-theme` switcher that
  persists to `localStorage`
* behavioural primitives on `window.webui`:
  - `theme`, `fetchAuth`, `postAuth`, `toast` — generic helpers
  - `tabs.discover(nav, container)` — auto-builds tab nav from
    `[data-tab]` elements (runs after panel injection)
  - `poll({endpoint, intervalMs})` — Alpine `x-data` factory
  - `components.logViewer({endpoint, refreshMs, ...})` — Alpine
    factory for log/event tables
  - `components.atConsole({endpoint, ...})` — Alpine factory for
    request/response consoles

Project panels live in **your project**: `panels.html` describes the
tabs and what's on them; `panels.js` (optional) adds bespoke Alpine
components when the factories above aren't enough.

## Smallest panel — no tabs, one card

```html
<!-- main/web_assets/panels.html -->
<article x-data="webui.poll({endpoint:'/api/network', intervalMs:5000})">
  <header><strong>Network</strong></header>
  <table>
    <tr><th>IP</th><td x-text="data.ip ?? '—'"></td></tr>
    <tr><th>Gateway</th><td x-text="data.gateway ?? '—'"></td></tr>
    <tr><th>DNS</th><td x-text="data.dns ?? '—'"></td></tr>
    <tr x-show="error"><td colspan="2"><kbd x-text="error"></kbd></td></tr>
  </table>
  <footer><small class="webui-muted" x-text="freshness"></small></footer>
</article>
```

No `[data-tab]` attribute → the tab manager stays hidden and the
panel renders directly below the built-in status card.

## Multiple tabs

The tab manager activates the moment a panel element declares
`data-tab="<id>"`. `data-tab-label="..."` controls the label that
appears in the nav strip; missing → `id` is used verbatim.

```html
<!-- main/web_assets/panels.html -->
<section data-tab="network" data-tab-label="Network"
         x-data="webui.poll({endpoint:'/api/network', intervalMs:5000})">
  <article>
    <header><strong>Wi-Fi</strong></header>
    <table>
      <tr><th>SSID</th><td x-text="data.ssid ?? '—'"></td></tr>
      <tr><th>RSSI</th><td x-text="(data.rssi ?? '—') + ' dBm'"></td></tr>
    </table>
  </article>
</section>

<section data-tab="devices" data-tab-label="Devices"
         x-data="webui.poll({endpoint:'/api/devices', intervalMs:3000})">
  <article>
    <header><strong>Connected</strong> — <small x-text="(data.list ?? []).length"></small></header>
    <table>
      <thead><tr><th>MAC</th><th>Type</th><th>RSSI</th></tr></thead>
      <tbody>
        <template x-for="d in (data.list ?? [])" :key="d.mac">
          <tr>
            <td x-text="d.mac"></td>
            <td x-text="d.type"></td>
            <td x-text="d.rssi + ' dBm'"></td>
          </tr>
        </template>
      </tbody>
    </table>
  </article>
</section>

<section data-tab="system" data-tab-label="System">
  <article>
    <header><strong>Actions</strong></header>
    <button type="button" @click="if (confirm('Reboot now?')) webui.postAuth('/api/reboot')">
      Reboot
    </button>
  </article>
</section>
```

The shell does the rest — building the nav strip, hash-syncing the
active tab, hiding inactive panels.

## Persistent log viewer

```html
<section data-tab="syslog" data-tab-label="System log"
         x-data="webui.components.logViewer({
                   endpoint: '/api/syslog',
                   refreshMs: 2000,
                   maxLines: 64,
                   parser: (p) => (p.entries ?? []).map(e =>
                     `${new Date(e.uptime_ms).toISOString().substr(11,8)}  ${e.level.padEnd(8)}  ${e.category.padEnd(8)}  ${e.message}`
                   )
                 })">
  <article>
    <header>
      <strong>System log</strong>
      <label style="float:right">
        <input type="checkbox" x-model="autoscroll"> autoscroll
      </label>
    </header>
    <pre class="webui-log-pane" x-init="bind_pane($el)"
         x-text="lines.join('\n')"></pre>
    <footer>
      <button type="button" @click="refresh()">Refresh</button>
      <button type="button" @click="clear()">Clear (view only)</button>
      <small class="webui-muted" x-text="freshness" style="float:right"></small>
    </footer>
  </article>
</section>
```

`bind_pane($el)` hands the `<pre>` element to the factory so the
auto-scroll logic can drive `scrollTop` without DOM-query gymnastics.

## AT command console

```html
<section data-tab="at" data-tab-label="AT console"
         x-data="webui.components.atConsole({
                   endpoint: '/api/at',
                   defaultTimeoutMs: 5000 })">
  <article>
    <header><strong>SIM7080G console</strong></header>

    <div class="webui-at-console">
      <form class="webui-at-console-input"
            @submit.prevent="send()">
        <input type="text" x-model="cmd"
               placeholder="AT+CSQ"
               :disabled="sending"
               autofocus>
        <button type="submit" :disabled="sending || !cmd.trim()">
          <span x-show="sending" class="webui-spinner"></span>
          <span x-show="!sending">Send</span>
        </button>
      </form>

      <pre class="webui-log-pane"><template x-for="(h, i) in history" :key="i"
        ><span :class="h.ok ? '' : 'webui-muted'"
        >> <span x-text="h.cmd"></span><br><span x-text="h.response"></span><br><br></span
      ></template></pre>
    </div>

    <footer>
      <button type="button" @click="clear()">Clear history</button>
      <span x-show="error" class="webui-muted" x-text="error"></span>
    </footer>
  </article>
</section>
```

The factory owns the `cmd`, `history`, `sending`, `error` state and
the POST flow. The HTML is just bindings.

## Reusing webui.poll's data

A common pattern: one polled endpoint feeds several visual chunks on
the same tab. Hoist the poll component to the section level; child
elements reach `data` via Alpine's scope inheritance.

```html
<section data-tab="status" data-tab-label="Status"
         x-data="webui.poll({endpoint:'/api/status', intervalMs:5000})">
  <article>
    <header><strong>Signal</strong></header>
    <table>
      <tr><th>CSQ</th><td x-text="data.csq ?? '—'"></td></tr>
      <tr><th>Strength</th>
          <td>
            <span class="webui-signal-bar">
              <template x-for="b in 5">
                <span :class="{ on: (data.csq ?? 0) >= b * 6 }"
                      :style="`height:${4 + b * 3}px`"></span>
              </template>
            </span>
          </td>
      </tr>
    </table>
  </article>
  <article>
    <header><strong>Gateway</strong></header>
    <table>
      <tr><th>MQTT</th><td x-text="data.mqtt_state ?? '—'"></td></tr>
      <tr><th>Last publish</th><td x-text="data.last_publish ?? '—'"></td></tr>
    </table>
  </article>
</section>
```

## Project-specific theme

The lib loads `/theme.css` with `onerror="this.remove()"`, so the
`<link>` silently disappears if you don't ship one. To add a custom
palette, register a route from your project and define a new
`[data-theme="..."]` block in the served CSS:

```css
/* main/web_assets/theme.css */
[data-theme="myproject"] {
  --webui-accent:       #6c5ce7;
  --webui-accent-hover: #4b3eb3;
  --pico-primary:       #6c5ce7;
  --pico-primary-hover: #4b3eb3;
}
```

Then in your C glue (mirror of how you serve `panels.html`):

```cpp
extern const char theme_css_start[] asm("_binary_theme_css_start");
extern const char theme_css_end[]   asm("_binary_theme_css_end");

esp_err_t handle_theme(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, theme_css_start,
                    theme_css_end - theme_css_start - 1);
    return ESP_OK;
}
// register_route(server, HTTP_GET, "/theme.css", handle_theme);
```

And add a tiny boot script (or call from your `panels.js`) to switch
to it on demand:

```js
webui.theme.set('myproject');
```

The lib's built-in `dark` / `light` toggle button only flips between
those two; if you want a tri-state cycle including your custom
theme, override the toggle by binding `@click` on the button to your
own handler.

## Backward compatibility

Pre-toolkit panel files that called `WebShell.fetch_json` /
`WebShell.toast` keep working — the shell exposes them as aliases of
`webui.fetchAuth` / `webui.toast`. You can migrate one panel at a
time; mixing styles in the same project is fine.

## Where to put what

Quick reference for "lib or project?":

| It does … | Lives in … |
|-----------|-----------|
| HTTP + Auth + OTA + status core | lib (`src/web/`) |
| The shell HTML, tab nav, header, theme switch | lib (`assets/`) |
| Visual primitives (cards via Pico, log pane, signal bar, AT-grid, toast, spinner) | lib (`webui.css`) |
| Generic Alpine factories (poll, logViewer, atConsole) | lib (`webui.js`) |
| Default themes (dark, light) | lib (`assets/themes/`) |
| Project-specific tabs + their REST endpoints | **your project** |
| Project-specific Alpine bespoke components | **your project** (`panels.js`) |
| Brand palette / custom theme | **your project** (`theme.css`) |

When in doubt: would two unrelated consumer projects want this
verbatim? Yes → lib. No → consumer.
