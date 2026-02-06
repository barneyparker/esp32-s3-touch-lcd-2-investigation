#include "websocket_client.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "amazon_root_ca.h"
#include <string.h>
#include <time.h>

static const char *TAG = "websocket_client";

// WebSocket configuration
#define WS_URI "wss://steps-ws.barneyparker.com/"
#define WS_RECONNECT_TIMEOUT_MS 5000
#define WS_PING_INTERVAL_SEC 10
#define WS_MAX_RETRY_COUNT 10

// WebSocket client handle
static esp_websocket_client_handle_t client = NULL;
static ws_state_t current_state = WS_STATE_DISCONNECTED;
static bool initialized = false;

/**
 * @brief WebSocket event handler
 */
static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                   int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            current_state = WS_STATE_CONNECTED;
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected");
            current_state = WS_STATE_DISCONNECTED;
            break;

        case WEBSOCKET_EVENT_DATA:
            ESP_LOGI(TAG, "Received data: %.*s", data->data_len, (char *)data->data_ptr);
            // Handle server responses here if needed
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            current_state = WS_STATE_ERROR;
            break;

        default:
            break;
    }
}

esp_err_t websocket_client_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "WebSocket client already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WebSocket client");

    esp_websocket_client_config_t ws_cfg = {
        .uri = WS_URI,
        .reconnect_timeout_ms = WS_RECONNECT_TIMEOUT_MS,
        .network_timeout_ms = 10000,
        .ping_interval_sec = WS_PING_INTERVAL_SEC,
        .cert_pem = amazon_root_ca,
        .skip_cert_common_name_check = false,
    };

    client = esp_websocket_client_init(&ws_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                                   websocket_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WebSocket events: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(client);
        client = NULL;
        return err;
    }

    initialized = true;
    ESP_LOGI(TAG, "WebSocket client initialized successfully");
    return ESP_OK;
}

esp_err_t websocket_client_start(void)
{
    if (!initialized || client == NULL) {
        ESP_LOGE(TAG, "WebSocket client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (current_state == WS_STATE_CONNECTED || current_state == WS_STATE_CONNECTING) {
        ESP_LOGW(TAG, "WebSocket already connected or connecting");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting WebSocket connection to %s", WS_URI);
    current_state = WS_STATE_CONNECTING;

    esp_err_t err = esp_websocket_client_start(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(err));
        current_state = WS_STATE_ERROR;
        return err;
    }

    return ESP_OK;
}

esp_err_t websocket_client_stop(void)
{
    if (!initialized || client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping WebSocket connection");

    esp_err_t err = esp_websocket_client_stop(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop WebSocket client: %s", esp_err_to_name(err));
        return err;
    }

    current_state = WS_STATE_DISCONNECTED;
    return ESP_OK;
}

bool websocket_client_is_connected(void)
{
    return (current_state == WS_STATE_CONNECTED &&
            client != NULL &&
            esp_websocket_client_is_connected(client));
}

ws_state_t websocket_client_get_state(void)
{
    return current_state;
}

esp_err_t websocket_client_send_step(uint32_t step_count, time_t timestamp)
{
    if (!websocket_client_is_connected()) {
        ESP_LOGW(TAG, "Cannot send step data - not connected");
        return ESP_ERR_INVALID_STATE;
    }

    // Create JSON message
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "step");
    cJSON_AddNumberToObject(root, "count", step_count);
    cJSON_AddNumberToObject(root, "timestamp", (double)timestamp);

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to generate JSON string");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sending step data: %s", json_string);

    int sent = esp_websocket_client_send_text(client, json_string, strlen(json_string),
                                               portMAX_DELAY);
    free(json_string);

    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send WebSocket message");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfully sent %d bytes", sent);
    return ESP_OK;
}

esp_websocket_client_handle_t websocket_client_get_handle(void)
{
    return client;
}

esp_err_t websocket_client_deinit(void)
{
    if (!initialized) {
        return ESP_OK;
    }

    if (client != NULL) {
        websocket_client_stop();
        esp_websocket_client_destroy(client);
        client = NULL;
    }

    initialized = false;
    current_state = WS_STATE_DISCONNECTED;

    ESP_LOGI(TAG, "WebSocket client deinitialized");
    return ESP_OK;
}
