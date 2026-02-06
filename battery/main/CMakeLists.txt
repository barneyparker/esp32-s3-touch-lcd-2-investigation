idf_component_register(SRCS "main.c" "battery.c" "display.c" "ui.c" "touch.c"
                    INCLUDE_DIRS "."
                    REQUIRES lvgl esp_lcd driver esp_driver_ledc esp_adc esp_lcd_touch_cst816s)
