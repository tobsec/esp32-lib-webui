#include "web/system_routes.h"

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "web/server.h"

namespace webui::system_routes {

namespace {

constexpr const char* kTag = "sys_routes";

void reboot_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGW(kTag, "rebooting on /api/reboot request");
    esp_restart();
}

esp_err_t handle_reboot(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, R"({"status":"rebooting","delay_s":1})");
    // Defer the actual restart so the response flushes first. 4 KB is
    // plenty for a task that just sleeps then calls esp_restart().
    xTaskCreate(reboot_task, "reboot", 4096, nullptr, 5, nullptr);
    return ESP_OK;
}

}  // namespace

esp_err_t register_routes(httpd_handle_t server) {
    return register_route(server, HTTP_POST, "/api/reboot", handle_reboot);
}

}  // namespace webui::system_routes
