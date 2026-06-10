# esp32-lib-webui

Embedded web UI / admin / signed-upload OTA component for ESP-IDF projects on the ESP32 family.

Provides:

- HTTP server bootstrap with HTTP Basic-Auth middleware
- Project-agnostic `/api/status` endpoint with an extension hook for project-specific fields
- `/api/reboot` deferred restart
- Signed-upload OTA via `POST /api/ota/upload` with Ed25519 verification (libsodium) and bootloader-rollback support
- Embedded shell HTML/CSS/JS plus vendored Pico CSS + Alpine.js — drag-and-drop reactive UI, no build toolchain
- A panel-injection mechanism so each consumer project ships its own `panels.html` + `panels.js` without forking the shell

## Status

Pre-1.0. The API is stable enough that a consumer project (`esp32-homekey`) lives against it; expect breaking changes through 0.x.

## Design philosophy

The lib is a **toolkit**, not a finished application. It ships the bits that every embedded admin UI needs verbatim — HTTP server bootstrap, Basic-Auth, signed-upload OTA, a `/api/status` core, a tab-capable HTML shell, a Pico+Alpine bundle, a default dark and light theme, and a small set of CSS + JS primitives (`webui.css`, `webui.js`) that cover the visual and behavioural patterns that recur across embedded gateways: tab navigation, status polling, signal bars, log panes, toasts, theme switching.

What lives in the **consumer project**, not in the lib:
- The actual list of tabs and their compositions — a Status tab built from which KPI rows, an AT-Console tab pointed at which endpoint, a Devices tab consuming which payload schema. These are project decisions, expressed in `panels.html` / `panels.js`.
- The REST endpoints behind those tabs. The lib never grows a `/api/at` or `/api/cellular/operators` route; those belong to the project that owns the modem.
- Project-specific themes. If a consumer wants a brand palette, it ships its own `theme.css` via `EMBED_TXTFILES` registering itself as a new `[data-theme="myproject"]` block and overriding the `--webui-*` and `--pico-*` tokens it cares about.

When a candidate feature comes up, the test is at the right granularity: a generic primitive (KPI tile, log pane, AT-console widget, status polling factory) passes the *"would two unrelated consumer projects want this verbatim?"* test and belongs in the lib. A concrete composition (the Brachberg gateway's Status tab with these exact eight fields wired to those exact endpoints) fails the test and stays in the consumer.

This split keeps the lib small enough to be a reusable dependency while still giving a fresh consumer enough material that its first-shot UI looks finished — not a naked Pico page that someone has to design from scratch.

## Repo layout

```
include/web/        public headers (server, auth, status_core, system_routes, ota)
src/web/            implementations
assets/             shell HTML/CSS/JS + vendored Pico + Alpine
tools/ota_sign.py   PyNaCl-based local image signer
examples/minimal/   smallest-possible glue stub for a new project
docs/integration.md how to consume from your project (managed component vs submodule)
```

## Quick start (consume as managed component)

In your project's `main/idf_component.yml`:

```yaml
dependencies:
  idf: ">=5.5"
  tobsec/esp32-lib-webui:
    version: ">=0.1"
    git: https://github.com/tobsec/esp32-lib-webui.git
```

In your `main/main.cpp` after Wi-Fi connects:

```cpp
#include "web/server.h"
#include "web/status_core.h"
#include "web/system_routes.h"
#include "web/ota.h"

webui::Config cfg;
cfg.user = "admin";
cfg.pass = "secret";       // pull from your secrets.h
auto server = webui::start(cfg);

webui::register_shell_assets(server);
webui::status_core::register_routes(server);
webui::system_routes::register_routes(server);

webui::ota::Config ota_cfg{};
webui::ota::parse_pubkey_hex("YOUR_64_HEX_CHARS", ota_cfg.pubkey);
webui::ota::register_routes(server, ota_cfg);

// Project-specific routes go here — your /api/yourthing handlers, etc.
```

See `examples/minimal/` for a complete glue stub.

## OTA signing workflow

```bash
# Once on the build host:
python -c "import os; open('tools/.ota_signing_key','wb').write(os.urandom(32))"

# After every build:
python tools/ota_sign.py build/<project>.bin
# Prints the public key the first time — paste into your secrets.h
# HK_OTA_SIGNING_PUBKEY (or whatever name your project uses).
```

The signed `.bin` is what you POST to `/api/ota/upload`. The reader verifies the trailing 64-byte Ed25519 signature against its compiled-in pubkey and, only on a match, flips its boot partition.

## License

MIT (see [LICENSE](LICENSE)). Vendored Pico CSS and Alpine.js are MIT-licensed by their respective authors; see [NOTICE](NOTICE) and the LICENSE files inside `assets/vendor/`.
