#ifndef DISPLAY_H
#define DISPLAY_H

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"

/**
 * @brief Initialize display hardware (LCD panel, SPI, backlight)
 *
 * @param on_color_trans_done Callback invoked when DMA transfer completes
 * @return LCD panel handle for use by other components
 */
esp_lcd_panel_handle_t display_init(esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done);

/**
 * @brief Set display backlight brightness
 *
 * @param brightness Brightness level 0-100 (0 = off, 100 = full brightness)
 */
void display_set_backlight(uint8_t brightness);

/**
 * @brief Turn display backlight on
 */
void display_backlight_on(void);

/**
 * @brief Turn display backlight off
 */
void display_backlight_off(void);

#endif // DISPLAY_H
