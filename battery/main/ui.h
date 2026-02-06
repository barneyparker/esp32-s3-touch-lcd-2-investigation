#ifndef UI_H
#define UI_H

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"

/**
 * @brief Callback for LCD DMA transfer completion
 *
 * This callback is invoked by the LCD driver when a DMA transfer completes.
 * It notifies LVGL that the flush operation is ready.
 */
bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);

/**
 * @brief Initialize LVGL and create startup screen with spinner
 *
 * @param panel_handle LCD panel handle from display_init()
 */
void ui_init(esp_lcd_panel_handle_t panel_handle);

/**
 * @brief Update startup status message
 *
 * @param status Status message to display
 */
void ui_update_startup_status(const char *status);

/**
 * @brief Transition from startup screen to main UI
 */
void ui_show_main_screen(void);

/**
 * @brief Update UI with battery information
 *
 * @param voltage Battery voltage in volts
 * @param adc_raw Raw ADC reading
 * @param pct_milli Battery percentage in thousandths
 */
void ui_update_battery(float voltage, int adc_raw, int pct_milli);

/**
 * @brief Display a QR code with message on the startup screen
 *
 * @param qr_data QR code data string (e.g., WiFi connection string)
 * @param message Message to display below QR code
 */
void ui_show_qr_code(const char *qr_data, const char *message);

/**
 * @brief Update main screen with step count and status
 *
 * @param step_count Total steps detected
 * @param buffer_count Number of unsent steps in buffer
 * @param wifi_connected WiFi connection status
 * @param ws_connected WebSocket connection status
 * @param battery_pct Battery percentage (0-100)
 */
void ui_update_status(uint32_t step_count, uint8_t buffer_count, bool wifi_connected, bool ws_connected, int battery_pct);

/**
 * @brief Update power management countdown timers
 *
 * @param wifi_countdown_s Seconds until WiFi shuts down (0 = already off)
 * @param display_countdown_s Seconds until display shuts down (0 = already off)
 */
void ui_update_power_timers(int wifi_countdown_s, int display_countdown_s);

/**
 * @brief Show or hide OTA update status overlay
 *
 * @param visible True to show OTA overlay, false to hide
 */
void ui_show_ota_status(bool visible);

/**
 * @brief Update OTA download progress
 *
 * @param percent Download progress percentage (0-100)
 */
void ui_update_ota_progress(int percent);

#endif // UI_H
