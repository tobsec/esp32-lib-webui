#include "web/server.h"

#include <cstring>
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
    bool           is_public;  // skip auth check
};

std::vector<Route> g_routes;

esp_err_t auth_dispatcher(httpd_req_t* req) {
    // Match against the path portion only — the query string lives in
    // req->uri but routes are registered without one, so a request to
    // `/api/reader/ota/upload?reader=0` would otherwise miss its
    // registered `/api/reader/ota/upload` route. Handlers that care
    // about the query parse it themselves via `httpd_req_get_url_query_str`.
    const char* uri = req->uri;
    size_t path_len = 0;
    while (uri[path_len] && uri[path_len] != '?') ++path_len;

    for (const auto& r : g_routes) {
        if (r.method != req->method) continue;
        if (r.uri.size() != path_len) continue;
        if (std::memcmp(r.uri.data(), uri, path_len) != 0) continue;
        // Auth gate runs only for non-public routes. Public routes
        // (e.g. /api/setup-password) must be reachable before the user
        // has provisioned credentials.
        if (!r.is_public && auth::check_basic(req) != ESP_OK) return ESP_FAIL;
        // Forward the registered ctx so handlers can find their state.
        // Note: we replace user_ctx for the duration of the call, but
        // the handler is the only consumer so this is safe.
        req->user_ctx = r.ctx;
        return r.handler(req);
    }
    httpd_resp_send_404(req);
    return ESP_OK;
}

esp_err_t register_route_impl(httpd_handle_t server,
                              httpd_method_t method,
                              const char*    uri,
                              esp_err_t (*handler)(httpd_req_t*),
                              void*          ctx,
                              bool           is_public) {
    if (!server) return ESP_ERR_INVALID_STATE;

    g_routes.push_back({method, uri, handler, ctx, is_public});

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
    hcfg.server_port      = cfg.port;
    hcfg.stack_size       = cfg.http_stack_size;
    hcfg.max_uri_handlers = cfg.max_uri_handlers;
    hcfg.lru_purge_enable = true;     // free oldest socket when full
    hcfg.uri_match_fn     = httpd_uri_match_wildcard;
    // Default per-recv timeout is 5 s, but the reader-OTA forwarder's
    // feed() may block on its 32 KB stream buffer for several seconds
    // while the reader drains a burst. During that pause the handler
    // isn't pulling from the TCP socket; if the client's TCP window
    // closes and the next send arrives slowly, the recv() that comes
    // after feed() returns can sit idle long enough to trip the 5 s
    // cap and abort an otherwise-healthy upload. Bumping to 30 s
    // covers worst-case stream-buffer drain + brief WiFi blips without
    // letting a truly dead client hang the handler indefinitely.
    hcfg.recv_wait_timeout = 30;
    hcfg.send_wait_timeout = 30;

    if (httpd_start(&g_server, &hcfg) != ESP_OK) {
        ESP_LOGE(kTag, "httpd_start failed");
        g_server = nullptr;
        return nullptr;
    }
    ESP_LOGI(kTag, "started on port %u (stack %u, realm=%s)",
             cfg.port, static_cast<unsigned>(cfg.http_stack_size), cfg.realm.c_str());
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
    return register_route_impl(server, method, uri, handler, ctx, false);
}

esp_err_t register_public_route(httpd_handle_t       server,
                                httpd_method_t       method,
                                const char*          uri,
                                esp_err_t (*handler)(httpd_req_t*),
                                void*                ctx) {
    return register_route_impl(server, method, uri, handler, ctx, true);
}

}  // namespace webui
