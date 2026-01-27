#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "nimble/nimble_port.h"
#include "gale.h"

static const char *TAG = "BLE_HRM";

// Heart Rate Service UUID: 0x180D
static const ble_uuid16_t hrm_service_uuid = BLE_UUID16_INIT(0x180D);

// Heart Rate Measurement Characteristic UUID: 0x2A37
static const ble_uuid16_t hrm_char_uuid = BLE_UUID16_INIT(0x2A37);

// Client Characteristic Configuration Descriptor UUID: 0x2902
static const ble_uuid16_t cccd_uuid = BLE_UUID16_INIT(0x2902);

// Connection state
static uint16_t hrm_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool is_scanning = false;
static uint16_t hrm_chr_val_handle = 0;
static uint16_t hrm_chr_cccd_handle = 0;

// Forward declarations
static void ble_hrm_scan_start(void);
static int ble_hrm_gap_event(struct ble_gap_event *event, void *arg);
static int ble_hrm_on_dsc_disc(uint16_t conn_handle,
                               const struct ble_gatt_error *error,
                               uint16_t chr_val_handle,
                               const struct ble_gatt_dsc *dsc,
                               void *arg);

// Calculate fan speed from heart rate data
static void calculate_fan_speed(uint8_t heart_rate)
{
    if (heart_rate == 0) return;

    uint8_t current_speed = g_current_speed;

    // ZONE 0 -> FAN OFF (or minimum speed if alwaysOn)
    if (current_speed > 0 && heart_rate < g_zone1) {
        g_current_speed = g_config.alwaysOn;
        g_speed_changed_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    // ZONE 1
    else if ((current_speed < 1 && heart_rate >= g_zone1 && heart_rate < g_zone2) ||
             (current_speed > 1 && heart_rate < g_zone2 - g_config.hrHysteresis)) {
        g_current_speed = 1;
        g_speed_changed_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    // ZONE 2
    else if ((current_speed < 2 && heart_rate >= g_zone2 && heart_rate < g_zone3) ||
             (current_speed > 2 && heart_rate < g_zone3 - g_config.hrHysteresis)) {
        g_current_speed = 2;
        g_speed_changed_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    // ZONE 3
    else if (current_speed < 3 && heart_rate >= g_zone3) {
        g_current_speed = 3;
        g_speed_changed_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    ESP_LOGI(TAG, "Heart Rate: %d BPM, Current Speed: %d", heart_rate, g_current_speed);
}

// Callback for GATT attribute access (notifications)
static int ble_hrm_on_notify(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             struct ble_gatt_attr *attr,
                             void *arg)
{
    if (error->status == 0 && attr && attr->om) {
        uint8_t *data = OS_MBUF_DATA(attr->om, uint8_t *);
        uint16_t len = OS_MBUF_PKTLEN(attr->om);

        if (len >= 2) {
            // Parse heart rate value (flags in byte 0, HR in byte 1 for 8-bit format)
            uint8_t flags = data[0];
            uint8_t hr;

            if (flags & 0x01) {
                // 16-bit heart rate value
                if (len >= 3) {
                    hr = data[1];  // Use lower byte
                } else {
                    return 0;
                }
            } else {
                // 8-bit heart rate value
                hr = data[1];
            }

            calculate_fan_speed(hr);
        }
    }
    return 0;
}

// Callback for writing to CCCD (enabling notifications)
static int ble_hrm_on_cccd_write(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attr,
                                  void *arg)
{
    if (error->status == 0) {
        ESP_LOGI(TAG, "Notifications enabled");
    } else {
        ESP_LOGE(TAG, "Failed to enable notifications, status=%d", error->status);
    }
    return 0;
}

// Callback for characteristic discovery
static int ble_hrm_on_chr_disc(uint16_t conn_handle,
                               const struct ble_gatt_error *error,
                               const struct ble_gatt_chr *chr,
                               void *arg)
{
    if (error->status == 0 && chr) {
        // Check if this is the Heart Rate Measurement characteristic
        if (ble_uuid_cmp(&chr->uuid.u, &hrm_char_uuid.u) == 0) {
            hrm_chr_val_handle = chr->val_handle;
            ESP_LOGI(TAG, "Found HRM characteristic, handle=%d", hrm_chr_val_handle);
        }
    } else if (error->status == BLE_HS_EDONE) {
        // Characteristic discovery complete
        if (hrm_chr_val_handle != 0) {
            // Now discover the CCCD descriptor
            ESP_LOGI(TAG, "Discovering CCCD descriptor");
            ble_gattc_disc_all_dscs(conn_handle,
                                    hrm_chr_val_handle,
                                    hrm_chr_val_handle + 10,  // Search a range
                                    ble_hrm_on_dsc_disc, NULL);
        }
    }
    return 0;
}

// Callback for descriptor discovery
static int ble_hrm_on_dsc_disc(uint16_t conn_handle,
                               const struct ble_gatt_error *error,
                               uint16_t chr_val_handle,
                               const struct ble_gatt_dsc *dsc,
                               void *arg)
{
    if (error->status == 0 && dsc) {
        // Check if this is the CCCD
        if (ble_uuid_cmp(&dsc->uuid.u, &cccd_uuid.u) == 0) {
            hrm_chr_cccd_handle = dsc->handle;
            ESP_LOGI(TAG, "Found CCCD descriptor, handle=%d", hrm_chr_cccd_handle);
        }
    } else if (error->status == BLE_HS_EDONE) {
        // Descriptor discovery complete
        if (hrm_chr_cccd_handle != 0) {
            // Subscribe to notifications
            ESP_LOGI(TAG, "Subscribing to HRM notifications");
            uint8_t value[2] = {0x01, 0x00};  // Enable notifications
            ble_gattc_write_flat(conn_handle, hrm_chr_cccd_handle,
                                value, sizeof(value), ble_hrm_on_cccd_write, NULL);
        }
    }
    return 0;
}

// Callback for service discovery
static int ble_hrm_on_svc_disc(uint16_t conn_handle,
                               const struct ble_gatt_error *error,
                               const struct ble_gatt_svc *svc,
                               void *arg)
{
    if (error->status == 0 && svc) {
        ESP_LOGI(TAG, "Found Heart Rate Service, handles %d-%d",
                 svc->start_handle, svc->end_handle);

        // Discover characteristics within this service
        ble_gattc_disc_all_chrs(conn_handle,
                                svc->start_handle, svc->end_handle,
                                ble_hrm_on_chr_disc, NULL);
    } else if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "Service discovery complete");
    } else {
        ESP_LOGE(TAG, "Service discovery error, status=%d", error->status);
    }
    return 0;
}

// GAP event handler
static int ble_hrm_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        // Check if device advertises HRM service
        {
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                              event->disc.length_data);
            if (rc != 0) {
                break;
            }

            bool found_hrm = false;

            // Check 16-bit UUIDs
            for (int i = 0; i < fields.num_uuids16; i++) {
                if (ble_uuid_u16(&fields.uuids16[i].u) == 0x180D) {
                    found_hrm = true;
                    break;
                }
            }

            if (found_hrm) {
                char addr_str[18];
                snprintf(addr_str, sizeof(addr_str),
                         "%02x:%02x:%02x:%02x:%02x:%02x",
                         event->disc.addr.val[5], event->disc.addr.val[4],
                         event->disc.addr.val[3], event->disc.addr.val[2],
                         event->disc.addr.val[1], event->disc.addr.val[0]);
                ESP_LOGI(TAG, "Found HRM device: %s", addr_str);

                // Stop scanning and connect
                ble_gap_disc_cancel();
                is_scanning = false;

                rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &event->disc.addr,
                                    30000, NULL, ble_hrm_gap_event, NULL);
                if (rc != 0) {
                    ESP_LOGE(TAG, "Failed to connect, rc=%d", rc);
                    // Restart scanning
                    ble_hrm_scan_start();
                }
            }
        }
        break;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "Connected to HRM");
            hrm_conn_handle = event->connect.conn_handle;
            g_ble_connected = true;

            // Reset characteristic handles
            hrm_chr_val_handle = 0;
            hrm_chr_cccd_handle = 0;

            // Turn on fan to low speed when HRM connects
            if (g_current_speed == 0) {
                g_current_speed = 1;
                g_speed_changed_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                ESP_LOGI(TAG, "HRM connected - fan set to low speed");
            }

            // Start LED pulsing at current speed
            led_control_set_mode(g_current_speed);

            // Discover Heart Rate Service
            ble_gattc_disc_svc_by_uuid(hrm_conn_handle,
                                       &hrm_service_uuid.u,
                                       ble_hrm_on_svc_disc, NULL);
        } else {
            ESP_LOGE(TAG, "Connection failed, status=%d", event->connect.status);
            hrm_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            // Restart scanning after delay
            vTaskDelay(pdMS_TO_TICKS(1000));
            ble_hrm_scan_start();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected from HRM, reason=%d", event->disconnect.reason);
        hrm_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        hrm_chr_val_handle = 0;
        hrm_chr_cccd_handle = 0;
        g_ble_connected = false;
        g_disconnected_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        led_control_off();  // Turn off LED immediately
        // Fan will turn off after fanDelay timeout in fan_control_task

        // Restart scanning after delay
        vTaskDelay(pdMS_TO_TICKS(1000));
        ble_hrm_scan_start();
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        // Handle incoming notification
        if (event->notify_rx.attr_handle == hrm_chr_val_handle) {
            uint8_t *data = OS_MBUF_DATA(event->notify_rx.om, uint8_t *);
            uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);

            if (len >= 2) {
                uint8_t flags = data[0];
                uint8_t hr;

                if (flags & 0x01) {
                    // 16-bit heart rate
                    hr = (len >= 3) ? data[1] : 0;
                } else {
                    // 8-bit heart rate
                    hr = data[1];
                }

                calculate_fan_speed(hr);
            }
        }
        break;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "Discovery complete, reason=%d", event->disc_complete.reason);
        is_scanning = false;
        if (!g_ble_connected) {
            // Restart scanning after delay
            vTaskDelay(pdMS_TO_TICKS(1000));
            ble_hrm_scan_start();
        }
        break;

    default:
        break;
    }

    return 0;
}

// Start BLE scanning
static void ble_hrm_scan_start(void)
{
    if (is_scanning || g_ble_connected) {
        return;
    }

    struct ble_gap_disc_params disc_params = {
        .filter_duplicates = 1,
        .passive = 0,
        .itvl = 0x50,
        .window = 0x30,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0,
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params,
                          ble_hrm_gap_event, NULL);
    if (rc == 0) {
        is_scanning = true;
        ESP_LOGI(TAG, "Scanning started");
    } else {
        ESP_LOGE(TAG, "Failed to start scan, rc=%d", rc);
    }
}

void ble_hrm_init(void)
{
    ESP_LOGI(TAG, "Initializing NimBLE HRM client");
    // NimBLE is initialized by Matter stack
    // We just need to wait for the stack to be ready
    ESP_LOGI(TAG, "NimBLE HRM client initialized");
}

void ble_hrm_start_scan(void)
{
    ESP_LOGI(TAG, "Starting HRM scan");
    ble_hrm_scan_start();
}
