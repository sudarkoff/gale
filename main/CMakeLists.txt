idf_component_register(SRCS "main.c"
                             "ble_hrm_nimble.c"
                             "nvs_config.c"
                             "fan_control.c"
                             "led_control.c"
                             "matter_device.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES nvs_flash driver bt esp_driver_gpio esp_driver_ledc
                                  esp_matter)
