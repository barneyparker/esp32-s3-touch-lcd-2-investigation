#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief Initialize display hardware (LCD + touch)
 */
esp_err_t display_driver_init(void);

/**
 * @brief Set backlight brightness (0-100%)
 */
void display_driver_set_brightness(uint8_t percent);

/**
 * @brief Get backlight brightness (0-100%)
 */
uint8_t display_driver_get_brightness(void);

/**
 * @brief Get display width in pixels
 */
uint16_t display_driver_get_width(void);

/**
 * @brief Get display height in pixels
 */
uint16_t display_driver_get_height(void);

/**
 * @brief Thread-safe LVGL lock (required before calling LVGL functions from other tasks)
 * @param timeout_ms Timeout in milliseconds, -1 for infinite
 * @return true if lock acquired, false on timeout
 */
bool display_driver_lock(int timeout_ms);

/**
 * @brief Release LVGL lock
 */
void display_driver_unlock(void);

#endif // DISPLAY_DRIVER_H
