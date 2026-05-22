idf_component_register(
    SRCS
        "app_main.cpp"
        "avoid_control.cpp"
        "speed_drive.cpp"
        "trace_control.cpp"
        "web_handler.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_event
        esp_http_server
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
)
