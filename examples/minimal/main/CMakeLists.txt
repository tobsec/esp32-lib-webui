idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp32-lib-webui
    EMBED_TXTFILES
        "web_assets/panels.html"
        "web_assets/panels.js"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
