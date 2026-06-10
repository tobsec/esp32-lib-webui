// Static-asset handlers for the shared web shell, vendored libraries,
// and the toolkit's CSS/JS + theme bundle.
//
// Files are embedded into the component via EMBED_TXTFILES in
// CMakeLists.txt; the linker exports each file's _binary_*_start /
// _binary_*_end symbols, and we hand the buffer (minus the trailing
// NUL terminator that EMBED_TXTFILES appends) to httpd_resp_send.

#include <esp_err.h>
#include <esp_http_server.h>

#include "web/server.h"

extern const char shell_html_start[]      asm("_binary_shell_html_start");
extern const char shell_html_end[]        asm("_binary_shell_html_end");
extern const char shell_css_start[]       asm("_binary_shell_css_start");
extern const char shell_css_end[]         asm("_binary_shell_css_end");
extern const char shell_js_start[]        asm("_binary_shell_js_start");
extern const char shell_js_end[]          asm("_binary_shell_js_end");
extern const char webui_css_start[]       asm("_binary_webui_css_start");
extern const char webui_css_end[]         asm("_binary_webui_css_end");
extern const char webui_js_start[]        asm("_binary_webui_js_start");
extern const char webui_js_end[]          asm("_binary_webui_js_end");
extern const char dark_css_start[]        asm("_binary_dark_css_start");
extern const char dark_css_end[]          asm("_binary_dark_css_end");
extern const char light_css_start[]       asm("_binary_light_css_start");
extern const char light_css_end[]         asm("_binary_light_css_end");
extern const char pico_min_css_start[]    asm("_binary_pico_min_css_start");
extern const char pico_min_css_end[]      asm("_binary_pico_min_css_end");
extern const char alpine_min_js_start[]   asm("_binary_alpine_min_js_start");
extern const char alpine_min_js_end[]     asm("_binary_alpine_min_js_end");

namespace webui {

namespace {

esp_err_t serve_blob(httpd_req_t* req, const char* type,
                     const char* start, const char* end) {
    // EMBED_TXTFILES appends a NUL terminator that lives between start
    // and end. Strict parsers (the JS engine, mostly) treat the NUL as
    // an invalid token, so trim it from the response length.
    httpd_resp_set_type(req, type);
    httpd_resp_send(req, start, end - start - 1);
    return ESP_OK;
}

esp_err_t serve_blob_cached(httpd_req_t* req, const char* type,
                            const char* start, const char* end) {
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400, immutable");
    return serve_blob(req, type, start, end);
}

esp_err_t handle_index(httpd_req_t* req)        { return serve_blob(req, "text/html",              shell_html_start,    shell_html_end);    }
esp_err_t handle_shell_css(httpd_req_t* req)    { return serve_blob(req, "text/css",               shell_css_start,     shell_css_end);     }
esp_err_t handle_shell_js(httpd_req_t* req)     { return serve_blob(req, "application/javascript", shell_js_start,      shell_js_end);      }
esp_err_t handle_webui_css(httpd_req_t* req)    { return serve_blob(req, "text/css",               webui_css_start,     webui_css_end);     }
esp_err_t handle_webui_js(httpd_req_t* req)     { return serve_blob(req, "application/javascript", webui_js_start,      webui_js_end);      }
esp_err_t handle_theme_dark(httpd_req_t* req)   { return serve_blob(req, "text/css",               dark_css_start,      dark_css_end);      }
esp_err_t handle_theme_light(httpd_req_t* req)  { return serve_blob(req, "text/css",               light_css_start,     light_css_end);     }
esp_err_t handle_pico(httpd_req_t* req)         { return serve_blob_cached(req, "text/css",               pico_min_css_start,  pico_min_css_end);  }
esp_err_t handle_alpine(httpd_req_t* req)       { return serve_blob_cached(req, "application/javascript", alpine_min_js_start, alpine_min_js_end); }

}  // namespace

esp_err_t register_shell_assets(httpd_handle_t server) {
    esp_err_t err;
    if ((err = register_route(server, HTTP_GET, "/",                     handle_index))       != ESP_OK) return err;
    if ((err = register_route(server, HTTP_GET, "/shell.css",            handle_shell_css))   != ESP_OK) return err;
    if ((err = register_route(server, HTTP_GET, "/shell.js",             handle_shell_js))    != ESP_OK) return err;
    if ((err = register_route(server, HTTP_GET, "/webui.css",            handle_webui_css))   != ESP_OK) return err;
    if ((err = register_route(server, HTTP_GET, "/webui.js",             handle_webui_js))    != ESP_OK) return err;
    if ((err = register_route(server, HTTP_GET, "/themes/dark.css",      handle_theme_dark))  != ESP_OK) return err;
    if ((err = register_route(server, HTTP_GET, "/themes/light.css",     handle_theme_light)) != ESP_OK) return err;
    if ((err = register_route(server, HTTP_GET, "/vendor/pico.min.css",  handle_pico))        != ESP_OK) return err;
    if ((err = register_route(server, HTTP_GET, "/vendor/alpine.min.js", handle_alpine))      != ESP_OK) return err;
    // /theme.css is intentionally not registered here — if the consumer
    // ships a project-specific theme they register the route themselves
    // (mirror of the panels.html / panels.js pattern). Browsers that
    // hit a 404 silently drop the <link> via the inline onerror in
    // shell.html, so no console noise either way.
    return ESP_OK;
}

}  // namespace webui
