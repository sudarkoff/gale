#include <esp_matter.h>
#include <esp_matter_core.h>
#include <app/server/Server.h>
#include <app/clusters/fan-control-server/fan-control-server.h>
#include "esp_log.h"

extern "C" {
#include "gale.h"
#include "matter_device.h"
}

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "MATTER_DEVICE";

static uint16_t fan_endpoint_id = 0;

// Convert Gale speed (0-3) to Matter percent (0-100)
static uint8_t speed_to_percent(uint8_t speed)
{
    switch (speed) {
        case 0: return 0;
        case 1: return 33;
        case 2: return 66;
        case 3: return 100;
        default: return 0;
    }
}

// Convert Matter percent (0-100) to Gale speed (0-3)
static uint8_t percent_to_speed(uint8_t percent)
{
    if (percent == 0) return 0;
    if (percent <= 33) return 1;
    if (percent <= 66) return 2;
    return 3;
}

// Helper to apply Matter speed change immediately and set override mode
static void apply_matter_speed(uint8_t new_speed, bool enable_override)
{
    g_matter_override = enable_override;
    g_current_speed = new_speed;
    fan_control_set_speed_immediate(new_speed);

    if (!enable_override && g_ble_connected) {
        ESP_LOGI(TAG, "Returning to HRM auto mode");
    }
}

// Attribute update callback - called when Matter client changes attributes
static esp_err_t app_attribute_update_cb(
    attribute::callback_type_t type,
    uint16_t endpoint_id,
    uint32_t cluster_id,
    uint32_t attribute_id,
    esp_matter_attr_val_t *val,
    void *priv_data)
{
    if (type != attribute::PRE_UPDATE || endpoint_id != fan_endpoint_id) {
        return ESP_OK;
    }

    if (cluster_id == FanControl::Id) {
        if (attribute_id == FanControl::Attributes::PercentSetting::Id) {
            uint8_t new_percent = val->val.u8;
            uint8_t new_speed = percent_to_speed(new_percent);
            ESP_LOGI(TAG, "Matter: Fan percent set to %d (speed %d)", new_percent, new_speed);
            // Off (0%) returns to auto mode, otherwise override HRM
            apply_matter_speed(new_speed, new_speed > 0);
        }
        else if (attribute_id == FanControl::Attributes::FanMode::Id) {
            uint8_t mode = val->val.u8;
            ESP_LOGI(TAG, "Matter: Fan mode set to %d", mode);
            // Map mode to speed: 0=Off, 1=Low, 2=Medium, 3=High, 4=On, 5=Auto
            switch (mode) {
                case 0:  // Off - return to auto mode
                    apply_matter_speed(0, false);
                    break;
                case 1:  // Low
                    apply_matter_speed(1, true);
                    break;
                case 2:  // Medium
                    apply_matter_speed(2, true);
                    break;
                case 3:  // High
                    apply_matter_speed(3, true);
                    break;
                case 4:  // On (default to low)
                    apply_matter_speed(1, true);
                    break;
                case 5:  // Auto - let HRM control it
                    g_matter_override = false;
                    ESP_LOGI(TAG, "Returning to HRM auto mode");
                    break;
                default:
                    break;
            }
        }
        else if (attribute_id == FanControl::Attributes::SpeedSetting::Id) {
            uint8_t new_speed = val->val.u8;
            if (new_speed <= 3) {
                ESP_LOGI(TAG, "Matter: Fan speed set to %d", new_speed);
                // Off (0) returns to auto mode, otherwise override HRM
                apply_matter_speed(new_speed, new_speed > 0);
            }
        }
    }

    return ESP_OK;
}

// Matter event callback
static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            ESP_LOGI(TAG, "Last fabric removed, factory reset");
            esp_matter::factory_reset();
        }
        break;

    default:
        break;
    }
}

esp_err_t matter_device_init(void)
{
    ESP_LOGI(TAG, "Initializing Matter device");

    // Create Matter node
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, NULL);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return ESP_FAIL;
    }

    // Create fan endpoint
    endpoint::fan::config_t fan_config;
    fan_config.fan_control.fan_mode = 0;  // Off initially
    fan_config.fan_control.fan_mode_sequence = 2;  // Off/Low/Med/High
    fan_config.fan_control.percent_setting = nullable<uint8_t>(0);
    fan_config.fan_control.percent_current = 0;

    endpoint_t *fan_endpoint = endpoint::fan::create(node, &fan_config, ENDPOINT_FLAG_NONE, NULL);
    if (!fan_endpoint) {
        ESP_LOGE(TAG, "Failed to create fan endpoint");
        return ESP_FAIL;
    }

    fan_endpoint_id = endpoint::get_id(fan_endpoint);
    ESP_LOGI(TAG, "Fan endpoint created with ID: %d", fan_endpoint_id);

    // Add multi-speed feature to fan control cluster
    cluster_t *fan_cluster = cluster::get(fan_endpoint, FanControl::Id);
    if (fan_cluster) {
        cluster::fan_control::feature::multi_speed::config_t multi_speed_config;
        multi_speed_config.speed_max = 3;  // 3 speed levels
        multi_speed_config.speed_setting = nullable<uint8_t>(0);
        multi_speed_config.speed_current = 0;
        cluster::fan_control::feature::multi_speed::add(fan_cluster, &multi_speed_config);
    }

    // Start Matter
    esp_err_t err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Matter device initialized successfully");
    ESP_LOGI(TAG, "==================================");
    ESP_LOGI(TAG, "Matter Commissioning Information:");
    ESP_LOGI(TAG, "Discriminator: 3840");
    ESP_LOGI(TAG, "Passcode: 20202021");
    ESP_LOGI(TAG, "==================================");

    return ESP_OK;
}

void matter_device_update_fan_state(uint8_t speed)
{
    if (fan_endpoint_id == 0) {
        return;
    }

    uint8_t percent = speed_to_percent(speed);

    // Update PercentCurrent attribute (non-nullable)
    esp_matter_attr_val_t val = esp_matter_uint8(percent);
    attribute::update(fan_endpoint_id, FanControl::Id,
                     FanControl::Attributes::PercentCurrent::Id, &val);

    // Update SpeedCurrent attribute (non-nullable)
    val = esp_matter_uint8(speed);
    attribute::update(fan_endpoint_id, FanControl::Id,
                     FanControl::Attributes::SpeedCurrent::Id, &val);

    // Update FanMode to reflect current state
    // If Matter is overriding, show the manual mode (0=Off, 1=Low, 2=Medium, 3=High)
    // If HRM is controlling (auto mode), show Auto (5) when on, Off (0) when off
    uint8_t fan_mode;
    if (g_matter_override) {
        fan_mode = speed;  // 0=Off, 1=Low, 2=Med, 3=High
    } else {
        fan_mode = (speed > 0) ? 5 : 0;  // Auto or Off
    }
    val = esp_matter_enum8(fan_mode);
    attribute::update(fan_endpoint_id, FanControl::Id,
                     FanControl::Attributes::FanMode::Id, &val);

    ESP_LOGD(TAG, "Matter state updated: speed=%d, percent=%d, mode=%d", speed, percent, fan_mode);
}

bool matter_device_is_commissioned(void)
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}
