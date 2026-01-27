#ifndef MATTER_DEVICE_H
#define MATTER_DEVICE_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Matter stack and create fan endpoint
esp_err_t matter_device_init(void);

// Update Matter attributes when fan state changes
void matter_device_update_fan_state(uint8_t speed);

// Check if device is commissioned
bool matter_device_is_commissioned(void);

#ifdef __cplusplus
}
#endif

#endif // MATTER_DEVICE_H
