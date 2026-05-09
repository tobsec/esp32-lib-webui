#include "web/server.h"

#include <esp_log.h>

#include <vector>

#include "web/auth.h"

namespace webui {

namespace {

constexpr const char* kTag = "web_server";

httpd_handle_t g_server = nullptr;
Config         g_config;

// One entry per registered route. We can't capture the user's handler
// in a closure (esp_http_server only accepts a plain function pointer),
// so we install a tiny static dispatcher and do the lookup ourselves.
// Lookup is O(N) but N is small (~10) and we're not on the hot path.
struct Route {
    httpd_method_t method;
    std::string    uri;
    esp_err_t (*handler)(httpd_req_t*);
    void*          ctx;
};

std::vector<Route> g_routes;

esp_err_t auth_dispatcher(httpd_req_t* req) {
    if (auth::check_basic(req) != ESP_OK) return ESP_FAIL;

    for (const auto& r : g_routes) {
        if (r.method == req->method && r.uri == req->uri) {
            // Forward the registered ctx so handlers can find their state.
            // Note: we replace user_ctx for the duration of the call, but
            // the handler is the only consumer so this is safe.
            req->user_ctx = r.ctx;
            return r.handler(req);
        }
    }
    httpd_resp_send_404(req);
    return ESP_OK;
}

}  // namespace

const Config& config() { return g_config; }

httpd_handle_t start(const Config& cfg) {
    if (g_server) {
        ESP_LOGW(kTag, "start() called twice — returning existing handle");
        return g_server;
    }
    g_config = cfg;
    g_routes.clear();

    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.server_port    = cfg.port;
    hcfg.max_uri_handlers = 16;       // covers shared + project routes
    hcfg.lru_purge_enable = true;     // free oldest socket when full
    hcfg.uri_match_fn   = httpd_uri_match_wildcard;

    if (httpd_start(&g_server, &hcfg) != ESP_OK) {
        ESP_LOGE(kTag, "httpd_start failed");
        g_server = nullptr;
        return nullptr;
    }
    ESP_LOGI(kTag, "started on port %u (realm=%s)", cfg.port, cfg.realm.c_str());
    return g_server;
}

void stop() {
    if (g_server) {
        httpd_stop(g_server);
        g_server = nullptr;
    }
    g_routes.clear();
}

esp_err_t register_route(httpd_handle_t       server,
                         httpd_method_t       method,
                         const char*          uri,
                         esp_err_t (*handler)(httpd_req_t*),
                         void*                ctx) {
    if (!server) return ESP_ERR_INVALID_STATE;

    g_routes.push_back({method, uri, handler, ctx});

    httpd_uri_t entry{};
    entry.uri      = uri;
    entry.method   = method;
    entry.handler  = auth_dispatcher;
    entry.user_ctx = nullptr;

    esp_err_t err = httpd_register_uri_handler(server, &entry);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "register %s %s failed: %s", uri,
                 method == HTTP_GET ? "GET" : "(other)",
                 esp_err_to_name(err));
        g_routes.pop_back();
    }
    return err;
}

}  // namespace webui
