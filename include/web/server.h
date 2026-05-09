#pragma once

// Lifecycle wrapper around esp_http_server, the primary web admin
// surface for both the standalone reader and (later) the controller.
// Owns the httpd handle and the Basic-Auth credentials; offers a thin
// register_route() that wraps every handler in the auth check.
//
// Project-specific glue (web_ui_glue.cpp) is responsible for calling
// start() once Wi-Fi is up and then registering whatever
// project-specific routes it needs alongside the common routes that
// reader_core/web/status_core.cpp + reader_core/web/system_routes.cpp
// install themselves.

#include <esp_err.h>
#include <esp_http_server.h>

#include <string>

namespace reader_core::web {

struct Config {
    // Basic-Auth credentials. The handler returns 401 + WWW-Authenticate
    // until the client presents a matching Authorization header.
    std::string user;
    std::string pass;
    // BSD-style realm string surfaced in the WWW-Authenticate challenge.
    std::string realm = "homekey-reader";
    // Listening port. 8080 by default — HomeSpan grabs port 80 for the
    // HAP server and esp_http_server can't bind a second listener on
    // the same port. Override via Config{...}.port if needed.
    uint16_t    port  = 8080;
};

// Brings up esp_http_server with the supplied config. Returns the
// httpd_handle_t so callers can register additional routes via
// register_route(). Returns nullptr on failure (already logged).
httpd_handle_t start(const Config& cfg);

// Tears down the server. Safe to call even if start() failed.
void stop();

// Returns the current Config — used by auth.cpp to look up the
// expected credentials inside the authentication middleware.
const Config& config();

// Registers a route with auth-middleware applied. The handler runs
// only after auth::check_basic() succeeds; otherwise the middleware
// emits 401 and the handler is never called.
//
// `ctx` is opaque user data forwarded to the handler via
// `httpd_req_t::user_ctx` — pass `nullptr` if the handler doesn't
// need state.
esp_err_t register_route(httpd_handle_t       server,
                         httpd_method_t       method,
                         const char*          uri,
                         esp_err_t (*handler)(httpd_req_t*),
                         void*                ctx = nullptr);

// Registers GET / + GET /shell.css + GET /shell.js handlers backed by
// the embedded shell assets. Project glue still has to register
// /panels.html and /panels.js for its own UI fragments.
esp_err_t register_shell_assets(httpd_handle_t server);

}  // namespace reader_core::web
