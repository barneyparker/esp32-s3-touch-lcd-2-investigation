#include "touch.h"
#include "esp_log.h"

#define LCD_H_RES 240
#define LCD_V_RES 320
#define LCD_PIN_RST -1

static const char *TAG = "touch";

esp_lcd_touch_handle_t touch_init(esp_lcd_panel_io_handle_t io_handle)
{
  ESP_LOGI(TAG, "Initializing touch controller");

  esp_lcd_touch_handle_t tp = NULL;
  esp_lcd_touch_config_t tp_config = {
      .x_max = LCD_H_RES,
      .y_max = LCD_V_RES,
      .rst_gpio_num = LCD_PIN_RST,
      .int_gpio_num = -1,
      .levels = {
          .reset = 0,
          .interrupt = 0,
      },
  };

  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(io_handle, &tp_config, &tp));
  ESP_LOGI(TAG, "Touch controller initialized");

  return tp;
}
