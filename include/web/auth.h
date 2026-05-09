#pragma once

// HTTP Basic Auth helper for esp_http_server handlers.
// Heimnetz-only PoC — no TLS in this iteration. Credentials live in
// the project's secrets.h and are passed into web::start() via Config.

#include <esp_err.h>
#include <esp_http_server.h>

namespace reader_core::web::auth {

// Checks the Authorization header against the credentials stored in
// the active web::Config. If the request authenticates, returns
// ESP_OK and lets the caller proceed to the handler body. If it
// doesn't, sends a 401 with WWW-Authenticate: Basic realm=... and
// returns ESP_FAIL — the caller must propagate that return code so
// the server doesn't double-respond.
esp_err_t check_basic(httpd_req_t* req);

}  // namespace reader_core::web::auth
