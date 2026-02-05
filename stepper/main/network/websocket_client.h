#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <cJSON.h>

// Callback type definitions
typedef void (*ws_message_cb_t)(const char *data, size_t len);
typedef void (*ws_connect_cb_t)(bool connected);

// WebSocket client configuration
typedef struct {
    const char *host;
    uint16_t port;
    const char *path;
    const char *ca_cert;        // Optional: for TLS connections
    ws_message_cb_t on_message;
    ws_connect_cb_t on_connect;
} ws_client_config_t;

// Initialize the WebSocket client with configuration
esp_err_t ws_client_init(const ws_client_config_t *config);

// Connect to the WebSocket server
esp_err_t ws_client_connect(void);

// Disconnect from the WebSocket server
esp_err_t ws_client_disconnect(void);

// Check if WebSocket is connected
bool ws_client_is_connected(void);

// Send text data
esp_err_t ws_client_send_text(const char *data, size_t len);

// Send JSON data
esp_err_t ws_client_send_json(cJSON *json);

// Destroy the WebSocket client
esp_err_t ws_client_destroy(void);

#endif
