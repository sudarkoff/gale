idf_component_register(SRCS "main.c"
                             "ble_hrm.c"
                             "wifi_manager.c"
                             "web_server.c"
                             "nvs_config.c"
                             "fan_control.c"
                             "ota_update.c"
                    INCLUDE_DIRS "."
                    REQUIRES mdns nvs_flash esp_wifi esp_http_server driver bt app_update esp_driver_gpio esp_https_ota)
