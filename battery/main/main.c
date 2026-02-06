#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_log.h"

#include "battery.h"
#include "display.h"
#include "ui.h"
#include "touch.h"

static const char *TAG = "main";

static void app_main_loop(void)
{
  while (1)
  {
    float voltage = 0.0f;
    int adc_raw = 0;
    read_battery(&voltage, &adc_raw);

    int pct_milli = estimate_percentage_milli(voltage);

    ESP_LOGI(TAG, "ADC raw: %d, Voltage: %.3f V, Percent: %.1f%%, free heap: %u",
             adc_raw,
             voltage,
             pct_milli / 10.0f,
             esp_get_free_heap_size());

    ui_update_battery(voltage, adc_raw, pct_milli);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void app_main(void)
{
  ESP_LOGI(TAG, "Starting battery monitor demo");

  // Initialize battery monitoring
  battery_init();
  ESP_LOGI(TAG, "Battery monitoring initialized");

  // Initialize display hardware
  esp_lcd_panel_handle_t panel = display_init(notify_lvgl_flush_ready);

  // Initialize UI and LVGL
  ui_init(panel);

  // Initialize touch controller
  // Note: touch_init returns handle but we're not using it yet
  // touch_init(io_handle);

  // Log chip info
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  ESP_LOGI(TAG, "Chip: %s, cores: %d, features: 0x%lx",
           CONFIG_IDF_TARGET, chip_info.cores, chip_info.features);

  // Start main application loop
  app_main_loop();
}
