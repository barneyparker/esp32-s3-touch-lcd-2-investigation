#include "ntp_sync.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "app_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network/wifi_manager.h"
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>

static const char *TAG = "ntp_sync";

// NTP state
static bool ntp_synced = false;
static time_t epoch_base = 0;

// SNTP event callback
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized via NTP");
    time_t now = tv->tv_sec;
    epoch_base = now;
    ntp_synced = true;

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "Current time: %04d-%02d-%02d %02d:%02d:%02d",
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    );

    app_state_set_time(true, now);
}

esp_err_t ntp_sync_init(void)
{
    if (ntp_synced) {
        return ESP_OK;  // Already synced
    }

    ESP_LOGI(TAG, "Initializing SNTP");

    // Servers to try (in order)
    const char* servers[] = { "pool.ntp.org", "time.nist.gov", "time.google.com", "time.cloudflare.com" };
    const int server_count = sizeof(servers) / sizeof(servers[0]);

    // DNS-resolve servers for diagnostics
    for (int i = 0; i < server_count; ++i) {
        struct addrinfo hints = {0};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        struct addrinfo *res = NULL;
        int rc = getaddrinfo(servers[i], "", &hints, &res);
        if (rc == 0 && res) {
            char host[128] = {0};
            void *addrptr = NULL;
            if (res->ai_family == AF_INET) {
                struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
                addrptr = &sa->sin_addr;
            } else if (res->ai_family == AF_INET6) {
                struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)res->ai_addr;
                addrptr = &sa6->sin6_addr;
            }
            if (addrptr) {
                inet_ntop(res->ai_family, addrptr, host, sizeof(host));
                ESP_LOGI(TAG, "NTP server '%s' resolved to %s", servers[i], host);
            } else {
                ESP_LOGI(TAG, "NTP server '%s' resolved (addr family %d)", servers[i], res->ai_family);
            }
            freeaddrinfo(res);
        } else {
            ESP_LOGW(TAG, "Failed to resolve NTP server '%s' (getaddrinfo=%d)", servers[i], rc);
        }
    }

    // Configure SNTP servers (use the first two by default, set others as additional)
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    for (int i = 0; i < server_count; ++i) {
        esp_sntp_setservername(i, servers[i]);
    }

    // Set callback for time synchronization
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    // Start SNTP
    esp_sntp_init();

    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "Warning: WiFi not connected yet — SNTP will retry when network is available");
    }

    ESP_LOGI(TAG, "SNTP initialized, waiting for synchronization...");
    return ESP_OK;
}

bool ntp_sync_get_time(time_t *out_time)
{
    if (!out_time) {
        return false;
    }

    if (!ntp_synced) {
        return false;
    }

    time_t now = time(NULL);
    if (now > epoch_base) {
        *out_time = now;
        return true;
    }

    return false;
}

bool ntp_sync_is_synced(void)
{
    return ntp_synced;
}

esp_err_t ntp_sync_wait_for_sync(TickType_t timeout_ms)
{
    TickType_t start_time = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (!ntp_synced) {
        vTaskDelay(pdMS_TO_TICKS(100));

        if (xTaskGetTickCount() - start_time > timeout_ticks) {
            ESP_LOGW(TAG, "NTP synchronization timeout");
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_OK;
}
