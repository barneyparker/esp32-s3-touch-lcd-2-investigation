#ifndef TOUCH_H
#define TOUCH_H

#include "esp_lcd_touch_cst816s.h"

/**
 * @brief Initialize touch controller
 *
 * @param io_handle LCD IO handle from display initialization
 * @return Touch handle
 */
esp_lcd_touch_handle_t touch_init(esp_lcd_panel_io_handle_t io_handle);

#endif // TOUCH_H
