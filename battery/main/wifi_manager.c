#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "wifi_manager";

#define MAX_WIFI_CREDENTIALS 10
#define NVS_NAMESPACE "wifi_creds"
#define AP_SSID "Stepper"
#define AP_PASSWORD "" // Open network
#define MAX_SCAN_RESULTS 20

static bool wifi_connected = false;
static httpd_handle_t server = NULL;

// WiFi credential structure
typedef struct {
    char ssid[33];
    char password[64];
} wifi_credential_t;

static wifi_credential_t stored_credentials[MAX_WIFI_CREDENTIALS];
static int stored_count = 0;

// Cached scan results
static wifi_ap_record_t scan_results[MAX_SCAN_RESULTS];
static uint16_t scan_results_count = 0;

// Event handler for WiFi events
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi station started");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi connected");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WiFi disconnected");
                wifi_connected = false;
                break;
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "Access point started");
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Station connected to AP");
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
    }
}

// Load WiFi credentials from NVS
static int load_credentials_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No stored credentials found");
        return 0;
    }

    size_t count_size = sizeof(int);
    err = nvs_get_blob(handle, "count", &stored_count, &count_size);
    if (err != ESP_OK || stored_count == 0) {
        ESP_LOGW(TAG, "No credential count in NVS");
        nvs_close(handle);
        return 0;
    }

    size_t creds_size = stored_count * sizeof(wifi_credential_t);
    err = nvs_get_blob(handle, "creds", stored_credentials, &creds_size);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read credentials: %s", esp_err_to_name(err));
        return 0;
    }

    ESP_LOGI(TAG, "Loaded %d WiFi credentials", stored_count);
    return stored_count;
}

// Try to connect to a specific WiFi network
static bool try_connect_wifi(const char* ssid, const char* password)
{
    ESP_LOGI(TAG, "Attempting to connect to: %s", ssid);

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    // Wait for connection (with timeout)
    int retry_count = 0;
    while (!wifi_connected && retry_count < 30) { // 15 second timeout
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;
    }

    return wifi_connected;
}

// Scan for available WiFi networks and cache results
static void scan_wifi_networks(void)
{
    ESP_LOGI(TAG, "Scanning for WiFi networks...");

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    // Limit to our array size
    scan_results_count = (ap_count > MAX_SCAN_RESULTS) ? MAX_SCAN_RESULTS : ap_count;

    if (scan_results_count > 0) {
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&scan_results_count, scan_results));
    }

    ESP_LOGI(TAG, "Found %d networks", scan_results_count);
}

// HTML for captive portal
static const char* get_portal_html(void)
{
    return "<!DOCTYPE html>"
           "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Stepper WiFi Setup</title>"
           "<style>body{font-family:Arial;margin:20px;background:#f0f0f0}"
           ".container{max-width:400px;margin:auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
           "h1{color:#333;text-align:center}"
           "select,input{width:100%;padding:10px;margin:10px 0;box-sizing:border-box;border:1px solid #ddd;border-radius:4px}"
           "button{width:100%;padding:12px;background:#4CAF50;color:white;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
           "button:hover{background:#45a049}"
           "</style></head><body>"
           "<div class='container'>"
           "<h1>Stepper Setup</h1>"
           "<p>Select your WiFi network:</p>"
           "<form action='/save' method='post'>"
           "<select name='ssid' id='ssid' required><option value=''>Scanning...</option></select>"
           "<input type='password' name='password' placeholder='Password' required>"
           "<button type='submit'>Connect</button>"
           "</form></div>"
           "<script>"
           "fetch('/scan').then(r=>r.json()).then(data=>{"
           "let select=document.getElementById('ssid');select.innerHTML='';"
           "data.networks.forEach(n=>{let opt=document.createElement('option');opt.value=n.ssid;opt.textContent=`${n.ssid} (${n.rssi}dBm)`;select.appendChild(opt);});"
           "});"
           "</script></body></html>";
}

// HTTP GET handler for root
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char* html = get_portal_html();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

// HTTP GET handler for scan
static esp_err_t scan_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Scan handler: returning %d cached networks", scan_results_count);

    // Build JSON response using cJSON from cached scan results
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    cJSON *networks = cJSON_AddArrayToObject(root, "networks");
    if (networks == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    // Add cached scan results to JSON
    for (int i = 0; i < scan_results_count; i++) {
        cJSON *net = cJSON_CreateObject();
        if (net != NULL) {
            cJSON_AddStringToObject(net, "ssid", (char*)scan_results[i].ssid);
            cJSON_AddNumberToObject(net, "rssi", scan_results[i].rssi);
            cJSON_AddItemToArray(networks, net);
        }
    }

    char *json_str = cJSON_Print(root);
    if (json_str == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_Delete(root);
    free(json_str);
    return ESP_OK;
}

// HTTP POST handler for saving credentials
static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Parse form data (simple parser for ssid=xxx&password=yyy)
    char ssid[33] = {0};
    char password[64] = {0};

    char *ssid_start = strstr(buf, "ssid=");
    char *pass_start = strstr(buf, "password=");

    if (ssid_start && pass_start) {
        ssid_start += 5; // Skip "ssid="
        char *ssid_end = strchr(ssid_start, '&');
        if (ssid_end) {
            int len = ssid_end - ssid_start;
            if (len > 32) len = 32;
            strncpy(ssid, ssid_start, len);
        }

        pass_start += 9; // Skip "password="
        strncpy(password, pass_start, 63);
    }

    ESP_LOGI(TAG, "Saving credentials for: %s", ssid);

    // Save to NVS
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        wifi_credential_t cred;
        strncpy(cred.ssid, ssid, sizeof(cred.ssid) - 1);
        strncpy(cred.password, password, sizeof(cred.password) - 1);

        stored_credentials[0] = cred;
        stored_count = 1;

        nvs_set_blob(handle, "count", &stored_count, sizeof(int));
        nvs_set_blob(handle, "creds", stored_credentials, sizeof(wifi_credential_t));
        nvs_commit(handle);
        nvs_close(handle);
    }

    // Send response
    const char* resp = "<html><body><h1>Credentials saved!</h1><p>Rebooting...</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, strlen(resp));

    // Reboot after a short delay
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

wifi_result_t wifi_manager_init(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Load stored credentials
    int count = load_credentials_from_nvs();
    if (count == 0) {
        return WIFI_RESULT_NO_CREDENTIALS;
    }

    // Try each stored credential
    for (int i = 0; i < stored_count; i++) {
        if (try_connect_wifi(stored_credentials[i].ssid, stored_credentials[i].password)) {
            return WIFI_RESULT_CONNECTED;
        }
    }

    return WIFI_RESULT_FAILED;
}

void wifi_manager_start_ap_mode(void)
{
    ESP_LOGI(TAG, "Starting AP mode: %s", AP_SSID);

    // Stop STA mode and start AP
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Scan for networks after WiFi is started in APSTA mode
    // This ensures scan results are available when the web server starts
    ESP_LOGI(TAG, "Scanning for available WiFi networks...");
    scan_wifi_networks();

    // Start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler
        };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t scan_uri = {
            .uri = "/scan",
            .method = HTTP_GET,
            .handler = scan_get_handler
        };
        httpd_register_uri_handler(server, &scan_uri);

        httpd_uri_t save_uri = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = save_post_handler
        };
        httpd_register_uri_handler(server, &save_uri);

        // Catch-all handler for captive portal (redirect everything to root)
        httpd_uri_t catchall_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = root_get_handler
        };
        httpd_register_uri_handler(server, &catchall_uri);

        ESP_LOGI(TAG, "Web server started");
    }
}

bool wifi_manager_is_connected(void)
{
    return wifi_connected;
}

void wifi_manager_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting WiFi for power saving");
    wifi_connected = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
}

wifi_result_t wifi_manager_reconnect(void)
{
    ESP_LOGI(TAG, "Reconnecting WiFi after power saving");

    // Restart WiFi (it was stopped by wifi_manager_disconnect)
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return WIFI_RESULT_FAILED;
    }

    // Try each stored credential
    for (int i = 0; i < stored_count; i++) {
        if (try_connect_wifi(stored_credentials[i].ssid, stored_credentials[i].password)) {
            return WIFI_RESULT_CONNECTED;
        }
    }

    ESP_LOGW(TAG, "Failed to reconnect to any stored network");
    return WIFI_RESULT_FAILED;
}

int wifi_manager_get_stored_count(void)
{
    return stored_count;
}

bool wifi_manager_get_ap_qr_string(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size < 50) {
        return false;
    }

    // Format: WIFI:T:nopass;S:<SSID>;;\n    // For open network (no password)
    snprintf(buffer, buffer_size, "WIFI:T:nopass;S:%s;;", AP_SSID);
    return true;
}
