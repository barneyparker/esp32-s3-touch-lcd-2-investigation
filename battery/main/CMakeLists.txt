idf_component_register(SRCS "main.c" "battery.c" "display.c" "ui.c" "touch.c" "wifi_manager.c"
                    INCLUDE_DIRS "."
                    REQUIRES lvgl esp_lcd driver esp_driver_ledc esp_adc esp_lcd_touch_cst816s cjson nvs_flash esp_http_server esp_wifi esp_netif)
