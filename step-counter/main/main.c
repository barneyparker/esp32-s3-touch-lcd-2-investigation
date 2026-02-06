#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "battery.h"
#include "display.h"
#include "ui.h"
#include "touch.h"
#include "wifi_manager.h"
#include "ntp_time.h"
#include "websocket_client.h"
#include "step_counter.h"
#include "ota.h"

static const char *TAG = "main";

// Power management state
static bool wifi_power_saving_active = false;
static bool display_power_saving_active = false;
static uint64_t power_management_start_time_ms = 0;

static void app_main_loop(void)
{
  // Initialize power management timer
  power_management_start_time_ms = esp_timer_get_time() / 1000;
  
  // Battery reading throttle - only read every 15 seconds
  uint64_t last_battery_read_ms = 0;
  float voltage = 0.0f;
  int battery_pct = 0;

  while (1)
  {
    uint64_t current_time_ms = esp_timer_get_time() / 1000;
    uint64_t last_step_ms = step_counter_get_last_step_time_ms();

    // Use the later of: power management start time or last step time
    uint64_t activity_reference_ms = (last_step_ms > power_management_start_time_ms) ? last_step_ms : power_management_start_time_ms;
    uint64_t time_since_last_step_ms = current_time_ms - activity_reference_ms;

    // Only read battery every 15 seconds (not every 100ms)
    if (current_time_ms - last_battery_read_ms >= 15000) {
      int adc_raw = 0;
      read_battery(&voltage, &adc_raw);
      int pct_milli = estimate_percentage_milli(voltage);
      battery_pct = pct_milli / 10;
      last_battery_read_ms = current_time_ms;
    }

    uint8_t buffer_size = step_counter_get_buffer_size();
    uint32_t total_steps = step_counter_get_total_steps();

    // Check if we need to reconnect WiFi after a step
    if (step_counter_needs_wifi_reconnect() && wifi_power_saving_active) {
      ESP_LOGI(TAG, "Step detected while WiFi off - reconnecting...");
      wifi_power_saving_active = false;
      power_management_start_time_ms = current_time_ms; // Reset timer

      // Reconnect WiFi (don't call init, WiFi is already initialized)
      wifi_result_t result = wifi_manager_reconnect();
      if (result == WIFI_RESULT_CONNECTED) {
        ESP_LOGI(TAG, "WiFi reconnected");
        // Reconnect WebSocket
        if (websocket_client_start() == ESP_OK) {
          ESP_LOGI(TAG, "WebSocket reconnected");
        }
      } else {
        ESP_LOGW(TAG, "Failed to reconnect WiFi after step");
      }
    }

    // Reset power management timer on any step
    if (last_step_ms > power_management_start_time_ms) {
      power_management_start_time_ms = last_step_ms;
    }

    bool wifi_connected = wifi_manager_is_connected();
    bool ws_connected = websocket_client_is_connected();

    // Calculate countdown timers
    int wifi_countdown_s = 0;
    int display_countdown_s = 0;

    if (!wifi_power_saving_active && time_since_last_step_ms < 30000) {
      wifi_countdown_s = (30000 - time_since_last_step_ms) / 1000;
    }

    if (!display_power_saving_active && time_since_last_step_ms < 60000) {
      display_countdown_s = (60000 - time_since_last_step_ms) / 1000;
    }

    // Power management: WiFi
    // Turn off WiFi if no steps for 30 seconds AND buffer is empty
    if (!wifi_power_saving_active && time_since_last_step_ms > 30000 && buffer_size == 0) {
      ESP_LOGI(TAG, "No activity for 30s, turning off WiFi to save power");
      websocket_client_stop();
      wifi_manager_disconnect();
      wifi_power_saving_active = true;
      wifi_connected = false;
      ws_connected = false;
    }

    // Power management: Display
    // Turn off display backlight if no steps for 60 seconds
    if (!display_power_saving_active && time_since_last_step_ms > 60000) {
      ESP_LOGI(TAG, "No activity for 60s, turning off display to save power");
      display_backlight_off();
      display_power_saving_active = true;
    } else if (display_power_saving_active && time_since_last_step_ms < 60000) {
      ESP_LOGI(TAG, "Activity detected, turning display back on");
      display_backlight_on();
      display_power_saving_active = false;
    }

    // Log power management state only when buffer has steps or timers are near zero
    if ((buffer_size > 0 && current_time_ms - last_battery_read_ms < 100) || 
        wifi_countdown_s <= 5 || display_countdown_s <= 5) {
      ESP_LOGI(TAG, "WiFi in: %ds, Display in: %ds, Steps: %lu, Buffered: %d",
               wifi_countdown_s,
               display_countdown_s,
               (unsigned long)total_steps,
               buffer_size);
    }

    // Update UI with all status information (even when display is off, so it's ready when we turn back on)
    ui_update_status(total_steps, buffer_size, wifi_connected, ws_connected, battery_pct);

    // Update power management countdown timers on display
    ui_update_power_timers(wifi_countdown_s, display_countdown_s);

    // Try to send ALL buffered steps if we have any and are connected
    if (buffer_size > 0 && ws_connected) {
      int sent_count = 0;
      while (step_counter_get_buffer_size() > 0 && websocket_client_is_connected()) {
        esp_err_t err = step_counter_flush_one();
        if (err == ESP_OK) {
          sent_count++;
        } else if (err != ESP_ERR_NOT_FOUND) {
          ESP_LOGW(TAG, "Failed to send buffered step: %s", esp_err_to_name(err));
          break; // Stop trying if we fail to send
        } else {
          break; // Buffer is empty
        }
      }
      if (sent_count > 0) {
        ESP_LOGI(TAG, "Sent %d buffered step(s)", sent_count);
      }
    }

    // Run main loop faster for more responsive display and faster WebSocket sends
    // Check every 100ms instead of 1 second
    vTaskDelay(pdMS_TO_TICKS(100));
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

    // Check for firmware updates
    ui_update_startup_status("Checking for updates...");
    if (ota_init() == ESP_OK) {
      ESP_LOGI(TAG, "OTA initialized");
      esp_err_t ota_result = ota_check_and_update();
      if (ota_result == ESP_OK) {
        const char* etag = ota_get_current_etag();
        if (etag) {
          ESP_LOGI(TAG, "Firmware up to date (ETag: %s)", etag);
        } else {
          ESP_LOGI(TAG, "No firmware update available");
        }
        ui_update_startup_status("Firmware up to date!");
      } else {
        ESP_LOGW(TAG, "OTA check failed: %s", esp_err_to_name(ota_result));
        ui_update_startup_status("Update check failed");
      }
    } else {
      ESP_LOGW(TAG, "Failed to initialize OTA");
      ui_update_startup_status("OTA init failed");
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize and connect to WebSocket server
    ui_update_startup_status("Connecting to server...");
    if (websocket_client_init() == ESP_OK) {
      if (websocket_client_start() == ESP_OK) {
        ESP_LOGI(TAG, "WebSocket connection initiated");
        ui_update_startup_status("Server connection started");
      } else {
        ESP_LOGW(TAG, "Failed to start WebSocket connection");
        ui_update_startup_status("Server connection failed");
      }
    } else {
      ESP_LOGW(TAG, "Failed to initialize WebSocket client");
      ui_update_startup_status("Server init failed");
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize step counter
    ui_update_startup_status("Initializing step counter...");
    if (step_counter_init() == ESP_OK) {
      ESP_LOGI(TAG, "Step counter initialized");
      ui_update_startup_status("Step counter ready!");
    } else {
      ESP_LOGW(TAG, "Failed to initialize step counter");
      ui_update_startup_status("Step counter failed");
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
