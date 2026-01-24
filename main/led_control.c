#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "gale.h"

static const char *TAG = "LED_CONTROL";

#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY      5000
#define LEDC_MAX_DUTY       ((1 << 13) - 1)  // 8191 for 13-bit resolution

// Pulse periods in milliseconds (full cycle: fade in + fade out)
#define PULSE_PERIOD_SPEED1  3000  // 3 seconds - slowest
#define PULSE_PERIOD_SPEED2  1500  // 1.5 seconds - medium
#define PULSE_PERIOD_SPEED3  750   // 0.75 seconds - fastest

static bool led_initialized = false;
static uint8_t current_led_mode = 0;  // 0=off, 1/2/3=pulsing at different speeds

void led_control_init(void)
{
    // Configure LEDC timer
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    // Configure LEDC channel
    ledc_channel_config_t channel_config = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = g_config.ledGPIO,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

    // Install fade service
    ESP_ERROR_CHECK(ledc_fade_func_install(0));

    led_initialized = true;
    ESP_LOGI(TAG, "LED control initialized on GPIO %d", g_config.ledGPIO);
}

void led_control_off(void)
{
    if (!led_initialized) return;

    current_led_mode = 0;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void led_control_on(void)
{
    if (!led_initialized) return;

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_MAX_DUTY);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void led_control_set_mode(uint8_t mode)
{
    current_led_mode = mode;
}

void led_control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "LED control task started");

    uint8_t last_mode = 0;
    bool fading_up = true;

    while (1) {
        uint8_t mode = current_led_mode;

        if (mode == 0) {
            // LED off
            if (last_mode != 0) {
                ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
                ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            }
            last_mode = 0;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Determine pulse period based on mode
        uint32_t pulse_period;
        switch (mode) {
            case 1:  pulse_period = PULSE_PERIOD_SPEED1; break;
            case 2:  pulse_period = PULSE_PERIOD_SPEED2; break;
            case 3:  pulse_period = PULSE_PERIOD_SPEED3; break;
            default: pulse_period = PULSE_PERIOD_SPEED1; break;
        }

        uint32_t fade_time = pulse_period / 2;  // Half period for fade in, half for fade out

        if (fading_up) {
            // Fade from 0 to max
            ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL, LEDC_MAX_DUTY, fade_time);
            ledc_fade_start(LEDC_MODE, LEDC_CHANNEL, LEDC_FADE_WAIT_DONE);
            fading_up = false;
        } else {
            // Fade from max to 0
            ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL, 0, fade_time);
            ledc_fade_start(LEDC_MODE, LEDC_CHANNEL, LEDC_FADE_WAIT_DONE);
            fading_up = true;
        }

        last_mode = mode;
    }
}
