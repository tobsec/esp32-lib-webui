# esp32-lib-webui — embedded web UI / admin / OTA stack for ESP-IDF.
#
# Consumed as an ESP-IDF managed component (via idf_component.yml in the
# parent project) or as a git submodule referenced through
# EXTRA_COMPONENT_DIRS. Either way the directory layout is the same and
# this CMakeLists is the only entry point.

idf_component_register(
    SRCS
        "src/web/assets.cpp"
        "src/web/auth.cpp"
        "src/web/ota.cpp"
        "src/web/server.cpp"
        "src/web/status_core.cpp"
        "src/web/system_routes.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_server
        esp_app_format
        esp_timer
        app_update
        spi_flash
        mbedtls
        espressif__cjson
        espressif__libsodium
    EMBED_TXTFILES
        "assets/shell.html"
        "assets/shell.css"
        "assets/shell.js"
        "assets/webui.css"
        "assets/webui.js"
        "assets/themes/dark.css"
        "assets/themes/light.css"
        "assets/vendor/pico/pico.min.css"
        "assets/vendor/alpine/alpine.min.js"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
