#include "wifi_manager.h"
#include "storage_manager.h"
#include "app_state.h"
#include "captive_portal.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "wifi_mgr";

// WiFi event handler storage
static esp_event_handler_instance_t instance_any_id;
static esp_event_handler_instance_t instance_got_ip;

// WiFi state
static bool wifi_initialized = false;
static bool sta_connected = false;
static SemaphoreHandle_t wifi_mutex = NULL;
static TaskHandle_t scan_task_handle = NULL;
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

// Scan cache
#define MAX_SCAN_CACHE 40
static wifi_scan_result_t scan_cache[MAX_SCAN_CACHE];
static uint8_t scan_cache_count = 0;
static bool scan_cache_valid = false;
static volatile bool scan_done_flag = false;
static SemaphoreHandle_t scan_sem = NULL;

// Configurable connection timing
static uint32_t connect_timeout_ms = 10000; // per-SSID timeout
static uint32_t connect_backoff_ms = 200;   // delay between connection status checks

// Background connect task handle
static TaskHandle_t connect_task_handle = NULL;

// Event handler for WiFi events
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        // Just set flag - cache update happens in separate task
        scan_done_flag = true;
        if (scan_sem) {
            xSemaphoreGive(scan_sem);
        }

        // Notify waiting task if any
        if (scan_task_handle) {
            xTaskNotifyGive(scan_task_handle);
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started");
        esp_wifi_connect();
        app_state_set_wifi(WIFI_STATE_CONNECTING, "", 0);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "WiFi disconnected, reason: %d", disconnected->reason);

        xSemaphoreTake(wifi_mutex, portMAX_DELAY);
        sta_connected = false;
        xSemaphoreGive(wifi_mutex);

        app_state_set_wifi(WIFI_STATE_CONNECTING, "", 0);
        // Retry connection
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IPv4 address: " IPSTR, IP2STR(&event->ip_info.ip));

        // Set DNS server to Cloudflare 1.1.1.1 to ensure name resolution works
        if (sta_netif) {
            esp_netif_dns_info_t dns_info;
            if (inet_pton(AF_INET, "1.1.1.1", &dns_info.ip.u_addr.ip4) == 1) {
                dns_info.ip.type = IPADDR_TYPE_V4;
                esp_err_t dns_ret = esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
                if (dns_ret == ESP_OK) {
                    ESP_LOGI(TAG, "Set DNS server to 1.1.1.1");
                } else {
                    ESP_LOGW(TAG, "Failed to set DNS server: %s", esp_err_to_name(dns_ret));
                }
            } else {
                ESP_LOGW(TAG, "inet_pton failed for DNS string");
            }
        }

        xSemaphoreTake(wifi_mutex, portMAX_DELAY);
        sta_connected = true;
        xSemaphoreGive(wifi_mutex);

        // Get current RSSI
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            app_state_set_wifi(WIFI_STATE_CONNECTED, (const char*)ap_info.ssid, ap_info.rssi);
            ESP_LOGI(TAG, "Connected to %s (RSSI: %d)", ap_info.ssid, ap_info.rssi);
        } else {
            app_state_set_wifi(WIFI_STATE_CONNECTED, "", 0);
        }
    }
}

esp_err_t wifi_manager_init(void) {
    if (wifi_initialized) {
        return ESP_OK;
    }

    // Create mutex
    wifi_mutex = xSemaphoreCreateMutex();
    if (!wifi_mutex) {
        ESP_LOGE(TAG, "Failed to create WiFi mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create scan semaphore
    scan_sem = xSemaphoreCreateBinary();
    if (!scan_sem) {
        ESP_LOGE(TAG, "Failed to create scan semaphore");
        vSemaphoreDelete(wifi_mutex);
        return ESP_ERR_NO_MEM;
    }

    // Initialize TCP/IP stack (must be done before any network operations)
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(wifi_mutex);
        return ret;
    }

    // Create default event loop (required for WiFi and IP events)
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means loop already created, which is OK
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(wifi_mutex);
        return ret;
    }

    // Create network interfaces (must be done before esp_wifi_init)
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(wifi_mutex);
        return ret;
    }

    // Register event handlers
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(ret));
        return ret;
    }

    wifi_initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(void) {
    if (!wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Load stored credentials
    wifi_credential_t credentials[WIFI_MAX_STORED_NETWORKS];
    uint8_t cred_count = 0;

    ESP_LOGI(TAG, "wifi_manager_connect: Starting connection attempt...");
    esp_err_t ret = wifi_manager_get_credentials(credentials, &cred_count);
    if (ret != ESP_OK || cred_count == 0) {
        ESP_LOGW(TAG, "wifi_manager_connect: No stored WiFi credentials found");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "wifi_manager_connect: Found %d stored credential(s)", cred_count);

    // Log stored SSIDs for diagnostics (do not print passwords)
    ESP_LOGI(TAG, "Stored WiFi credentials: count=%d", cred_count);
    for (int i = 0; i < cred_count; i++) {
        ESP_LOGI(TAG, "  [%d] SSID='%s'", i, credentials[i].ssid);
    }

    // Ensure WiFi is in STA mode
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }

    // Try stored credentials in order. For each: set config, start/connect and
    // wait for a brief timeout for a successful connection. Log result per SSID.
    for (int idx = 0; idx < cred_count; idx++) {
        wifi_config_t wifi_config = {
            .sta = {
                .ssid = "",
                .password = "",
                .scan_method = WIFI_FAST_SCAN,
                .bssid_set = false,
                .channel = 0,
                .listen_interval = 0,
                .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
                .threshold.rssi = -127,
                .threshold.authmode = WIFI_AUTH_OPEN,
            },
        };

        strncpy((char*)wifi_config.sta.ssid, credentials[idx].ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, credentials[idx].password, sizeof(wifi_config.sta.password) - 1);

        ESP_LOGI(TAG, "Attempting to connect to SSID [%d]: %s", idx, credentials[idx].ssid);

        ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set WiFi config for %s: %s", credentials[idx].ssid, esp_err_to_name(ret));
            continue;
        }

        // Start WiFi if not already started
        ret = esp_wifi_start();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
            ESP_LOGE(TAG, "Failed to start WiFi for %s: %s", credentials[idx].ssid, esp_err_to_name(ret));
            continue;
        }

        // Ask driver to connect
        ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect returned %s for %s", esp_err_to_name(ret), credentials[idx].ssid);
        }

        // Wait up to configured timeout for connection to be reflected in wifi_manager_is_connected()
        const TickType_t timeout = pdMS_TO_TICKS(connect_timeout_ms);
        TickType_t start = xTaskGetTickCount();
        bool connected = false;
        while ((xTaskGetTickCount() - start) < timeout) {
            if (wifi_manager_is_connected()) {
                connected = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(connect_backoff_ms));
        }

        if (connected) {
            ESP_LOGI(TAG, "Connected to SSID '%s'", credentials[idx].ssid);
            // app_state is updated by event handler; return success
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Failed to connect to SSID '%s' within timeout, trying next", credentials[idx].ssid);

        // Stop WiFi before next attempt to ensure clean state
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(connect_backoff_ms));
    }

    ESP_LOGW(TAG, "All stored WiFi credentials attempted and failed");
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_list_credentials(void) {
    wifi_credential_t credentials[WIFI_MAX_STORED_NETWORKS];
    uint8_t cred_count = 0;
    esp_err_t ret = wifi_manager_get_credentials(credentials, &cred_count);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "wifi_manager_list_credentials: failed to read storage: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Stored WiFi credentials: count=%d", cred_count);
    for (int i = 0; i < cred_count; i++) {
        ESP_LOGI(TAG, "  [%d] SSID='%s'", i, credentials[i].ssid);
    }
    return ESP_OK;
}

static void wifi_connect_task(void *arg) {
    ESP_LOGI(TAG, "wifi_connect_task: background connect task started");

    // Reuse the synchronous connect logic but run in background
    esp_err_t res = wifi_manager_connect();
    if (res == ESP_OK) {
        ESP_LOGI(TAG, "wifi_connect_task: ✓ Connected successfully to WiFi");
    } else {
        ESP_LOGW(TAG, "wifi_connect_task: ✗ Connection failed with status %s", esp_err_to_name(res));

        // If we couldn't connect to any stored networks, start captive portal
        if (res == ESP_ERR_TIMEOUT || res == ESP_ERR_NOT_FOUND) {
            ESP_LOGI(TAG, "wifi_connect_task: Starting captive portal for WiFi setup...");

            if (captive_portal_start() == ESP_OK) {
                ESP_LOGI(TAG, "wifi_connect_task: ✓ Captive portal started");
                app_state_set_wifi(WIFI_STATE_AP_MODE, "ESP32-Setup", 0);
            } else {
                ESP_LOGE(TAG, "wifi_connect_task: ✗ Failed to start captive portal");
            }
        }
    }

    // Clear handle and delete task
    connect_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t wifi_manager_connect_async(void) {
    if (!wifi_initialized) return ESP_ERR_INVALID_STATE;

    if (connect_task_handle != NULL) {
        ESP_LOGW(TAG, "wifi_manager_connect_async: connect task already running");
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ok = xTaskCreate(&wifi_connect_task, "wifi_connect_task", 4096, NULL, tskIDLE_PRIORITY + 5, &connect_task_handle);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "wifi_manager_connect_async: failed to create connect task");
        connect_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t wifi_manager_set_connect_timeout(uint32_t ms) {
    if (ms == 0) return ESP_ERR_INVALID_ARG;
    connect_timeout_ms = ms;
    ESP_LOGI(TAG, "wifi_manager: set connect_timeout_ms=%u", connect_timeout_ms);
    return ESP_OK;
}

esp_err_t wifi_manager_set_connect_backoff(uint32_t ms) {
    if (ms == 0) return ESP_ERR_INVALID_ARG;
    connect_backoff_ms = ms;
    ESP_LOGI(TAG, "wifi_manager: set connect_backoff_ms=%u", connect_backoff_ms);
    return ESP_OK;
}

esp_err_t wifi_manager_disconnect(void) {
    if (!wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(wifi_mutex, portMAX_DELAY);
    sta_connected = false;
    xSemaphoreGive(wifi_mutex);

    app_state_set_wifi(WIFI_STATE_DISCONNECTED, "", 0);
    return esp_wifi_stop();
}

bool wifi_manager_is_connected(void) {
    if (!wifi_mutex) return false;

    xSemaphoreTake(wifi_mutex, portMAX_DELAY);
    bool connected = sta_connected;
    xSemaphoreGive(wifi_mutex);

    return connected;
}

int8_t wifi_manager_get_rssi(void) {
    if (!wifi_manager_is_connected()) {
        return 0;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

static void initial_scan_task(void *arg) {
    ESP_LOGI(TAG, "Initial scan task: starting WiFi scan...");

    // Small delay to ensure AP is fully started
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Just trigger the scan - results will be cached by event handler
    wifi_scan_config_t scan_config = {
        .show_hidden = true,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Initial scan failed to start: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Initial scan started, waiting for completion...");

    // Wait for scan to complete
    if (xSemaphoreTake(scan_sem, pdMS_TO_TICKS(10000)) == pdTRUE) {
        // Scan completed, now update cache in this task context (not event handler)
        ESP_LOGI(TAG, "Scan done, updating cache...");

        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);

        if (ap_count > 0) {
            if (ap_count > MAX_SCAN_CACHE) {
                ap_count = MAX_SCAN_CACHE;
            }

            wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
            if (ap_records) {
                esp_wifi_scan_get_ap_records(&ap_count, ap_records);

                // Update cache with mutex protection
                xSemaphoreTake(wifi_mutex, portMAX_DELAY);
                for (int i = 0; i < ap_count; i++) {
                    memset(scan_cache[i].ssid, 0, sizeof(scan_cache[i].ssid));
                    strncpy(scan_cache[i].ssid, (const char*)ap_records[i].ssid, WIFI_SSID_MAX_LEN);
                    scan_cache[i].ssid[WIFI_SSID_MAX_LEN] = '\0';  // Force null termination
                    scan_cache[i].rssi = ap_records[i].rssi;
                    scan_cache[i].authmode = ap_records[i].authmode;
                }
                scan_cache_count = ap_count;
                scan_cache_valid = true;
                xSemaphoreGive(wifi_mutex);

                ESP_LOGI(TAG, "✓ Scan cache updated with %d networks", ap_count);

                free(ap_records);
            } else {
                ESP_LOGE(TAG, "Failed to allocate memory for scan results");
            }
        } else {
            ESP_LOGW(TAG, "Scan completed but found no networks");
        }
    } else {
        ESP_LOGW(TAG, "Scan timeout");
    }

    // Task deletes itself
    vTaskDelete(NULL);
}

esp_err_t wifi_manager_start_ap(const char* ssid, const char* password) {
    if (!wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wifi_config = {
        .ap = {
            .max_connection = 4,
            .authmode = (password && strlen(password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
            .channel = 1,
        },
    };

    strncpy((char*)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);

    if (password && strlen(password) > 0) {
        strncpy((char*)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
    } else {
        wifi_config.ap.password[0] = '\0';
        ESP_LOGI(TAG, "AP configured as open network (no password)");
    }

    // Use APSTA mode so we can scan for networks while the AP is running
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set APSTA mode: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WiFi set to APSTA mode (AP + Station)");

    // Set hostname for both STA and AP interfaces
    if (sta_netif) {
        esp_netif_set_hostname(sta_netif, "Stepper");
    }
    if (ap_netif) {
        esp_netif_set_hostname(ap_netif, "Stepper");
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AP mode: %s", esp_err_to_name(ret));
        return ret;
    }

    app_state_set_wifi(WIFI_STATE_AP_MODE, ssid, 0);
    ESP_LOGI(TAG, "AP mode started: %s", ssid);

    // Start async scan task to populate cache for captive portal
    // This avoids stack overflow in main task
    ESP_LOGI(TAG, "Starting background WiFi scan for captive portal...");
    xTaskCreate(initial_scan_task, "initial_scan", 4096, NULL, 5, NULL);

    return ESP_OK;
}

esp_err_t wifi_manager_stop_ap(void) {
    if (!wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_wifi_stop();
}

bool wifi_manager_is_ap_active(void) {
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        return false;
    }
    return (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
}

esp_err_t wifi_manager_scan(wifi_scan_result_t* results, uint8_t max_results, uint8_t* num_found) {
    if (!wifi_initialized || !results || !num_found) {
        ESP_LOGE(TAG, "Scan called with invalid args");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting WiFi scan...");

    // Store current task handle for notification
    scan_task_handle = xTaskGetCurrentTaskHandle();

    wifi_scan_config_t scan_config = {
        .show_hidden = true,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);  // false = non-blocking
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(ret));
        scan_task_handle = NULL;
        return ret;
    }

    // Wait for scan to complete (with 10 second timeout)
    ESP_LOGI(TAG, "Waiting for scan completion...");
    uint32_t notification = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));
    scan_task_handle = NULL;

    if (notification == 0) {
        ESP_LOGE(TAG, "Scan timeout");
        return ESP_ERR_TIMEOUT;
    }

    // Results are already in cache from event handler
    // Just copy them to output
    uint8_t count = (scan_cache_count > max_results) ? max_results : scan_cache_count;
    memcpy(results, scan_cache, count * sizeof(wifi_scan_result_t));
    *num_found = count;

    ESP_LOGI(TAG, "✓ Returning %d cached scan results", count);
    for (int i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  [%d] SSID: %s, RSSI: %d dBm, Auth: %d",
                 i, results[i].ssid, results[i].rssi, results[i].authmode);
    }

    return ESP_OK;
}

esp_err_t wifi_manager_get_cached_scan(wifi_scan_result_t* results, uint8_t max_results, uint8_t* num_found) {
    if (!results || !num_found) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(wifi_mutex, portMAX_DELAY);

    if (!scan_cache_valid) {
        xSemaphoreGive(wifi_mutex);
        ESP_LOGI(TAG, "No cached scan results available");
        *num_found = 0;
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t count = (scan_cache_count > max_results) ? max_results : scan_cache_count;
    memcpy(results, scan_cache, count * sizeof(wifi_scan_result_t));
    *num_found = count;

    xSemaphoreGive(wifi_mutex);

    ESP_LOGI(TAG, "Returning %d cached scan results", count);
    return ESP_OK;
}

esp_err_t wifi_manager_save_credential(const wifi_credential_t* cred) {
    if (!cred) {
        return ESP_ERR_INVALID_ARG;
    }

    // Get all existing credentials
    wifi_credential_t credentials[WIFI_MAX_STORED_NETWORKS];
    uint8_t count = 0;
    wifi_manager_get_credentials(credentials, &count);

    // Check if SSID already exists
    for (int i = 0; i < count; i++) {
        if (strcmp(credentials[i].ssid, cred->ssid) == 0) {
            // Update existing credential
            memcpy(&credentials[i], cred, sizeof(wifi_credential_t));
            return storage_set_blob("wifi", "credentials", credentials, count * sizeof(wifi_credential_t));
        }
    }

    // Add new credential if space available
    if (count >= WIFI_MAX_STORED_NETWORKS) {
        ESP_LOGW(TAG, "Credential storage full");
        return ESP_ERR_NO_MEM;
    }

    memcpy(&credentials[count], cred, sizeof(wifi_credential_t));
    count++;

    return storage_set_blob("wifi", "credentials", credentials, count * sizeof(wifi_credential_t));
}

esp_err_t wifi_manager_get_credentials(wifi_credential_t* creds, uint8_t* count) {
    if (!creds || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[WIFI_MAX_STORED_NETWORKS * sizeof(wifi_credential_t)];
    size_t blob_size = sizeof(buffer);

    esp_err_t ret = storage_get_blob("wifi", "credentials", buffer, &blob_size);
    if (ret != ESP_OK) {
        *count = 0;
        return (ret == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : ret;
    }

    *count = blob_size / sizeof(wifi_credential_t);
    if (*count > WIFI_MAX_STORED_NETWORKS) {
        *count = WIFI_MAX_STORED_NETWORKS;
    }

    memcpy(creds, buffer, *count * sizeof(wifi_credential_t));
    return ESP_OK;
}

esp_err_t wifi_manager_delete_credential(const char* ssid) {
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_credential_t credentials[WIFI_MAX_STORED_NETWORKS];
    uint8_t count = 0;

    esp_err_t ret = wifi_manager_get_credentials(credentials, &count);
    if (ret != ESP_OK) {
        return ret;
    }

    // Find and remove credential
    int found_idx = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(credentials[i].ssid, ssid) == 0) {
            found_idx = i;
            break;
        }
    }

    if (found_idx == -1) {
        return ESP_ERR_NOT_FOUND;
    }

    // Shift remaining credentials
    for (int i = found_idx; i < count - 1; i++) {
        memcpy(&credentials[i], &credentials[i + 1], sizeof(wifi_credential_t));
    }
    count--;

    if (count == 0) {
        // Delete the key entirely
        return storage_delete("wifi", "credentials");
    }

    return storage_set_blob("wifi", "credentials", credentials, count * sizeof(wifi_credential_t));
}

esp_err_t wifi_manager_clear_credentials(void) {
    return storage_delete("wifi", "credentials");
}

void wifi_manager_power_off(void) {
    if (wifi_initialized) {
        esp_wifi_stop();
    }
}

void wifi_manager_power_on(void) {
    if (wifi_initialized) {
        esp_wifi_start();
    }
}

bool wifi_manager_is_powered(void) {
    wifi_mode_t mode;
    return (esp_wifi_get_mode(&mode) == ESP_OK);
}
