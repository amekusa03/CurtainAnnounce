idf_component_register(
    SRCS
        "main.c"
        "wifi_manager.c"
        "ntp_client.c"
        "spiffs_storage.c"
        "http_server.c"
        "data_fetcher.c"
        "thermal_logic.c"
        "cast_client.c"
        "announce_history.c"
        "web_reporter.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        esp_http_client
        spiffs
        esp-tls
        mbedtls
        lwip
        json
        mdns
        esp_driver_gpio
)


