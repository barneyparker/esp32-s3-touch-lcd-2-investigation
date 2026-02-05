#include "app_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "app_state";

// Maximum number of state change callbacks
#define MAX_STATE_CALLBACKS 5

typedef struct {
    state_change_cb_t cb;
    void* user_data;
} state_callback_t;

static app_state_t current_state = {0};
static SemaphoreHandle_t state_mutex = NULL;
static state_callback_t callbacks[MAX_STATE_CALLBACKS] = {0};
static uint8_t callback_count = 0;

void app_state_init(void) {
    // Create mutex for thread-safe access
    state_mutex = xSemaphoreCreateRecursiveMutex();
    if (state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return;
    }

    // Initialize state with defaults
    memset(&current_state, 0, sizeof(app_state_t));
    current_state.wifi_state = WIFI_STATE_DISCONNECTED;
    current_state.ws_state = WS_STATE_DISCONNECTED;
    current_state.battery_percent = 100;
    current_state.current_screen = SCREEN_STEP_MODE;

    ESP_LOGI(TAG, "Application state initialized");
}

static void notify_callbacks(void) {
    for (uint8_t i = 0; i < callback_count; i++) {
        if (callbacks[i].cb != NULL) {
            callbacks[i].cb(&current_state, callbacks[i].user_data);
        }
    }
}

void app_state_get(app_state_t* out_state) {
    if (out_state == NULL || state_mutex == NULL) {
        return;
    }

    xSemaphoreTakeRecursive(state_mutex, portMAX_DELAY);
    memcpy(out_state, &current_state, sizeof(app_state_t));
    xSemaphoreGiveRecursive(state_mutex);
}

void app_state_set_wifi(wifi_state_t state, const char* ssid, int8_t rssi) {
    if (state_mutex == NULL) {
        return;
    }

    xSemaphoreTakeRecursive(state_mutex, portMAX_DELAY);

    bool changed = (current_state.wifi_state != state) ||
                   (rssi != current_state.wifi_rssi);

    current_state.wifi_state = state;
    current_state.wifi_rssi = rssi;

    if (ssid != NULL) {
        strncpy(current_state.wifi_ssid, ssid, sizeof(current_state.wifi_ssid) - 1);
        current_state.wifi_ssid[sizeof(current_state.wifi_ssid) - 1] = '\0';
    }

    if (changed) {
        notify_callbacks();
    }

    xSemaphoreGiveRecursive(state_mutex);
}

void app_state_set_ws(ws_state_t state) {
    if (state_mutex == NULL) {
        return;
    }

    xSemaphoreTakeRecursive(state_mutex, portMAX_DELAY);

    if (current_state.ws_state != state) {
        current_state.ws_state = state;
        notify_callbacks();
    }

    xSemaphoreGiveRecursive(state_mutex);
}

void app_state_set_steps(uint32_t count, uint8_t backlog) {
    if (state_mutex == NULL) {
        return;
    }

    xSemaphoreTakeRecursive(state_mutex, portMAX_DELAY);

    bool changed = (current_state.step_count != count) ||
                   (current_state.backlog_size != backlog);

    current_state.step_count = count;
    current_state.backlog_size = backlog;

    if (changed) {
        notify_callbacks();
    }

    xSemaphoreGiveRecursive(state_mutex);
}

void app_state_set_time(bool synced, time_t time) {
    if (state_mutex == NULL) {
        return;
    }

    xSemaphoreTakeRecursive(state_mutex, portMAX_DELAY);

    if (current_state.time_synced != synced || current_state.current_time != time) {
        current_state.time_synced = synced;
        current_state.current_time = time;
        notify_callbacks();
    }

    xSemaphoreGiveRecursive(state_mutex);
}

void app_state_set_battery(uint8_t percent, bool charging) {
    if (state_mutex == NULL) {
        return;
    }

    xSemaphoreTakeRecursive(state_mutex, portMAX_DELAY);

    if (current_state.battery_percent != percent ||
        current_state.battery_charging != charging) {
        current_state.battery_percent = percent;
        current_state.battery_charging = charging;
        notify_callbacks();
    }

    xSemaphoreGiveRecursive(state_mutex);
}

void app_state_set_ota(ota_state_t state, uint8_t progress) {
    if (state_mutex == NULL) {
        return;
    }

    xSemaphoreTakeRecursive(state_mutex, portMAX_DELAY);

    if (current_state.ota_state != state || current_state.ota_progress != progress) {
        current_state.ota_state = state;
        current_state.ota_progress = progress;
        notify_callbacks();
    }

    xSemaphoreGiveRecursive(state_mutex);
}

void app_state_set_screen(screen_id_t screen) {
    if (state_mutex == NULL) {
        return;
    }

    xSemaphoreTakeRecursive(state_mutex, portMAX_DELAY);

    if (current_state.current_screen != screen) {
        current_state.current_screen = screen;
        notify_callbacks();
    }

    xSemaphoreGiveRecursive(state_mutex);
}

void app_state_register_callback(state_change_cb_t cb, void* user_data) {
    if (cb == NULL || callback_count >= MAX_STATE_CALLBACKS) {
        ESP_LOGW(TAG, "Cannot register callback (count: %d, max: %d)",
                 callback_count, MAX_STATE_CALLBACKS);
        return;
    }

    callbacks[callback_count].cb = cb;
    callbacks[callback_count].user_data = user_data;
    callback_count++;

    ESP_LOGD(TAG, "Registered state callback (%d total)", callback_count);
}
