#include "ntp_time.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>

static const char *TAG = "ntp_time";

// NTP server configuration
#define NTP_SERVER_PRIMARY "pool.ntp.org"
#define NTP_SERVER_SECONDARY "time.nist.gov"
#define NTP_SYNC_TIMEOUT_MS 10000
#define NTP_RETRY_COUNT 10

static bool time_synced = false;

static void time_sync_notification_cb(struct timeval *tv)
{
  ESP_LOGI(TAG, "Time synchronized from NTP server");
  time_synced = true;
}

bool ntp_time_sync(void)
{
  ESP_LOGI(TAG, "Initializing SNTP time synchronization...");

  // Reset sync flag
  time_synced = false;

  // Configure SNTP
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, NTP_SERVER_PRIMARY);
  esp_sntp_setservername(1, NTP_SERVER_SECONDARY);
  esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

  // Start SNTP service
  esp_sntp_init();

  ESP_LOGI(TAG, "Waiting for time synchronization...");

  // Wait for time to be set
  int retry = 0;
  while (retry < NTP_RETRY_COUNT && !time_synced) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    retry++;

    time_t now = 0;
    time(&now);

    // Check if time has been set (Unix epoch starts at 1970, so anything before 2020 is not set)
    if (now > 1577836800) { // Jan 1, 2020
      time_synced = true;
      break;
    }
  }

  if (time_synced) {
    time_t now = 0;
    struct tm timeinfo = {0};
    char strftime_buf[64];

    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);

    ESP_LOGI(TAG, "Time synchronized successfully: %s", strftime_buf);
    return true;
  } else {
    ESP_LOGW(TAG, "Failed to synchronize time after %d attempts", retry);
    return false;
  }
}

time_t ntp_time_get_current(void)
{
  time_t now = 0;
  time(&now);
  return now;
}

bool ntp_time_is_synced(void)
{
  return time_synced;
}

bool ntp_time_get_string(char *buffer, size_t buffer_size, const char *format)
{
  if (!buffer || buffer_size == 0 || !format) {
    return false;
  }

  time_t now = 0;
  struct tm timeinfo = {0};

  time(&now);

  // Check if time is valid (after 2020)
  if (now < 1577836800) {
    snprintf(buffer, buffer_size, "Time not set");
    return false;
  }

  localtime_r(&now, &timeinfo);

  if (strftime(buffer, buffer_size, format, &timeinfo) == 0) {
    return false;
  }

  return true;
}
