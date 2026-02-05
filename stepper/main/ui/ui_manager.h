#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "esp_err.h"
#include "app_state.h"

/**
 * @brief Initialize UI manager
 * @return ESP_OK on success
 */
esp_err_t ui_manager_init(void);

/**
 * @brief Update UI based on state changes
 * @param state Current application state
 */
void ui_manager_update(const app_state_t* state);

/**
 * @brief Show step mode screen
 */
void ui_manager_show_step_mode(void);

/**
 * @brief Show setup/config screen
 */
void ui_manager_show_setup(void);

/**
 * @brief Clear all UI objects
 */
void ui_manager_clear(void);

#endif
