idf_component_register(SRCS "main.c"
                             "ble_hrm.c"
                             "nvs_config.c"
                             "fan_control.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash driver bt esp_driver_gpio)
