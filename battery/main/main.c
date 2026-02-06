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
#include "wifi_manager.h"
#include "ntp_time.h"

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

  // Initialize display hardware FIRST
  esp_lcd_panel_handle_t panel = display_init(notify_lvgl_flush_ready);

  // Initialize LVGL and show startup screen
  ui_init(panel);
  ui_update_startup_status("Initializing hardware...");
  vTaskDelay(pdMS_TO_TICKS(100)); // Allow UI to render

  // Initialize battery monitoring
  ui_update_startup_status("Starting battery monitor...");
  battery_init();
  ESP_LOGI(TAG, "Battery monitoring initialized");
  vTaskDelay(pdMS_TO_TICKS(500));

  // Initialize touch controller
  ui_update_startup_status("Initializing touch...");
  // Note: touch_init returns handle but we're not using it yet
  // touch_init(io_handle);
  vTaskDelay(pdMS_TO_TICKS(500));

  // Initialize WiFi and check for stored credentials
  ui_update_startup_status("Checking WiFi...");
  wifi_result_t wifi_result = wifi_manager_init();

  if (wifi_result == WIFI_RESULT_CONNECTED) {
    ESP_LOGI(TAG, "WiFi connected successfully");
    ui_update_startup_status("WiFi connected!");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Synchronize time with NTP server
    ui_update_startup_status("Syncing time...");
    if (ntp_time_sync()) {
      char time_str[64];
      if (ntp_time_get_string(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S")) {
        ESP_LOGI(TAG, "Current time: %s", time_str);
        ui_update_startup_status("Time synchronized!");
      } else {
        ui_update_startup_status("Time set!");
      }
    } else {
      ESP_LOGW(TAG, "Failed to sync time with NTP server");
      ui_update_startup_status("Time sync failed");
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  } else if (wifi_result == WIFI_RESULT_NO_CREDENTIALS) {
    ESP_LOGW(TAG, "No WiFi credentials stored");
    ui_update_startup_status("No WiFi - Starting AP...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    wifi_manager_start_ap_mode();

    // Display QR code for easy connection
    char qr_data[100];
    if (wifi_manager_get_ap_qr_string(qr_data, sizeof(qr_data))) {
      ui_show_qr_code(qr_data, "Scan to connect to 'Stepper'");
    } else {
      ui_update_startup_status("Connect to 'Stepper' WiFi");
    }

    // Stay in AP mode - don't transition to main screen yet
    while(1) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  } else {
    ESP_LOGW(TAG, "WiFi connection failed, starting AP mode");
    ui_update_startup_status("WiFi failed - Starting AP...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    wifi_manager_start_ap_mode();

    // Display QR code for easy connection
    char qr_data[100];
    if (wifi_manager_get_ap_qr_string(qr_data, sizeof(qr_data))) {
      ui_show_qr_code(qr_data, "Scan to connect to 'Stepper'");
    } else {
      ui_update_startup_status("Connect to 'Stepper' WiFi");
    }

    // Stay in AP mode
    while(1) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  // Log chip info
  ui_update_startup_status("System ready!");
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  ESP_LOGI(TAG, "Chip: %s, cores: %d, features: 0x%lx",
           CONFIG_IDF_TARGET, chip_info.cores, chip_info.features);
  vTaskDelay(pdMS_TO_TICKS(500));

  // Transition to main screen
  ui_show_main_screen();

  // Start main application loop
  app_main_loop();
}
