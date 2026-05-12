#pragma once

// HTTP Basic Auth helper for esp_http_server handlers.
// Heimnetz-only PoC — no TLS in this iteration.
//
// Credential lifecycle (open-until-secured pattern, #106):
//  1. At webui::start(), the initial credentials come from Config.user
//     / Config.pass. Controller-side code typically seeds these from
//     NVS (if a password was set previously) falling back to secrets.h
//     (dev-override) or empty (first-boot unsecured mode).
//  2. set_active_credentials() can be called later (e.g. from the
//     setup-password endpoint or `set-password` console command) to
//     change them at runtime without restarting the server.
//  3. is_secured() returns true only when both fields are non-empty;
//     handlers in front of a public setup-flow check this to render
//     the "set your password" banner.
//  4. check_basic() short-circuits to ESP_OK in unsecured mode so
//     first-boot users can reach the setup endpoint. Once secured,
//     standard Basic-Auth applies to every route except those
//     registered via webui::register_public_route().

#include <esp_err.h>
#include <esp_http_server.h>

#include <string>

namespace webui::auth {

esp_err_t check_basic(httpd_req_t* req);

// Replace the in-memory active credentials. Empty strings transition
// back to unsecured mode (anyone can POST /api/setup-password).
// Thread-safe — guarded by an internal mutex shared with check_basic.
void set_active_credentials(const std::string& user, const std::string& pass);

// True when both user + pass are non-empty (i.e. Basic-Auth is enforced).
bool is_secured();

}  // namespace webui::auth
