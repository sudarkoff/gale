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
    .hrMax = 180,
    .hrResting = 60,

    // HR Zone defaults
    // Turn-on threshold: 30-35% HRR marks transition from rest to light exercise where 
    // metabolic heat production becomes noticeable. Below this, the body handles heat through 
    // passive dissipation; above it, active cooling begins to help.
    //
    // Low speed: Remains in HRR calculation (personalized) for light to early-moderate intensity.
    // Medium/High: Switch to %Max HR using ACSM guidelines - 64-76% Max HR is moderate intensity 
    // (active sweating), 76%+ is vigorous (heavy heat production). These standardized zones align 
    // fan speed with thermoregulatory demand as exercise intensity increases.
    .zone1Percent = 0.33f, // %% of HR Reserve (light intensity, minimal heat production)
    .zone2Percent = 0.64f, // %% of Max HR (moderate intensity, active sweating)
    .zone3Percent = 0.76f, // %% of Max HR (vigorous intensity, heavy heat production)

    // Fan behavior defaults
    .alwaysOn = 0,  // Fan off by default, turns on when HRM connects
#ifdef CONFIG_DEBUG_MODE
    .fanDelay = 10000,     // 10 seconds in debug
    .hrHysteresis = 0,     // none in debug
#else
    .fanDelay = 60000,     // 1 minute
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

// Matter override mode (true = Matter controls fan, false = HRM auto mode)
bool g_matter_override = false;

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

    // Wait for Matter BLE stack to stabilize before initializing HRM
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Initialize BLE HRM client (NimBLE is already initialized by Matter)
    ble_hrm_init();

    // If already commissioned, start HRM scanning after a brief delay
    if (matter_device_is_commissioned()) {
        ESP_LOGI(TAG, "Already commissioned, starting HRM scan");
        vTaskDelay(pdMS_TO_TICKS(1000));
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
        // If just commissioned, start HRM scanning after BLE settles
        if (!hrm_scan_started && matter_device_is_commissioned()) {
            ESP_LOGI(TAG, "Commissioning detected, waiting for BLE to settle...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            ESP_LOGI(TAG, "Starting HRM scan");
            ble_hrm_start_scan();
            hrm_scan_started = true;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
