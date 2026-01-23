#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "gale.h"

static const char *TAG = "NVS_CONFIG";
static const char *NAMESPACE = "gale";

void nvs_config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
}

void calculate_zones(void)
{
    float hrReserve = g_config.hrMax - g_config.hrResting;
    g_zone1 = g_config.hrResting + (g_config.zone1Percent * hrReserve);
    g_zone2 = g_config.zone2Percent * g_config.hrMax;
    g_zone3 = g_config.zone3Percent * g_config.hrMax;

    ESP_LOGI(TAG, "Zones calculated: Zone1=%.1f, Zone2=%.1f, Zone3=%.1f",
             g_zone1, g_zone2, g_zone3);
}

void nvs_config_load(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved config found, using defaults");
        calculate_zones();
        return;
    }

    // WiFi settings
    size_t size;

    size = sizeof(g_config.wifiSSID);
    nvs_get_str(nvs_handle, "wifiSSID", g_config.wifiSSID, &size);

    size = sizeof(g_config.wifiPassword);
    nvs_get_str(nvs_handle, "wifiPass", g_config.wifiPassword, &size);

    size = sizeof(g_config.apSSID);
    nvs_get_str(nvs_handle, "apSSID", g_config.apSSID, &size);

    size = sizeof(g_config.apPassword);
    nvs_get_str(nvs_handle, "apPass", g_config.apPassword, &size);

    uint8_t useStation = 0;
    if (nvs_get_u8(nvs_handle, "useStation", &useStation) == ESP_OK) {
        g_config.useStationMode = useStation;
    }

    // Heart rate settings
    nvs_get_u8(nvs_handle, "hrMax", &g_config.hrMax);
    nvs_get_u8(nvs_handle, "hrRest", &g_config.hrResting);

    // Zone percentages
    uint32_t zone_val;
    if (nvs_get_u32(nvs_handle, "zone1Pct", &zone_val) == ESP_OK) {
        memcpy(&g_config.zone1Percent, &zone_val, sizeof(float));
    }
    if (nvs_get_u32(nvs_handle, "zone2Pct", &zone_val) == ESP_OK) {
        memcpy(&g_config.zone2Percent, &zone_val, sizeof(float));
    }
    if (nvs_get_u32(nvs_handle, "zone3Pct", &zone_val) == ESP_OK) {
        memcpy(&g_config.zone3Percent, &zone_val, sizeof(float));
    }

    // Fan behavior
    nvs_get_u8(nvs_handle, "alwaysOn", &g_config.alwaysOn);
    nvs_get_u32(nvs_handle, "fanDelay", &g_config.fanDelay);
    nvs_get_u8(nvs_handle, "hrHyst", &g_config.hrHysteresis);

    // GPIO pins
    size = sizeof(g_config.relayGPIO);
    nvs_get_blob(nvs_handle, "gpios", g_config.relayGPIO, &size);

    nvs_close(nvs_handle);

    calculate_zones();

    ESP_LOGI(TAG, "Configuration loaded");
    ESP_LOGI(TAG, "HR Max: %d, Resting: %d", g_config.hrMax, g_config.hrResting);
    ESP_LOGI(TAG, "Zone 1: %.1f, Zone 2: %.1f, Zone 3: %.1f", g_zone1, g_zone2, g_zone3);
}

void nvs_config_save(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    // WiFi settings
    nvs_set_str(nvs_handle, "wifiSSID", g_config.wifiSSID);
    nvs_set_str(nvs_handle, "wifiPass", g_config.wifiPassword);
    nvs_set_str(nvs_handle, "apSSID", g_config.apSSID);
    nvs_set_str(nvs_handle, "apPass", g_config.apPassword);
    nvs_set_u8(nvs_handle, "useStation", g_config.useStationMode ? 1 : 0);

    // Heart rate settings
    nvs_set_u8(nvs_handle, "hrMax", g_config.hrMax);
    nvs_set_u8(nvs_handle, "hrRest", g_config.hrResting);

    // Zone percentages (store float as uint32_t)
    uint32_t zone_val;
    memcpy(&zone_val, &g_config.zone1Percent, sizeof(float));
    nvs_set_u32(nvs_handle, "zone1Pct", zone_val);
    memcpy(&zone_val, &g_config.zone2Percent, sizeof(float));
    nvs_set_u32(nvs_handle, "zone2Pct", zone_val);
    memcpy(&zone_val, &g_config.zone3Percent, sizeof(float));
    nvs_set_u32(nvs_handle, "zone3Pct", zone_val);

    // Fan behavior
    nvs_set_u8(nvs_handle, "alwaysOn", g_config.alwaysOn);
    nvs_set_u32(nvs_handle, "fanDelay", g_config.fanDelay);
    nvs_set_u8(nvs_handle, "hrHyst", g_config.hrHysteresis);

    // GPIO pins
    nvs_set_blob(nvs_handle, "gpios", g_config.relayGPIO, sizeof(g_config.relayGPIO));

    // Commit
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);

    calculate_zones();

    ESP_LOGI(TAG, "Configuration saved");
}
