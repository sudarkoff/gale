#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "gale.h"
#include "matter_device.h"

static const char *TAG = "FAN_CONTROL";

void fan_control_init(void)
{
    ESP_LOGI(TAG, "Initializing fan control");

    // Configure relay GPIO pins as outputs
    for (int i = 0; i < NUM_RELAYS; i++) {
        gpio_reset_pin(g_config.relayGPIO[i]);
        gpio_set_direction(g_config.relayGPIO[i], GPIO_MODE_OUTPUT);
        gpio_set_level(g_config.relayGPIO[i], RELAY_OFF);
    }

    ESP_LOGI(TAG, "Fan control initialized");
}

void fan_control_set_speed(uint8_t fanSpeed)
{
    if (fanSpeed == g_prev_speed) {
        return;
    }

    uint32_t currentTime = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // If the speed is going up—change it right away
    // or wait fanDelay ms before lowering it
    if ((fanSpeed > g_prev_speed) || (currentTime - g_speed_changed_time) > g_config.fanDelay) {
        for (int i = 0; i < NUM_RELAYS; i++) {
            gpio_set_level(g_config.relayGPIO[i],
                          (i == fanSpeed - 1) ? RELAY_ON : RELAY_OFF);
        }
        g_prev_speed = fanSpeed;
        ESP_LOGI(TAG, "Fan speed set to %d", fanSpeed);

        // Update Matter state
        matter_device_update_fan_state(fanSpeed);

        // Update LED pulsing mode (only if BLE connected)
        if (g_ble_connected) {
            led_control_set_mode(fanSpeed);
        }
    }
}

void fan_control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Fan control task started");

    while (1) {
        // The fan is on, but we're no longer connected to HRM
        if (!g_ble_connected && g_current_speed > 0) {
            uint32_t currentTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((currentTime - g_disconnected_time) > g_config.fanDelay) {
                // It's been long enough, giving up on HRM reconnecting and turning off the fan
                ESP_LOGI(TAG, "HRM disconnected timeout, setting speed to %d", g_config.alwaysOn);
                g_current_speed = g_config.alwaysOn;
            }
        }

        fan_control_set_speed(g_current_speed);

        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
    }
}
