#include "websocket_client.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "app_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cJSON.h>
#include <string.h>

static const char *TAG = "ws_client";

// WebSocket client handle
static esp_websocket_client_handle_t client = NULL;
static bool ws_connected = false;
static SemaphoreHandle_t ws_mutex = NULL;

// User-provided callbacks
static ws_message_cb_t user_on_message = NULL;
static ws_connect_cb_t user_on_connect = NULL;

// WebSocket event handler
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            xSemaphoreTake(ws_mutex, portMAX_DELAY);
            ws_connected = true;
            xSemaphoreGive(ws_mutex);
            app_state_set_ws(WS_STATE_CONNECTED);
            if (user_on_connect) {
                user_on_connect(true);
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected");
            xSemaphoreTake(ws_mutex, portMAX_DELAY);
            ws_connected = false;
            xSemaphoreGive(ws_mutex);
            app_state_set_ws(WS_STATE_DISCONNECTED);
            if (user_on_connect) {
                user_on_connect(false);
            }
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x1) {  // Text frame
                ESP_LOGI(TAG, "Received WebSocket message (%d bytes): %.*s", data->data_len, (int)((data->data_len < 128) ? data->data_len : 128), (char *)data->data_ptr);
                if (user_on_message) {
                    user_on_message((const char *)data->data_ptr, data->data_len);
                }
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error: %.*s", data->data_len, (char *)data->data_ptr);
            break;

        default:
            break;
    }
}

esp_err_t ws_client_init(const ws_client_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (client != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Create mutex
    ws_mutex = xSemaphoreCreateMutex();
    if (!ws_mutex) {
        ESP_LOGE(TAG, "Failed to create WebSocket mutex");
        return ESP_ERR_NO_MEM;
    }

    // Store callbacks
    user_on_message = config->on_message;
    user_on_connect = config->on_connect;

    // Create WebSocket URI
    char uri[256];
    if (config->ca_cert) {
        snprintf(uri, sizeof(uri), "wss://%s:%d%s", config->host, config->port, config->path);
    } else {
        snprintf(uri, sizeof(uri), "ws://%s:%d%s", config->host, config->port, config->path);
    }

    // Configure WebSocket client
    esp_websocket_client_config_t ws_config = {
        .uri = uri,
        .cert_pem = config->ca_cert,
        .reconnect_timeout_ms = 10000,
        .network_timeout_ms = 10000,
        .ping_interval_sec = 60,
    };

    client = esp_websocket_client_init(&ws_config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create WebSocket client");
        vSemaphoreDelete(ws_mutex);
        return ESP_FAIL;
    }

    // Register event handler
    esp_err_t ret = esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WebSocket events: %s", esp_err_to_name(ret));
        esp_websocket_client_destroy(client);
        client = NULL;
        vSemaphoreDelete(ws_mutex);
        return ret;
    }

    ESP_LOGI(TAG, "WebSocket client initialized (wss://%s:%d%s)", config->host, config->port, config->path);
    return ESP_OK;
}

esp_err_t ws_client_connect(void)
{
    if (!client) {
        ESP_LOGE(TAG, "WebSocket client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (ws_client_is_connected()) {
        ESP_LOGD(TAG, "WebSocket already connected");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting WebSocket connection...");
    app_state_set_ws(WS_STATE_CONNECTING);
    return esp_websocket_client_start(client);
}

esp_err_t ws_client_disconnect(void)
{
    if (!client) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(ws_mutex, portMAX_DELAY);
    ws_connected = false;
    xSemaphoreGive(ws_mutex);

    app_state_set_ws(WS_STATE_DISCONNECTED);
    return esp_websocket_client_close(client, portMAX_DELAY);
}

bool ws_client_is_connected(void)
{
    if (!ws_mutex) return false;

    xSemaphoreTake(ws_mutex, portMAX_DELAY);
    bool connected = ws_connected;
    xSemaphoreGive(ws_mutex);

    return connected;
}

esp_err_t ws_client_send_text(const char *data, size_t len)
{
    if (!client) {
        ESP_LOGW(TAG, "WebSocket client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!ws_client_is_connected()) {
        ESP_LOGW(TAG, "WebSocket not connected, dropping message");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Sending WebSocket message (%d bytes): %.*s", len, (int)((len < 128) ? len : 128), data);

    // Use a shorter timeout and send with FIN bit set (last frame)
    int bytes_sent = esp_websocket_client_send_text(client, data, len, pdMS_TO_TICKS(5000));
    if (bytes_sent < 0) {
        ESP_LOGW(TAG, "Failed to send WebSocket message: send returned %d", bytes_sent);
        return ESP_FAIL;
    } else if (bytes_sent != len) {
        ESP_LOGW(TAG, "Partial send: sent %d/%d bytes", bytes_sent, len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "✓ WebSocket message sent successfully (%d bytes)", bytes_sent);
    return ESP_OK;
}

esp_err_t ws_client_send_json(cJSON *json)
{
    if (!client || !json) {
        return ESP_ERR_INVALID_ARG;
    }

    char *json_str = cJSON_Print(json);
    if (!json_str) {
        return ESP_FAIL;
    }

    esp_err_t ret = ws_client_send_text(json_str, strlen(json_str));
    free(json_str);
    return ret;
}

esp_err_t ws_client_destroy(void)
{
    if (!client) {
        return ESP_OK;
    }

    esp_err_t ret = esp_websocket_client_destroy(client);
    client = NULL;

    if (ws_mutex) {
        vSemaphoreDelete(ws_mutex);
        ws_mutex = NULL;
    }

    ESP_LOGI(TAG, "WebSocket client destroyed");
    return ret;
}
