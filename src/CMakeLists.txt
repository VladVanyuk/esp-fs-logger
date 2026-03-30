idf_component_register(
    SRCS
        "app_state.cpp"
        "log_storage.cpp"
        "main.cpp"
        "network.cpp"
        "system_init.cpp"
        "uart_service.cpp"
        "web_server.cpp"
    INCLUDE_DIRS "../include"
    REQUIRES
        esp_driver_uart
        esp_event
        esp_http_server
        esp_littlefs
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
)

littlefs_create_partition_image(littlefs ../spiffs_data FLASH_IN_PROJECT)
