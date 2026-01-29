idf_component_register(SRCS "one.c"
                    INCLUDE_DIRS "."
                    REQUIRES lvgl esp_lcd driver esp_driver_ledc esp_adc
                    PRIV_REQUIRES esp_lcd_touch_cst816s)
