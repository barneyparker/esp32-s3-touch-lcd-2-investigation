#ifndef UI_STEP_MODE_H
#define UI_STEP_MODE_H

#include <stdint.h>
#include "app_state.h"

/**
 * @brief Create step mode UI screen
 * Must be called within display_driver_lock() context
 */
void ui_step_mode_create(void);

/**
 * @brief Update step count display
 */
void ui_step_mode_update_count(uint32_t count);

/**
 * @brief Update time display (HH:MM format)
 * @param state Current app state containing time
 */
void ui_step_mode_update_time(const app_state_t* state);

/**
 * @brief Update WiFi status indicator and SSID
 */
void ui_step_mode_update_wifi(wifi_state_t state, int8_t rssi);

/**
 * @brief Update WiFi network name (SSID)
 */
void ui_step_mode_update_wifi_ssid(const char* ssid);

/**
 * @brief Update WebSocket status indicator
 */
void ui_step_mode_update_ws(ws_state_t state);

/**
 * @brief Update battery display
 */
void ui_step_mode_update_battery(uint8_t level, bool charging);

/**
 * @brief Update step backlog counter
 */
void ui_step_mode_update_backlog(uint32_t backlog_size);

#endif
