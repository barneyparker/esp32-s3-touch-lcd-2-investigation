#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdbool.h>
#include <stdint.h>

/* Configuration */
#define WIFI_MAX_STORED_NETWORKS 10
#define WIFI_SSID_MAX_LEN        32
#define WIFI_PASSWORD_MAX_LEN    64

/* WiFi credential storage structure */
typedef struct {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char password[WIFI_PASSWORD_MAX_LEN + 1];
    uint8_t priority;  // 0 = highest priority
} wifi_credential_t;

/* WiFi scan result structure */
typedef struct {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_scan_result_t;

/* Core functions */
esp_err_t wifi_manager_init(void);
bool wifi_manager_is_connected(void);
bool wifi_manager_is_ap_active(void);
bool wifi_manager_is_powered(void);
esp_err_t wifi_manager_connect(void);
esp_err_t wifi_manager_disconnect(void);
esp_err_t wifi_manager_start_ap(const char* ssid, const char* password);
esp_err_t wifi_manager_stop_ap(void);

/* Scanning */
esp_err_t wifi_manager_scan(wifi_scan_result_t* results, uint8_t max_results, uint8_t* num_found);
esp_err_t wifi_manager_get_cached_scan(wifi_scan_result_t* results, uint8_t max_results, uint8_t* num_found);

/* Credential management */
esp_err_t wifi_manager_save_credential(const wifi_credential_t* cred);
esp_err_t wifi_manager_get_credentials(wifi_credential_t* creds, uint8_t* count);
esp_err_t wifi_manager_delete_credential(const char* ssid);
esp_err_t wifi_manager_list_credentials(void);
/* Async connect: start background task that attempts stored SSIDs */
esp_err_t wifi_manager_connect_async(void);

/* Configure connection timeouts (ms) */
esp_err_t wifi_manager_set_connect_timeout(uint32_t ms);
esp_err_t wifi_manager_set_connect_backoff(uint32_t ms);

#endif // WIFI_MANAGER_H
