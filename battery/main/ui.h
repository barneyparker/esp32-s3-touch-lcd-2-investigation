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
 * @brief Initialize LVGL and create UI elements
 *
 * @param panel_handle LCD panel handle from display_init()
 */
void ui_init(esp_lcd_panel_handle_t panel_handle);

/**
 * @brief Update UI with battery information
 *
 * @param voltage Battery voltage in volts
 * @param adc_raw Raw ADC reading
 * @param pct_milli Battery percentage in thousandths
 */
void ui_update_battery(float voltage, int adc_raw, int pct_milli);

#endif // UI_H
