#ifndef GALE_H
#define GALE_H

#include <stdint.h>
#include <stdbool.h>

// Relay configuration
#define RELAY_NO
#define NUM_RELAYS 3

#ifdef RELAY_NO
#define RELAY_ON 0
#define RELAY_OFF 1
#else
#define RELAY_ON 1
#define RELAY_OFF 0
#endif

// Configuration structure
typedef struct {
    // WiFi settings
    char wifiSSID[32];
    char wifiPassword[64];
    char apSSID[32];
    char apPassword[64];
    bool useStationMode;

    // Heart rate settings
    uint8_t hrMax;
    uint8_t hrResting;

    // Zone thresholds (as percentages)
    float zone1Percent;  // Percent of HR reserve
    float zone2Percent;  // Percent of max HR
    float zone3Percent;  // Percent of max HR

    // Fan behavior settings
    uint8_t alwaysOn;         // 0=turn off below zone1, 1=keep on
    uint32_t fanDelay;        // Delay before lowering speed (ms)
    uint8_t hrHysteresis;     // BPM hysteresis for debouncing

    // GPIO pins
    uint8_t relayGPIO[NUM_RELAYS];
} config_t;

// Global configuration
extern config_t g_config;

// Calculated zone thresholds
extern float g_zone1, g_zone2, g_zone3;

// Fan speed state
extern uint8_t g_current_speed;
extern uint8_t g_prev_speed;
extern uint32_t g_speed_changed_time;

// BLE connection state
extern bool g_ble_connected;
extern uint32_t g_disconnected_time;

// Function declarations
void nvs_config_init(void);
void nvs_config_load(void);
void nvs_config_save(void);
void calculate_zones(void);

void ble_hrm_init(void);
void ble_hrm_start_scan(void);

void wifi_manager_init(void);
void wifi_manager_start(void);

void web_server_init(void);
void web_server_start(void);

void fan_control_init(void);
void fan_control_set_speed(uint8_t speed);
void fan_control_task(void *pvParameters);

#endif // GALE_H
