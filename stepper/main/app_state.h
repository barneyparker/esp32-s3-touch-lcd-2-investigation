#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING = 1,
    WIFI_STATE_CONNECTED = 2,
    WIFI_STATE_AP_MODE = 3
} wifi_state_t;

typedef enum {
    WS_STATE_DISCONNECTED = 0,
    WS_STATE_CONNECTING = 1,
    WS_STATE_CONNECTED = 2
} ws_state_t;

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECKING = 1,
    OTA_STATE_DOWNLOADING = 2,
    OTA_STATE_INSTALLING = 3,
    OTA_STATE_COMPLETE = 4,
    OTA_STATE_ERROR = 5
} ota_state_t;

typedef enum {
    SCREEN_STEP_MODE = 0,
    SCREEN_SETUP = 1,
    SCREEN_CONNECTING = 2,
    SCREEN_OTA_UPDATE = 3
} screen_id_t;

typedef struct {
    // WiFi state
    wifi_state_t wifi_state;
    char wifi_ssid[33];
    int8_t wifi_rssi;

    // WebSocket state
    ws_state_t ws_state;

    // Step data
    uint32_t step_count;
    uint8_t backlog_size;

    // Time
    bool time_synced;
    time_t current_time;

    // Battery
    uint8_t battery_percent;
    bool battery_charging;

    // OTA
    ota_state_t ota_state;
    uint8_t ota_progress;

    // UI
    screen_id_t current_screen;
} app_state_t;

/**
 * @brief Callback type for state changes
 */
typedef void (*state_change_cb_t)(const app_state_t* state, void* user_data);

/**
 * @brief Initialize application state
 */
void app_state_init(void);

/**
 * @brief Get current state (thread-safe copy)
 */
void app_state_get(app_state_t* out_state);

/**
 * @brief Update WiFi state
 */
void app_state_set_wifi(wifi_state_t state, const char* ssid, int8_t rssi);

/**
 * @brief Update WebSocket state
 */
void app_state_set_ws(ws_state_t state);

/**
 * @brief Update step count and backlog
 */
void app_state_set_steps(uint32_t count, uint8_t backlog);

/**
 * @brief Update time sync status
 */
void app_state_set_time(bool synced, time_t time);

/**
 * @brief Update battery status
 */
void app_state_set_battery(uint8_t percent, bool charging);

/**
 * @brief Update OTA status
 */
void app_state_set_ota(ota_state_t state, uint8_t progress);

/**
 * @brief Switch to different screen
 */
void app_state_set_screen(screen_id_t screen);

/**
 * @brief Register for state change notifications
 */
void app_state_register_callback(state_change_cb_t cb, void* user_data);

#endif // APP_STATE_H
