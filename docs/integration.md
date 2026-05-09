# Integration guide

How to consume `esp32-lib-webui` from another ESP-IDF project. Two
supported paths.

## Path A — managed component (preferred for ESP-IDF native projects)

In your project's `main/idf_component.yml`:

```yaml
dependencies:
  idf: ">=5.5"
  tobsec/esp32-lib-webui:
    version: ">=0.1"
    git: https://github.com/tobsec/esp32-lib-webui.git
```

`idf.py reconfigure` resolves the dependency, clones the repo into
`managed_components/REPLACE_ME__esp32-lib-webui/`, and adds it to the
component graph automatically.

Pin to a specific commit by adding `commit: abc1234` next to `git:`.

## Path B — git submodule (for projects that already use submodules)

```bash
mkdir -p components
git submodule add https://github.com/tobsec/esp32-lib-webui.git components/esp32-lib-webui
```

In your top-level `CMakeLists.txt`:

```cmake
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")
```

The `idf_component_register(...)` block in this repo's root CMakeLists
takes care of the rest.

## Path C — PlatformIO / arduino-esp32 projects

Same as Path B (git submodule + `lib_extra_dirs` in `platformio.ini`):

```ini
lib_extra_dirs =
    components/esp32-lib-webui
```

This component depends on `esp_http_server`, `app_update`, `mbedtls`,
`cjson`, and `libsodium` — the latter two are managed components in the
ESP-IDF Component Registry and Arduino projects need to add them
explicitly.

## Wiring in your project

Three things you need to provide:

### 1. Auth credentials and OTA pubkey

In your project's local `secrets.h` (gitignored):

```c
#define WEBUI_USER          "admin"
#define WEBUI_PASS          "your-strong-password"
#define OTA_SIGNING_PUBKEY  "60D0B3AF876AA24775CC36CC2AD19E524B4A7C9FE81AF4506FA53F587DB00B81"
```

The pubkey hex is what `tools/ota_sign.py` prints on first run.

### 2. Boot the server after Wi-Fi has an IP

```cpp
#include "web/server.h"
#include "web/status_core.h"
#include "web/system_routes.h"
#include "web/ota.h"

void start_web_ui() {
    webui::Config cfg;
    cfg.user = WEBUI_USER;
    cfg.pass = WEBUI_PASS;
    auto server = webui::start(cfg);
    if (!server) return;

    webui::register_shell_assets(server);
    webui::status_core::register_routes(server);
    webui::system_routes::register_routes(server);

    webui::ota::Config ota_cfg{};
    if (webui::ota::parse_pubkey_hex(OTA_SIGNING_PUBKEY, ota_cfg.pubkey)) {
        webui::ota::register_routes(server, ota_cfg);
    }

    // Project-specific routes go here.
    register_my_routes(server);
}
```

Note: HomeSpan grabs port 80. Default port is 8080; override via
`Config::port` if you need a different one.

### 3. Project-specific UI panels

Place these in your project's main component:

```
main/web_assets/panels.html
main/web_assets/panels.js
```

Then in your main `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.cpp"
    REQUIRES esp32-lib-webui
    EMBED_TXTFILES
        "web_assets/panels.html"
        "web_assets/panels.js"
)
```

Serve them from a route handler that this component cannot register
itself (because the binary symbols `_binary_panels_*` are scoped to your
project's component, not ours):

```cpp
extern const char panels_html_start[] asm("_binary_panels_html_start");
extern const char panels_html_end[]   asm("_binary_panels_html_end");
// ... and same for panels.js

esp_err_t handle_panels_html(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, panels_html_start, panels_html_end - panels_html_start - 1);
    return ESP_OK;
}
// register both /panels.html and /panels.js
```

The shell.js loader fetches these on page load and re-runs Alpine on the
injected subtree (`Alpine.initTree`), so your project's `panels.js` can
register its own `Alpine.data(...)` components against `window.Alpine`.

## Probe-phase rollback

If the consumer enables `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, the
bootloader rolls back to the previous slot after 3 consecutive boot
failures. To make a freshly-flashed slot stick, the consumer must call
`esp_ota_mark_app_valid_cancel_rollback()` once it considers the slot
healthy. The simplest pattern is a 30-second probe task spawned from
`app_main` — see `examples/minimal/main/main.cpp`.

## See also

- [README.md](../README.md) — overview, status, license
- [NOTICE](../NOTICE) — third-party attributions (Pico CSS, Alpine.js)
- `examples/minimal/` — drop-in glue stub for a new project
