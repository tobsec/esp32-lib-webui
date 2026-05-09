#include "web/ota.h"

#include <cstdio>
#include <cstring>

#include <esp_app_format.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sodium/crypto_sign.h>

#include "web/server.h"

namespace reader_core::web::ota {

namespace {

constexpr const char* kTag    = "ota";
constexpr size_t      kSigLen = 64;
constexpr size_t      kChunk  = 4096;

Config g_config;

const char* result_str_impl(Result r) {
    switch (r) {
        case Result::Ok:           return "ok";
        case Result::TooLarge:     return "too_large";
        case Result::TooSmall:     return "too_small";
        case Result::NoPartition:  return "no_partition";
        case Result::OtaBegin:     return "ota_begin";
        case Result::Recv:         return "recv";
        case Result::Write:        return "write";
        case Result::OtaEnd:       return "ota_end";
        case Result::Mmap:         return "mmap";
        case Result::Verify:       return "verify";
        case Result::SetBoot:      return "set_boot";
    }
    return "?";
}

// Reads the partition body back into a libsodium-friendly contiguous
// pointer via esp_partition_mmap, runs Ed25519 verify, unmaps. Returns
// true on signature match.
bool verify_image(const esp_partition_t* part, size_t image_len,
                  const uint8_t sig[kSigLen], const uint8_t pubkey[32]) {
    const void* mapped = nullptr;
    esp_partition_mmap_handle_t mh{};
    esp_err_t err = esp_partition_mmap(part, 0, image_len,
                                       ESP_PARTITION_MMAP_DATA, &mapped, &mh);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_partition_mmap failed: %s", esp_err_to_name(err));
        return false;
    }
    const int rc = crypto_sign_verify_detached(
        sig,
        static_cast<const unsigned char*>(mapped),
        image_len,
        pubkey);
    esp_partition_munmap(mh);
    return rc == 0;
}

void deferred_reboot_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGW(kTag, "rebooting into freshly-flashed OTA slot");
    esp_restart();
}

esp_err_t handle_upload(httpd_req_t* req) {
    size_t   img_size = 0;
    Result   r        = install_signed(req, g_config, &img_size);

    if (r == Result::Ok) {
        char body[128];
        std::snprintf(body, sizeof(body),
                      R"({"status":"ok","image_size":%u,"reboot_in_s":2})",
                      static_cast<unsigned>(img_size));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, body);
        // Defer reboot so the response flushes first.
        xTaskCreate(deferred_reboot_task, "ota_reboot", 4096, nullptr, 5, nullptr);
        return ESP_OK;
    }

    const char* http_status = "400 Bad Request";
    if (r == Result::TooLarge)      http_status = "413 Payload Too Large";
    if (r == Result::Verify)        http_status = "401 Unauthorized";  // signature didn't match
    if (r == Result::NoPartition ||
        r == Result::OtaBegin    ||
        r == Result::Write       ||
        r == Result::OtaEnd      ||
        r == Result::Mmap        ||
        r == Result::SetBoot)       http_status = "500 Internal Server Error";

    char body[128];
    std::snprintf(body, sizeof(body),
                  R"({"status":"err","reason":"%s"})", result_str_impl(r));
    httpd_resp_set_status(req, http_status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, body);
    return ESP_OK;
}

}  // namespace

const char* result_str(Result r) { return result_str_impl(r); }

bool parse_pubkey_hex(const char* hex, uint8_t out[32]) {
    if (!hex) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (std::strlen(hex) != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
        const int hi = nibble(hex[i * 2]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

Result install_signed(httpd_req_t* req, const Config& cfg, size_t* image_size_out) {
    if (req->content_len > cfg.max_size)            return Result::TooLarge;
    if (req->content_len < kSigLen + 1024)          return Result::TooSmall;

    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    if (!next) return Result::NoPartition;

    ESP_LOGI(kTag, "starting OTA into '%s' (size budget %u, body %u)",
             next->label, static_cast<unsigned>(cfg.max_size),
             static_cast<unsigned>(req->content_len));

    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(next, OTA_SIZE_UNKNOWN, &handle) != ESP_OK) {
        return Result::OtaBegin;
    }

    // Streaming pattern: keep a 64-byte rolling tail. Whatever stays in
    // the tail at end-of-stream is the signature; everything that
    // shifted out gets written to flash.
    uint8_t tail[kSigLen];
    size_t  tail_used  = 0;
    size_t  written    = 0;
    size_t  total_recv = 0;

    auto* buf = static_cast<uint8_t*>(heap_caps_malloc(kChunk + kSigLen, MALLOC_CAP_8BIT));
    if (!buf) {
        esp_ota_abort(handle);
        return Result::Write;
    }

    while (true) {
        const int n = httpd_req_recv(req,
                                     reinterpret_cast<char*>(buf + tail_used),
                                     kChunk);
        if (n < 0) { heap_caps_free(buf); esp_ota_abort(handle); return Result::Recv; }
        if (n == 0) break;
        total_recv += static_cast<size_t>(n);

        // buf[0..tail_used) holds the previous tail; buf[tail_used..tail_used+n)
        // holds the freshly-received chunk. We can write everything except
        // the last kSigLen bytes; whatever's left over becomes the new tail.
        const size_t combined = tail_used + static_cast<size_t>(n);
        if (combined < kSigLen) {
            // Not even enough data yet to hold a signature window — keep
            // accumulating. (We pre-validated content_len ≥ kSigLen+1024
            // so this branch is rare and harmless.)
            tail_used = combined;
            continue;
        }
        const size_t to_write = combined - kSigLen;
        if (esp_ota_write(handle, buf, to_write) != ESP_OK) {
            heap_caps_free(buf);
            esp_ota_abort(handle);
            return Result::Write;
        }
        written += to_write;

        // Slide the last kSigLen bytes back to the front for the next loop.
        std::memmove(tail, buf + to_write, kSigLen);
        std::memcpy(buf, tail, kSigLen);
        tail_used = kSigLen;
    }

    if (tail_used != kSigLen) {
        heap_caps_free(buf);
        esp_ota_abort(handle);
        return Result::TooSmall;
    }
    std::memcpy(tail, buf, kSigLen);
    heap_caps_free(buf);

    if (esp_ota_end(handle) != ESP_OK) {
        return Result::OtaEnd;
    }

    ESP_LOGI(kTag, "received %u B (image %u + sig 64); verifying",
             static_cast<unsigned>(total_recv),
             static_cast<unsigned>(written));

    if (!verify_image(next, written, tail, cfg.pubkey)) {
        ESP_LOGE(kTag, "signature verify failed — staged image will not boot");
        // Don't set boot partition; the bootloader keeps running the old slot.
        return Result::Verify;
    }

    if (esp_ota_set_boot_partition(next) != ESP_OK) {
        return Result::SetBoot;
    }

    if (image_size_out) *image_size_out = written;
    ESP_LOGI(kTag, "OTA verified + staged; boot partition flipped to '%s'", next->label);
    return Result::Ok;
}

esp_err_t register_routes(httpd_handle_t server, const Config& cfg) {
    g_config = cfg;
    return register_route(server, HTTP_POST, "/api/ota/upload", handle_upload);
}

}  // namespace reader_core::web::ota
