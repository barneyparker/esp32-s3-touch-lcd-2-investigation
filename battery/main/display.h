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

#endif // DISPLAY_H
