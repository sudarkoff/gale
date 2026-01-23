#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "gale.h"

static const char *TAG = "BLE_HRM";

// Heart Rate Service UUID: 0x180D
static esp_bt_uuid_t hrm_service_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = 0x180D,},
};

// Heart Rate Measurement Characteristic UUID: 0x2A37
static esp_bt_uuid_t hrm_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = 0x2A37,},
};

// Client Characteristic Configuration Descriptor UUID: 0x2902
static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = 0x2902,},
};

static bool is_connect = false;
static bool get_server = false;
static esp_gattc_char_elem_t *char_elem_result = NULL;
static esp_gattc_descr_elem_t *descr_elem_result = NULL;

// GATT client interface and connection info
static esp_gatt_if_t gattc_if = 0xff;
static uint16_t conn_id = 0;
static esp_bd_addr_t remote_bda;
static uint16_t hrm_service_start_handle = 0;
static uint16_t hrm_service_end_handle = 0;
static uint16_t hrm_char_handle = 0;
static uint16_t notify_descr_handle = 0;

// Calculate fan speed from heart rate data
static void calculate_fan_speed(uint8_t *pData, size_t length)
{
    if (length < 2 || pData[1] == 0) return;

    uint8_t current_speed = g_current_speed;

    // ZONE 0 -> FAN OFF (or minimum speed if alwaysOn)
    if (current_speed > 0 && pData[1] < g_zone1) {
        g_current_speed = g_config.alwaysOn;
        g_speed_changed_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    // ZONE 1
    else if ((current_speed < 1 && pData[1] >= g_zone1 && pData[1] < g_zone2) ||
             (current_speed > 1 && pData[1] < g_zone2 - g_config.hrHysteresis)) {
        g_current_speed = 1;
        g_speed_changed_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    // ZONE 2
    else if ((current_speed < 2 && pData[1] >= g_zone2 && pData[1] < g_zone3) ||
             (current_speed > 2 && pData[1] < g_zone3 - g_config.hrHysteresis)) {
        g_current_speed = 2;
        g_speed_changed_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    // ZONE 3
    else if (current_speed < 3 && pData[1] >= g_zone3) {
        g_current_speed = 3;
        g_speed_changed_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    ESP_LOGI(TAG, "Heart Rate: %d BPM, Current Speed: %d", pData[1], g_current_speed);
}

// GAP event handler
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scan parameters set, starting scan");
        esp_ble_gap_start_scanning(0); // 0 = scan continuously
        break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Scan start failed, status %d", param->scan_start_cmpl.status);
        } else {
            ESP_LOGI(TAG, "Scan started successfully");
        }
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            // Check if device advertises Heart Rate Service
            uint8_t *adv_data = param->scan_rst.ble_adv;
            uint16_t adv_data_len = param->scan_rst.adv_data_len;

            for (int i = 0; i < adv_data_len; i++) {
                if (i + 2 < adv_data_len &&
                    adv_data[i+1] == ESP_BLE_AD_TYPE_16SRV_CMPL &&
                    adv_data[i+2] == 0x0D && adv_data[i+3] == 0x18) {

                    ESP_LOGI(TAG, "Found HRM device");
                    esp_ble_gap_stop_scanning();
                    memcpy(remote_bda, param->scan_rst.bda, sizeof(esp_bd_addr_t));
                    esp_ble_gattc_open(gattc_if, remote_bda, param->scan_rst.ble_addr_type, true);
                    break;
                }
            }
        }
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(TAG, "Scan stop failed, status %d", param->scan_stop_cmpl.status);
        } else {
            ESP_LOGI(TAG, "Scan stopped successfully");
        }
        break;

    default:
        break;
    }
}

// GATT client event handler
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if_local, esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(TAG, "GATT client registered, app_id %04x", param->reg.app_id);
        gattc_if = gattc_if_local;

        // Set scan parameters
        esp_ble_gap_set_scan_params(&(esp_ble_scan_params_t){
            .scan_type = BLE_SCAN_TYPE_ACTIVE,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
            .scan_interval = 0x50,
            .scan_window = 0x30,
            .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
        });
        break;

    case ESP_GATTC_OPEN_EVT:
        if (param->open.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Failed to connect, status %d", param->open.status);
            break;
        }
        ESP_LOGI(TAG, "Connected to HRM");
        conn_id = param->open.conn_id;
        is_connect = true;
        g_ble_connected = true;
        gpio_set_level(GPIO_NUM_2, 1); // Turn on LED

        // Start service discovery
        esp_ble_gattc_search_service(gattc_if, conn_id, &hrm_service_uuid);
        break;

    case ESP_GATTC_CLOSE_EVT:
        ESP_LOGI(TAG, "Disconnected from HRM");
        is_connect = false;
        get_server = false;
        g_ble_connected = false;
        g_disconnected_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        gpio_set_level(GPIO_NUM_2, 0); // Turn off LED

        // Restart scanning
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_ble_gap_start_scanning(0);
        break;

    case ESP_GATTC_SEARCH_RES_EVT:
        if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 &&
            param->search_res.srvc_id.uuid.uuid.uuid16 == 0x180D) {
            ESP_LOGI(TAG, "Found Heart Rate Service");
            hrm_service_start_handle = param->search_res.start_handle;
            hrm_service_end_handle = param->search_res.end_handle;
            get_server = true;
        }
        break;

    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (param->search_cmpl.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Service search failed, status %d", param->search_cmpl.status);
            break;
        }

        if (get_server) {
            // Get characteristics
            uint16_t count = 0;
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count(gattc_if,
                                                                     conn_id,
                                                                     ESP_GATT_DB_CHARACTERISTIC,
                                                                     hrm_service_start_handle,
                                                                     hrm_service_end_handle,
                                                                     0,
                                                                     &count);
            if (status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "Get attr count failed, status %d", status);
                break;
            }

            if (count > 0) {
                char_elem_result = malloc(sizeof(esp_gattc_char_elem_t) * count);
                if (!char_elem_result) {
                    ESP_LOGE(TAG, "malloc failed");
                    break;
                }

                status = esp_ble_gattc_get_all_char(gattc_if,
                                                   conn_id,
                                                   hrm_service_start_handle,
                                                   hrm_service_end_handle,
                                                   char_elem_result,
                                                   &count,
                                                   0);

                if (status != ESP_GATT_OK) {
                    ESP_LOGE(TAG, "Get all char failed, status %d", status);
                    free(char_elem_result);
                    char_elem_result = NULL;
                    break;
                }

                // Find HRM characteristic
                for (int i = 0; i < count; i++) {
                    if (char_elem_result[i].uuid.len == ESP_UUID_LEN_16 &&
                        char_elem_result[i].uuid.uuid.uuid16 == 0x2A37) {
                        hrm_char_handle = char_elem_result[i].char_handle;
                        ESP_LOGI(TAG, "Found HRM characteristic, handle %d", hrm_char_handle);

                        // Get descriptors
                        uint16_t descr_count = 0;
                        status = esp_ble_gattc_get_attr_count(gattc_if,
                                                              conn_id,
                                                              ESP_GATT_DB_DESCRIPTOR,
                                                              hrm_service_start_handle,
                                                              hrm_service_end_handle,
                                                              hrm_char_handle,
                                                              &descr_count);

                        if (status == ESP_GATT_OK && descr_count > 0) {
                            descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * descr_count);
                            if (descr_elem_result) {
                                status = esp_ble_gattc_get_all_descr(gattc_if,
                                                                    conn_id,
                                                                    hrm_char_handle,
                                                                    descr_elem_result,
                                                                    &descr_count,
                                                                    0);

                                if (status == ESP_GATT_OK) {
                                    // Find notify descriptor
                                    for (int j = 0; j < descr_count; j++) {
                                        if (descr_elem_result[j].uuid.len == ESP_UUID_LEN_16 &&
                                            descr_elem_result[j].uuid.uuid.uuid16 == 0x2902) {
                                            notify_descr_handle = descr_elem_result[j].handle;
                                            ESP_LOGI(TAG, "Found notify descriptor, handle %d", notify_descr_handle);

                                            // Enable notifications
                                            uint8_t notify_en[] = {0x01, 0x00};
                                            esp_ble_gattc_write_char_descr(gattc_if,
                                                                          conn_id,
                                                                          notify_descr_handle,
                                                                          sizeof(notify_en),
                                                                          notify_en,
                                                                          ESP_GATT_WRITE_TYPE_RSP,
                                                                          ESP_GATT_AUTH_REQ_NONE);
                                            break;
                                        }
                                    }
                                }
                                free(descr_elem_result);
                                descr_elem_result = NULL;
                            }
                        }
                        break;
                    }
                }
                free(char_elem_result);
                char_elem_result = NULL;
            }
        }
        break;

    case ESP_GATTC_WRITE_DESCR_EVT:
        if (param->write.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Write descriptor failed, status %d", param->write.status);
        } else {
            ESP_LOGI(TAG, "Notifications enabled");
        }
        break;

    case ESP_GATTC_NOTIFY_EVT:
        // Heart rate notification received
        calculate_fan_speed(param->notify.value, param->notify.value_len);
        break;

    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGI(TAG, "Disconnect event");
        is_connect = false;
        get_server = false;
        g_ble_connected = false;
        g_disconnected_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        gpio_set_level(GPIO_NUM_2, 0); // Turn off LED

        // Restart scanning after delay
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_ble_gap_start_scanning(0);
        break;

    default:
        break;
    }
}

void ble_hrm_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE HRM client");

    // Initialize NVS for BLE
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Release classic BT memory
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register callbacks
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret) {
        ESP_LOGE(TAG, "GAP register failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if (ret) {
        ESP_LOGE(TAG, "GATTC register callback failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gattc_app_register(0);
    if (ret) {
        ESP_LOGE(TAG, "GATTC app register failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "BLE HRM client initialized");
}

void ble_hrm_start_scan(void)
{
    esp_ble_gap_start_scanning(0);
}
