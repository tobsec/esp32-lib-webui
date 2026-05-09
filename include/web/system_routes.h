#pragma once

// Project-agnostic system routes:
//   POST /api/reboot — schedules an esp_restart() ~1 s after the
//                       response so the client gets a 200 first.
//
// Mounted alongside status_core::register_routes() from project glue.

#include <esp_err.h>
#include <esp_http_server.h>

namespace webui::system_routes {

esp_err_t register_routes(httpd_handle_t server);

}  // namespace webui::system_routes
