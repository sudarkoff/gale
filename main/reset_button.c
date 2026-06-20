#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "gale.h"
#include "matter_device.h"

static const char *TAG = "RESET_BUTTON";

#define BUTTON_POLL_MS          50
#define BUTTON_DEBOUNCE_COUNT   3
#define RESET_HOLD_TIME_MS      5000
#define RESET_PENDING_MODE      4

void reset_button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << RESET_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (RESET_BUTTON_ACTIVE_LEVEL == 0),
        .pull_down_en = (RESET_BUTTON_ACTIVE_LEVEL != 0),
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_LOGI(TAG, "Reset button initialized on GPIO %d", RESET_BUTTON_GPIO);
}

void reset_button_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Reset button task started");

    bool stable_pressed = false;
    bool reset_armed = false;
    uint8_t debounce_count = 0;
    TickType_t press_start_ticks = 0;

    while (1) {
        int level = gpio_get_level((gpio_num_t) RESET_BUTTON_GPIO);
        bool raw_pressed = (level == RESET_BUTTON_ACTIVE_LEVEL);

        if (raw_pressed == stable_pressed) {
            debounce_count = 0;
        } else if (++debounce_count >= BUTTON_DEBOUNCE_COUNT) {
            stable_pressed = raw_pressed;
            debounce_count = 0;

            if (stable_pressed) {
                press_start_ticks = xTaskGetTickCount();
                reset_armed = false;
                ESP_LOGI(TAG, "Reset button pressed, hold for %d ms to factory reset Matter",
                         RESET_HOLD_TIME_MS);
            } else {
                if (!reset_armed) {
                    led_control_set_mode(g_ble_connected ? g_current_speed : 0);
                }
                ESP_LOGI(TAG, "Reset button released");
            }
        }

        if (stable_pressed && !reset_armed && !g_reset_in_progress) {
            TickType_t held_ticks = xTaskGetTickCount() - press_start_ticks;
            uint32_t held_ms = held_ticks * portTICK_PERIOD_MS;

            if (held_ms >= RESET_HOLD_TIME_MS) {
                reset_armed = true;
                ESP_LOGW(TAG, "Reset button hold complete, resetting Matter state");
                led_control_set_mode(RESET_PENDING_MODE);
                matter_device_factory_reset();
            } else if (held_ms >= 1000) {
                led_control_set_mode(RESET_PENDING_MODE);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}
