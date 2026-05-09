#pragma once

// Project-agnostic /api/status JSON. Always returns:
//   firmware_version (from esp_app_get_description())
//   uptime_s         (from esp_timer_get_time)
//   free_heap        (esp_get_free_heap_size)
//   min_free_heap    (esp_get_minimum_free_heap_size)
//   reset_reason     (esp_reset_reason as a short string)
//
// Project glue can attach a provider lambda via set_provider() to
// append project-specific fields (HomeKey state, last-card-seen, ...)
// to the same JSON object before it's serialised.

#include <esp_err.h>
#include <esp_http_server.h>
#include <cJSON.h>

#include <functional>

namespace reader_core::web::status_core {

// Lambda type. Invoked for every /api/status request. The provider
// must mutate the supplied cJSON object only by adding fields — do
// not delete or replace existing keys.
using Provider = std::function<void(cJSON*)>;

// Registers GET /api/status on the supplied server. Call once during
// glue::start().
esp_err_t register_routes(httpd_handle_t server);

// Installs the project-specific provider. Pass an empty std::function
// (default) to remove a previously installed provider.
void set_provider(Provider p = {});

}  // namespace reader_core::web::status_core
