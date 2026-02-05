#include "captive_portal.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "app_state.h"
#include "cJSON.h"
#include <string.h>
#include <ctype.h>

static const char* TAG = "captive_portal";
static httpd_handle_t server = NULL;

// HTML form for WiFi credential entry
static const char* CAPTIVE_HTML =
"<!DOCTYPE html>"
"<html><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>WiFi Setup</title>"
"<style>"
"body{font-family:Arial,sans-serif;background:#f0f0f0;margin:0;padding:20px;}"
"h1{color:#333;text-align:center;}"
".container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}"
"input,select{width:100%;padding:10px;margin:10px 0;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;}"
"button{width:100%;padding:10px;background:#007bff;color:white;border:none;border-radius:4px;cursor:pointer;font-size:16px;}"
"button:hover{background:#0056b3;}"
".status{margin:10px 0;padding:10px;text-align:center;}"
".error{color:#d32f2f;}"
".success{color:#388e3c;}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>📱 WiFi Setup</h1>"
"<form id='wifiForm'>"
"<label>Available Networks:</label>"
"<select id='networkSelect' name='ssid' required>"
"<option value=''>Scanning...</option>"
"</select>"
"<label>Password:</label>"
"<input type='password' id='password' name='password' required>"
"<button type='submit'>Connect</button>"
"</form>"
"<div id='status' class='status'></div>"
"<script>"
"document.getElementById('wifiForm').onsubmit=function(e){"
"e.preventDefault();"
"const ssid=document.getElementById('networkSelect').value;"
"const pass=document.getElementById('password').value;"
"if(!ssid||!pass)return;"
"const status=document.getElementById('status');"
"status.className='status';"
"status.textContent='Connecting...';"
"fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:pass})})"
".then(r=>r.json())"
".then(d=>{status.className='status '+(d.success?'success':'error');status.textContent=d.message;})"
".catch(e=>{status.className='status error';status.textContent='Error: '+e;});"
"};"
"fetch('/api/scan')"
".then(r=>r.json())"
".then(d=>{"
"const select=document.getElementById('networkSelect');"
"select.innerHTML='';"
"d.networks.forEach(n=>{"
"const o=document.createElement('option');"
"o.value=n.ssid;"
"o.text=n.ssid+' ('+n.rssi+'dBm)';"
"select.appendChild(o);"
"});"
"})"
".catch(e=>console.error('Scan error:',e));"
"</script>"
"</body></html>";

// HTTP handler for root path - return HTML
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, CAPTIVE_HTML, strlen(CAPTIVE_HTML));
    return ESP_OK;
}

// HTTP handler for WiFi scan API
static esp_err_t scan_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Scan API called from client");

    wifi_scan_result_t results[20];
    uint8_t num_found = 0;

    // Try cached results first
    esp_err_t ret = wifi_manager_get_cached_scan(results, 20, &num_found);

    // If no cache or cache failed, do a fresh scan
    if (ret != ESP_OK || num_found == 0) {
        ESP_LOGI(TAG, "No cached results, performing fresh scan...");
        ret = wifi_manager_scan(results, 20, &num_found);
    }

    ESP_LOGI(TAG, "Scan returned: %s, found %d networks", esp_err_to_name(ret), num_found);

    // Build JSON response using cJSON for proper escaping
    cJSON *root = cJSON_CreateObject();
    cJSON *networks_array = cJSON_CreateArray();

    if (ret == ESP_OK && num_found > 0) {
        for (int i = 0; i < num_found; i++) {
            cJSON *network = cJSON_CreateObject();
            cJSON_AddStringToObject(network, "ssid", results[i].ssid);
            cJSON_AddNumberToObject(network, "rssi", results[i].rssi);
            cJSON_AddNumberToObject(network, "auth", results[i].authmode);
            cJSON_AddItemToArray(networks_array, network);
        }
    }

    cJSON_AddItemToObject(root, "networks", networks_array);
    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!response) {
        ESP_LOGE(TAG, "Failed to generate JSON response");
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Sending scan response: %d bytes", (int)strlen(response));

    httpd_resp_set_type(req, "application/json");
    esp_err_t send_ret = httpd_resp_send(req, response, strlen(response));
    cJSON_free(response);

    if (send_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send response: %s", esp_err_to_name(send_ret));
    }

    return ESP_OK;
}

// HTTP handler for WiFi connect API
static esp_err_t connect_handler(httpd_req_t *req)
{
    char buffer[512];
    int total_len = req->content_len;
    int cur_len = 0;

    if (total_len >= sizeof(buffer)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }

    while (cur_len < total_len) {
        int recv_len = httpd_req_recv(req, buffer + cur_len, total_len - cur_len);
        if (recv_len <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read request");
            return ESP_FAIL;
        }
        cur_len += recv_len;
    }
    buffer[total_len] = '\0';

    // Simple JSON parsing (looking for "ssid" and "password")
    const char *ssid_ptr = strstr(buffer, "\"ssid\"");
    const char *pass_ptr = strstr(buffer, "\"password\"");

    char ssid[33] = {0};
    char password[65] = {0};
    bool success = false;

    if (ssid_ptr && pass_ptr) {
        // Extract SSID value
        ssid_ptr = strchr(ssid_ptr, ':');
        if (ssid_ptr) {
            ssid_ptr++;
            while (*ssid_ptr && (*ssid_ptr == ' ' || *ssid_ptr == '\"')) ssid_ptr++;
            int i = 0;
            while (*ssid_ptr && *ssid_ptr != '\"' && i < 32) {
                ssid[i++] = *ssid_ptr++;
            }
        }

        // Extract password value
        pass_ptr = strchr(pass_ptr, ':');
        if (pass_ptr) {
            pass_ptr++;
            while (*pass_ptr && (*pass_ptr == ' ' || *pass_ptr == '\"')) pass_ptr++;
            int i = 0;
            while (*pass_ptr && *pass_ptr != '\"' && i < 64) {
                password[i++] = *pass_ptr++;
            }
        }

        if (strlen(ssid) > 0 && strlen(password) > 0) {
            // Save credential
            wifi_credential_t cred = {.priority = 0};
            strncpy(cred.ssid, ssid, sizeof(cred.ssid) - 1);
            strncpy(cred.password, password, sizeof(cred.password) - 1);

            esp_err_t ret = wifi_manager_save_credential(&cred);
            success = (ret == ESP_OK);

            ESP_LOGI(TAG, "WiFi credential saved: %s", ssid);
        }
    }

    // Send JSON response
    const char *response = success ?
        "{\"success\":true,\"message\":\"Connecting to WiFi...\"}" :
        "{\"success\":false,\"message\":\"Failed to save credentials\"}";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

// HTTP handler for 404 - redirect to root
static esp_err_t notfound_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t captive_portal_start(void) {
    if (server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Start WiFi in AP mode with visible SSID (open network - no password)
    const char* ap_password = "";  // Empty password = open network

    ESP_LOGI(TAG, "Starting WiFi AP mode with SSID: %s (open network)", CAPTIVE_PORTAL_AP_SSID);
    esp_err_t ret = wifi_manager_start_ap(CAPTIVE_PORTAL_AP_SSID, ap_password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi AP: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start HTTP server for captive portal
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.stack_size = 4096;
    config.max_open_sockets = 7;

    ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register URI handlers
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
    };
    httpd_register_uri_handler(server, &root_uri);

    httpd_uri_t scan_uri = {
        .uri = "/api/scan",
        .method = HTTP_GET,
        .handler = scan_handler,
    };
    httpd_register_uri_handler(server, &scan_uri);

    httpd_uri_t connect_uri = {
        .uri = "/api/connect",
        .method = HTTP_POST,
        .handler = connect_handler,
    };
    httpd_register_uri_handler(server, &connect_uri);

    httpd_uri_t notfound_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = notfound_handler,
    };
    httpd_register_uri_handler(server, &notfound_uri);

    ESP_LOGI(TAG, "Captive portal started");
    return ESP_OK;
}

esp_err_t captive_portal_stop(void) {
    if (server == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = httpd_stop(server);
    if (ret == ESP_OK) {
        server = NULL;
    }
    return ret;
}
