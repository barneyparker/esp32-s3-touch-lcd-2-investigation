#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

// Module headers
#include "app_state.h"
#include "drivers/display_driver.h"
#include "drivers/battery_monitor.h"
#include "core/storage_manager.h"
#include "core/step_counter.h"
#include "network/wifi_manager.h"
#include "network/captive_portal.h"
#include "network/websocket_client.h"
#include "network/ntp_sync.h"
#include "network/ota_updater.h"
#include "ui/ui_manager.h"
#include "network/amazon_root_ca.h"

static const char* TAG = "main";

/**
 * @brief State change callback - updates UI when state changes
 */
static void on_state_changed(const app_state_t* state, void* user_data) {
    (void)user_data;
    // Update UI based on state changes
    ui_manager_update(state);
}

typedef enum {
    APP_STATE_WAIT_WIFI,      // Waiting for WiFi connection
    APP_STATE_SYNC_TIME,      // Syncing time via NTP
    APP_STATE_CONNECT_WS,     // Connecting to WebSocket
    APP_STATE_RUNNING         // Normal operation
} app_startup_state_t;

static bool ntp_initialized = false;
static bool ws_initialized = false;
static bool ota_initialized = false;

/**
 * @brief Main application loop with sequential startup
 */
static void app_main_loop_full(void) {
    ESP_LOGI(TAG, "Starting main application loop");

    app_startup_state_t startup_state = APP_STATE_WAIT_WIFI;
    app_state_t current_state;
    uint32_t state_entry_time = 0;

    while (1) {
        // Get current application state
        app_state_get(&current_state);

        // Update battery status periodically
        uint8_t battery = battery_monitor_get_level();
        bool charging = battery_monitor_is_charging();
        app_state_set_battery(battery, charging);

        // Check WiFi connectivity
        bool wifi_connected = wifi_manager_is_connected();

        // Sequential startup state machine
        switch (startup_state) {
            case APP_STATE_WAIT_WIFI:
                if (wifi_connected) {
                    ESP_LOGI(TAG, "========================================");
                    ESP_LOGI(TAG, "WiFi Connected - Proceeding to NTP sync");
                    ESP_LOGI(TAG, "========================================");
                    startup_state = APP_STATE_SYNC_TIME;
                    state_entry_time = xTaskGetTickCount();
                }
                break;

            case APP_STATE_SYNC_TIME:
                if (!ntp_initialized) {
                    ESP_LOGI(TAG, "Initializing NTP for time synchronization...");
                    if (ntp_sync_init() == ESP_OK) {
                        ntp_initialized = true;
                    } else {
                        ESP_LOGW(TAG, "Failed to initialize NTP");
                        vTaskDelay(pdMS_TO_TICKS(5000));
                        break;
                    }
                }

                // Check if time is synced (wait up to 30 seconds)
                time_t now = 0;
                time(&now);
                if (now > 1000000000) {  // Reasonable timestamp (after year 2001)
                    ESP_LOGI(TAG, "========================================");
                    ESP_LOGI(TAG, "Time Synchronized - Proceeding to WebSocket");
                    ESP_LOGI(TAG, "========================================");

                    // Initialize OTA now that we have time
                    if (!ota_initialized) {
                        ESP_LOGI(TAG, "Initializing OTA updater...");
                        if (ota_updater_init() == ESP_OK) {
                            ota_initialized = true;
                        }
                    }

                    startup_state = APP_STATE_CONNECT_WS;
                    state_entry_time = xTaskGetTickCount();
                } else if ((xTaskGetTickCount() - state_entry_time) > pdMS_TO_TICKS(30000)) {
                    ESP_LOGW(TAG, "NTP sync timeout - continuing anyway");
                    startup_state = APP_STATE_CONNECT_WS;
                    state_entry_time = xTaskGetTickCount();
                }
                break;

            case APP_STATE_CONNECT_WS: {
                if (!ws_initialized) {
                    ESP_LOGI(TAG, "Initializing WebSocket client...");
                    ws_client_config_t ws_config = {
                        .host = "steps-ws.barneyparker.com",
                        .port = 443,
                        .path = "/",
                        .ca_cert = amazon_root_ca,
                        .on_message = NULL,
                        .on_connect = NULL,
                    };
                    if (ws_client_init(&ws_config) == ESP_OK) {
                        ws_initialized = true;
                        ESP_LOGI(TAG, "Attempting WebSocket connection...");
                        ws_client_connect();
                    } else {
                        ESP_LOGW(TAG, "Failed to initialize WebSocket client");
                        vTaskDelay(pdMS_TO_TICKS(5000));
                        break;
                    }
                }

                // Try to connect WebSocket
                bool ws_conn_status = ws_client_is_connected();
                if (ws_conn_status) {
                    ESP_LOGI(TAG, "========================================");
                    ESP_LOGI(TAG, "WebSocket Connected - Entering normal operation");
                    ESP_LOGI(TAG, "========================================");
                    startup_state = APP_STATE_RUNNING;
                } else if ((xTaskGetTickCount() - state_entry_time) > pdMS_TO_TICKS(10000)) {
                    ESP_LOGI(TAG, "WebSocket connection attempt...");
                    ws_client_connect();
                    state_entry_time = xTaskGetTickCount();
                }
                break;
            }

            case APP_STATE_RUNNING: {
                // Normal operation - monitor connections and send data
                if (!wifi_connected) {
                    ESP_LOGW(TAG, "WiFi disconnected - returning to WAIT_WIFI state");
                    app_state_set_wifi(WIFI_STATE_DISCONNECTED, "", 0);
                    startup_state = APP_STATE_WAIT_WIFI;
                    break;
                }

                // Check WebSocket and reconnect if needed
                bool ws_running_status = ws_client_is_connected();
                if (!ws_running_status) {
                    ESP_LOGI(TAG, "WebSocket disconnected - attempting reconnection...");
                    ws_client_connect();
                }
                app_state_set_ws(ws_running_status ? WS_STATE_CONNECTED : WS_STATE_DISCONNECTED);

                // If WebSocket is connected, flush buffered steps
                if (ws_running_status) {
                    uint8_t backlog_size = step_counter_get_backlog_size();
                    if (backlog_size > 0) {
                        uint32_t timestamp = 0;
                        if (step_counter_get_next_buffered_step(&timestamp) == ESP_OK) {
                            // Send step via WebSocket
                            // Format: {"action":"sendStep","data":{"sent_at":1234567890.123,"deviceMAC":"AA:BB:CC:DD:EE:FF"}}
                            char json[256];
                            uint8_t mac[6];
                            esp_wifi_get_mac(WIFI_IF_STA, mac);

                            snprintf(json, sizeof(json),
                                "{\"action\":\"sendStep\",\"data\":{\"sent_at\":%lu.000,\"deviceMAC\":\"%02X:%02X:%02X:%02X:%02X:%02X\"}}",
                                timestamp,
                                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
                            );

                            if (ws_client_send_text(json, strlen(json)) == ESP_OK) {
                                ESP_LOGI(TAG, "Sent buffered step (timestamp=%lu, remaining=%d)", timestamp, backlog_size - 1);
                                step_counter_remove_first_buffered_step();
                            } else {
                                ESP_LOGW(TAG, "Failed to send buffered step, will retry");
                            }
                        }
                    }
                }

                // Update step count
                uint32_t steps = step_counter_get_count();
                uint8_t current_backlog = step_counter_get_backlog_size();
                app_state_set_steps(steps, current_backlog);
                break;
            }
        }

        // Update WiFi state in app state
        if (wifi_connected) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                app_state_set_wifi(WIFI_STATE_CONNECTED, (const char*)ap_info.ssid, ap_info.rssi);
            }
        } else {
            app_state_set_wifi(WIFI_STATE_DISCONNECTED, "", 0);
        }

        // Yield for other tasks
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*
 * Rename of the original full application entry so we can temporarily
 * run a minimal startup for diagnostics. The original behaviour is
 * preserved in `app_main_full` and `app_main_loop_full`.
 */
void app_main_full(void) {
    ESP_LOGI(TAG, "(FULL) ========================================");
    ESP_LOGI(TAG, "(FULL) Stepper Application Starting");
    ESP_LOGI(TAG, "(FULL) ========================================");

    // Phase 1: Initialize core infrastructure
    ESP_LOGI(TAG, "(FULL) [Phase 1] Initializing core infrastructure...");

    // Initialize storage (NVS)
    if (storage_init() != ESP_OK) {
        ESP_LOGE(TAG, "(FULL) Failed to initialize storage");
        return;
    }

    // Initialize application state
    app_state_init();

    // Initialize display driver (LCD + touch)
    if (display_driver_init() != ESP_OK) {
        ESP_LOGE(TAG, "(FULL) Failed to initialize display");
        return;
    }

    // Initialize battery monitor
    if (battery_monitor_init() != ESP_OK) {
        ESP_LOGW(TAG, "(FULL) Failed to initialize battery monitor");
    }

    // Register for state change notifications
    app_state_register_callback(on_state_changed, NULL);

    // Initialize UI
    if (ui_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "(FULL) Failed to initialize UI");
        return;
    }

    ESP_LOGI(TAG, "(FULL) [Phase 1] ✓ Core infrastructure initialized");

    // Phase 2/3 are intentionally left in the original function.
    ESP_LOGI(TAG, "(FULL) Skipping connectivity initialization in this renamed entry");

    // Run the original main loop (renamed)
    app_main_loop_full();
}

/*
 * Minimal app_main for diagnostic: initialize only the pieces required
 * to bring up the display and LVGL (based on one.c behaviour).
 * This avoids starting WiFi/NTP/WebSocket so we can isolate display issues.
 */
void app_main(void) {
    ESP_LOGI(TAG, "(MIN) Minimal Stepper App Starting (display-only)");

    // Minimal core: storage + app state
    if (storage_init() != ESP_OK) {
        ESP_LOGE(TAG, "(MIN) Failed to initialize storage");
    }
    app_state_init();

    // Initialize display driver only (this will initialize LVGL internals)
    if (display_driver_init() != ESP_OK) {
        ESP_LOGE(TAG, "(MIN) Failed to initialize display driver");
        return;
    }

    // Initialize battery monitor (start background task to update app_state)
    if (battery_monitor_init() != ESP_OK) {
        ESP_LOGW(TAG, "(MIN) battery_monitor_init failed or is in stub mode");
    } else {
        ESP_LOGI(TAG, "(MIN) battery monitor started");
    }

    // Register callback so UI updates still work
    app_state_register_callback(on_state_changed, NULL);

    // Initialize UI (creates the simple test UI)
    if (ui_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "(MIN) Failed to initialize UI manager");
    }

    ESP_LOGI(TAG, "(MIN) Display and UI should be up. Not starting network modules.");

    // Keep the task alive so device doesn't exit
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
