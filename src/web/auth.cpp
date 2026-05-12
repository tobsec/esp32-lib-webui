#include "web/auth.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mbedtls/base64.h>

#include <cstring>
#include <string>

#include "web/server.h"

namespace webui::auth {

namespace {

constexpr const char* kTag = "web_auth";

// Active credentials. Seeded from Config at start() time, mutated at
// runtime via set_active_credentials() once a user sets / changes their
// password via /api/setup-password or the controller console.
struct ActiveCreds {
    std::string       user;
    std::string       pass;
    SemaphoreHandle_t lock;
};
ActiveCreds s_active = {};

void ensure_lock() {
    if (!s_active.lock) s_active.lock = xSemaphoreCreateMutex();
}

void seed_from_config_if_unset() {
    // First call after webui::start(): pick up whatever start() stashed
    // in the Config. set_active_credentials() may overwrite later.
    ensure_lock();
    if (xSemaphoreTake(s_active.lock, portMAX_DELAY) == pdTRUE) {
        if (s_active.user.empty() && s_active.pass.empty()) {
            const auto& cfg = config();
            s_active.user = cfg.user;
            s_active.pass = cfg.pass;
        }
        xSemaphoreGive(s_active.lock);
    }
}

// Constant-time string compare. Avoids leaking the prefix length of
// the secret on a timing oracle. Returns true on equal length+bytes.
bool ct_equal(const char* a, size_t a_len, const char* b, size_t b_len) {
    if (a_len != b_len) return false;
    unsigned diff = 0;
    for (size_t i = 0; i < a_len; ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

esp_err_t send_401(httpd_req_t* req) {
    const auto& cfg = config();
    const std::string challenge = "Basic realm=\"" + cfg.realm + "\"";
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", challenge.c_str());
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "auth required\n");
    return ESP_FAIL;
}

}  // namespace

void set_active_credentials(const std::string& user, const std::string& pass) {
    ensure_lock();
    if (xSemaphoreTake(s_active.lock, portMAX_DELAY) == pdTRUE) {
        s_active.user = user;
        s_active.pass = pass;
        xSemaphoreGive(s_active.lock);
    }
    ESP_LOGI(kTag, "active credentials updated (secured=%s)",
             (!user.empty() && !pass.empty()) ? "yes" : "no");
}

bool is_secured() {
    seed_from_config_if_unset();
    bool secured = false;
    if (xSemaphoreTake(s_active.lock, portMAX_DELAY) == pdTRUE) {
        secured = !s_active.user.empty() && !s_active.pass.empty();
        xSemaphoreGive(s_active.lock);
    }
    return secured;
}

esp_err_t check_basic(httpd_req_t* req) {
    seed_from_config_if_unset();

    // Snapshot active credentials under the lock so we don't read
    // half-updated state while compare runs.
    std::string user, pass;
    if (xSemaphoreTake(s_active.lock, portMAX_DELAY) == pdTRUE) {
        user = s_active.user;
        pass = s_active.pass;
        xSemaphoreGive(s_active.lock);
    }

    // Open-until-secured: when no password is set, every route is
    // freely reachable. Controller-side glue is expected to render an
    // in-UI banner directing the user to /api/setup-password. Once
    // they set credentials we switch into the standard Basic-Auth
    // path below.
    if (user.empty() || pass.empty()) {
        return ESP_OK;
    }

    const size_t hdr_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (hdr_len == 0) return send_401(req);

    // "Basic " + base64(user:pass). Generous headroom; sdkconfig caps
    // headers at 1 KB, see CONFIG_HTTPD_MAX_REQ_HDR_LEN.
    char hdr[512];
    if (hdr_len + 1 > sizeof(hdr)) return send_401(req);
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
        return send_401(req);
    }

    constexpr const char* kPrefix = "Basic ";
    constexpr size_t      kPrefixLen = 6;
    if (std::strncmp(hdr, kPrefix, kPrefixLen) != 0) return send_401(req);

    // Decode the base64 chunk after the prefix.
    unsigned char decoded[256];
    size_t decoded_len = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_len,
                              reinterpret_cast<const unsigned char*>(hdr + kPrefixLen),
                              std::strlen(hdr + kPrefixLen)) != 0) {
        return send_401(req);
    }

    // Split on ':'. Any further colon-bytes belong to the password.
    const char* colon = static_cast<const char*>(
        std::memchr(decoded, ':', decoded_len));
    if (!colon) return send_401(req);
    const size_t user_len = colon - reinterpret_cast<const char*>(decoded);
    const char*  pass_ptr = colon + 1;
    const size_t pass_len = decoded_len - user_len - 1;

    if (!ct_equal(reinterpret_cast<const char*>(decoded), user_len,
                  user.data(), user.size()) ||
        !ct_equal(pass_ptr, pass_len, pass.data(), pass.size())) {
        return send_401(req);
    }
    return ESP_OK;
}

}  // namespace webui::auth
