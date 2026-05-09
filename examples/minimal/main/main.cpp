// Minimal example: bring up Wi-Fi (station mode), boot the web UI,
// register one project-specific endpoint, run a 30 s OTA probe-phase.
// Replace WIFI_SSID / WIFI_PASS / WEBUI_USER / WEBUI_PASS / OTA_PUBKEY
// with values from your own secrets.h before building.

#include <cstring>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "web/server.h"
#include "web/status_core.h"
#include "web/system_routes.h"
#include "web/ota.h"

namespace {

constexpr const char* WIFI_SSID = "your-ssid";
constexpr const char* WIFI_PASS = "your-password";
constexpr const char* WEBUI_USER = "admin";
constexpr const char* WEBUI_PASS = "change-me";
constexpr const char* OTA_PUBKEY =
    "0000000000000000000000000000000000000000000000000000000000000000";

constexpr const char* TAG = "minimal";

// EMBED_TXTFILES'd panels — see CMakeLists.txt.
extern const char panels_html_start[] asm("_binary_panels_html_start");
extern const char panels_html_end[]   asm("_binary_panels_html_end");
extern const char panels_js_start[]   asm("_binary_panels_js_start");
extern const char panels_js_end[]     asm("_binary_panels_js_end");

esp_err_t handle_panels_html(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, panels_html_start, panels_html_end - panels_html_start - 1);
    return ESP_OK;
}
esp_err_t handle_panels_js(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, panels_js_start, panels_js_end - panels_js_start - 1);
    return ESP_OK;
}

// One project-specific REST endpoint, here just to demonstrate the pattern.
esp_err_t handle_hello(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, R"({"hello":"world"})");
    return ESP_OK;
}

void wifi_init_sta() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wcfg{};
    std::strncpy(reinterpret_cast<char*>(wcfg.sta.ssid), WIFI_SSID, sizeof(wcfg.sta.ssid));
    std::strncpy(reinterpret_cast<char*>(wcfg.sta.password), WIFI_PASS, sizeof(wcfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

void start_web_ui() {
    webui::Config cfg;
    cfg.user = WEBUI_USER;
    cfg.pass = WEBUI_PASS;
    auto* server = webui::start(cfg);
    if (!server) { ESP_LOGE(TAG, "web::start failed"); return; }

    webui::register_shell_assets(server);
    webui::status_core::register_routes(server);
    webui::status_core::set_provider([](cJSON* j) {
        cJSON_AddStringToObject(j, "deployment", "minimal-example");
    });
    webui::system_routes::register_routes(server);

    webui::ota::Config ota_cfg{};
    if (webui::ota::parse_pubkey_hex(OTA_PUBKEY, ota_cfg.pubkey)) {
        webui::ota::register_routes(server, ota_cfg);
    }

    webui::register_route(server, HTTP_GET, "/panels.html", handle_panels_html);
    webui::register_route(server, HTTP_GET, "/panels.js",   handle_panels_js);
    webui::register_route(server, HTTP_GET, "/api/hello",   handle_hello);

    ESP_LOGI(TAG, "web UI up");
}

void ota_probe_task(void*) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_VALID;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "OTA slot marked valid");
    }
    vTaskDelete(nullptr);
}

}  // namespace

extern "C" void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_sta();

    // Wait until Wi-Fi connects (cheap busy-loop for the example;
    // production code uses an event group).
    while (true) {
        esp_netif_ip_info_t ip{};
        if (auto* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            nif && esp_netif_get_ip_info(nif, &ip) == ESP_OK && ip.ip.addr) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    start_web_ui();
    xTaskCreate(ota_probe_task, "ota_probe", 4096, nullptr, 4, nullptr);
}
