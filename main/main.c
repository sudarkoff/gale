#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "gale.h"

static const char *TAG = "GALE";

// Global configuration with defaults
config_t g_config = {
    // WiFi defaults
    .wifiSSID = "",
    .wifiPassword = "",
    .apSSID = "Gale",
    .apPassword = "gale1234",
    .useStationMode = false,

    // Heart rate defaults
    .hrMax = 200,
    .hrResting = 50,

    // Zone defaults
    .zone1Percent = 0.4f,  // 40% of HR reserve
    .zone2Percent = 0.7f,  // 70% of max HR
    .zone3Percent = 0.8f,  // 80% of max HR

    // Fan behavior defaults
    .alwaysOn = 1,
#ifdef CONFIG_DEBUG_MODE
    .fanDelay = 10000,     // 10 seconds in debug
    .hrHysteresis = 0,     // none in debug
#else
    .fanDelay = 120000,    // 2 minutes
    .hrHysteresis = 15,
#endif

    // GPIO defaults
    .relayGPIO = {25, 26, 27}
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
    ESP_LOGI(TAG, "Starting Gale - Heart Rate Controlled Fan");

    // Initialize NVS
    nvs_config_init();

    // Load configuration from NVS
    nvs_config_load();

    // Initialize fan control (GPIO setup)
    fan_control_init();

    // Set initial fan speed
    g_current_speed = g_config.alwaysOn;

    // Initialize and start WiFi
    wifi_manager_init();
    wifi_manager_start();

    // Initialize and start web server
    web_server_init();
    web_server_start();

    // Initialize BLE HRM client
    ble_hrm_init();

    // Create fan control task
    xTaskCreate(fan_control_task, "fan_control", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Gale initialized successfully");
    ESP_LOGI(TAG, "HR Max: %d, Resting: %d", g_config.hrMax, g_config.hrResting);
    ESP_LOGI(TAG, "Zone 1: %.1f, Zone 2: %.1f, Zone 3: %.1f", g_zone1, g_zone2, g_zone3);
    ESP_LOGI(TAG, "Fan delay: %d ms, Hysteresis: %d BPM, Always on: %d",
             g_config.fanDelay, g_config.hrHysteresis, g_config.alwaysOn);

    // Print OTA partition information
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "OTA update pending verification");
            // If we get here, the new firmware is working, so mark it as valid
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
    ESP_LOGI(TAG, "Running partition: %s", running->label);

    // Main loop - just keep the task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
