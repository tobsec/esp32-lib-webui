#include "web/status_core.h"

#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "web/server.h"

namespace webui::status_core {

namespace {

constexpr const char* kTag = "status_core";

Provider g_provider;

const char* reset_reason_str(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_UNKNOWN:   return "unknown";
        case ESP_RST_POWERON:   return "poweron";
        case ESP_RST_EXT:       return "ext";
        case ESP_RST_SW:        return "sw";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "?";
    }
}

esp_err_t handle_status(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    const esp_app_desc_t* desc = esp_app_get_description();
    if (desc) cJSON_AddStringToObject(root, "firmware_version", desc->version);

    const int64_t uptime_us = esp_timer_get_time();
    cJSON_AddNumberToObject(root, "uptime_s", static_cast<double>(uptime_us) / 1e6);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddStringToObject(root, "reset_reason", reset_reason_str(esp_reset_reason()));

    if (g_provider) g_provider(root);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    return ESP_OK;
}

}  // namespace

void set_provider(Provider p) { g_provider = std::move(p); }

esp_err_t register_routes(httpd_handle_t server) {
    return register_route(server, HTTP_GET, "/api/status", handle_status);
}

}  // namespace webui::status_core
