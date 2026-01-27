#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "gale.h"
#include "matter_device.h"

static const char *TAG = "GALE";

// Global configuration with defaults
// These can be modified via NVS or later through Matter
config_t g_config = {
    // Heart rate defaults
    .hrMax = 200,
    .hrResting = 50,

    // Zone defaults
    .zone1Percent = 0.4f,  // 40% of HR reserve
    .zone2Percent = 0.7f,  // 70% of max HR
    .zone3Percent = 0.8f,  // 80% of max HR

    // Fan behavior defaults
    .alwaysOn = 1,  // Fan off by default, turns on when HRM connects
#ifdef CONFIG_DEBUG_MODE
    .fanDelay = 10000,     // 10 seconds in debug
    .hrHysteresis = 0,     // none in debug
#else
    .fanDelay = 120000,    // 2 minutes
    .hrHysteresis = 15,
#endif

    // GPIO defaults
    .relayGPIO = {27, 26, 25},
    .ledGPIO = 2
};

// Calculated zone thresholds
float g_zone1, g_zone2, g_zone3;

// Fan speed state
uint8_t g_current_speed = 1;  // Will be set from config.alwaysOn in setup
uint8_t g_prev_speed = 0;
uint32_t g_speed_changed_time = 0;

// BLE connection state
bool g_ble_connected = false;
uint32_t g_disconnected_time = 0;

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Gale - Heart Rate Controlled Fan with Matter");

    // Initialize NVS (required before Matter)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Load configuration from NVS
    nvs_config_load();

    // Initialize fan control (GPIO setup)
    fan_control_init();

    // Initialize LED control (PWM for pulsing)
    led_control_init();

    // Set initial fan speed
    g_current_speed = g_config.alwaysOn;

    // Initialize Matter device (creates fan endpoint and starts Matter stack)
    err = matter_device_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Matter device");
        return;
    }

    // Initialize BLE HRM client (NimBLE is already initialized by Matter)
    ble_hrm_init();

    // If already commissioned, start HRM scanning immediately
    if (matter_device_is_commissioned()) {
        ESP_LOGI(TAG, "Already commissioned, starting HRM scan");
        ble_hrm_start_scan();
    } else {
        ESP_LOGI(TAG, "Not commissioned, waiting for Matter commissioning...");
        ESP_LOGI(TAG, "HRM scanning will start after commissioning completes");
    }

    // Create fan control task
    xTaskCreate(fan_control_task, "fan_control", 4096, NULL, 5, NULL);

    // Create LED control task
    xTaskCreate(led_control_task, "led_control", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Gale initialized successfully with Matter support");
    ESP_LOGI(TAG, "HR Max: %d, Resting: %d", g_config.hrMax, g_config.hrResting);
    ESP_LOGI(TAG, "Zone 1: %.1f, Zone 2: %.1f, Zone 3: %.1f", g_zone1, g_zone2, g_zone3);
    ESP_LOGI(TAG, "Fan delay: %" PRIu32 " ms, Hysteresis: %d BPM, Always on: %d",
             g_config.fanDelay, g_config.hrHysteresis, g_config.alwaysOn);

    // Main loop - check for commissioning completion and start HRM scan
    bool hrm_scan_started = matter_device_is_commissioned();
    while (1) {
        // If just commissioned, start HRM scanning
        if (!hrm_scan_started && matter_device_is_commissioned()) {
            ESP_LOGI(TAG, "Commissioning detected, starting HRM scan");
            ble_hrm_start_scan();
            hrm_scan_started = true;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
