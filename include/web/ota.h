#pragma once

// Signed-upload OTA. Project-agnostic — Standalone reader and (later)
// the controller share this code; they only differ in which
// verification public key they pass via Config.
//
// Image format expected on POST /api/ota/upload:
//
//   raw .bin || ed25519_signature(64 bytes)
//
// The body is the regular esp-idf .bin (factory-flashable as is); the
// signature is computed over the entire body and appended. tools/ota_sign.py
// produces this layout from a build/<project>.bin and a 32-byte
// signing key file.
//
// Verification happens AFTER the body is written to flash: we
// memory-map the OTA partition and pass the mapped pointer +
// recorded length to libsodium's crypto_sign_verify_detached. On
// success, esp_ota_set_boot_partition flips otadata; on failure, the
// staged image is left in place but otadata stays unchanged so the
// bootloader keeps running the old slot.

#include <cstddef>
#include <cstdint>

#include <esp_err.h>
#include <esp_http_server.h>

namespace reader_core::web::ota {

struct Config {
    // 32-byte ed25519 verification public key. Lives in secrets.h
    // (HK_OTA_SIGNING_PUBKEY) and is parsed once at boot.
    uint8_t pubkey[32];
    // Hard cap on accepted upload size. 4 MB matches our 3 MB OTA slot
    // with a comfortable margin. Bodies larger than this are rejected
    // before any flash write.
    size_t max_size = 4 * 1024 * 1024;
};

enum class Result {
    Ok,
    TooLarge,        // body > max_size
    TooSmall,        // body < signature trailer alone
    NoPartition,     // esp_ota_get_next_update_partition returned null
    OtaBegin,        // esp_ota_begin failed
    Recv,            // httpd_req_recv failed mid-stream
    Write,           // esp_ota_write failed
    OtaEnd,          // esp_ota_end failed (image header reject etc.)
    Mmap,            // esp_partition_mmap failed during verify
    Verify,          // ed25519 signature mismatch
    SetBoot,         // esp_ota_set_boot_partition failed
};

const char* result_str(Result r);

// Streams the request body into the next OTA slot, splitting off the
// trailing 64-byte signature, then verifies and (on success) flips the
// boot partition. The caller is responsible for sending the HTTP
// response — register_routes() does that for the default /api/ota/upload
// endpoint.
Result install_signed(httpd_req_t* req, const Config& cfg, size_t* image_size_out = nullptr);

// Registers POST /api/ota/upload on the supplied server. The Config is
// copied and held internally — callers don't need to keep it alive.
esp_err_t register_routes(httpd_handle_t server, const Config& cfg);

// Helper: parses a 64-character hex string into 32 raw bytes. Returns
// false if the string is malformed. Used by glue code to lift the
// HK_OTA_SIGNING_PUBKEY define from secrets.h into Config.pubkey.
bool parse_pubkey_hex(const char* hex, uint8_t out[32]);

}  // namespace reader_core::web::ota
