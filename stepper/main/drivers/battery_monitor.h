#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialize battery monitoring
 */
esp_err_t battery_monitor_init(void);

/**
 * @brief Read battery level (0-100%)
 */
uint8_t battery_monitor_get_level(void);

/**
 * @brief Check if battery is charging
 */
bool battery_monitor_is_charging(void);

#endif // BATTERY_MONITOR_H
