#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "esp_websocket_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WebSocket client connection state
 */
typedef enum {
    WS_STATE_DISCONNECTED = 0,
    WS_STATE_CONNECTING,
    WS_STATE_CONNECTED,
    WS_STATE_ERROR
} ws_state_t;

/**
 * @brief Initialize WebSocket client
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t websocket_client_init(void);

/**
 * @brief Start WebSocket connection
 *
 * Connects to the WebSocket server. This is non-blocking.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t websocket_client_start(void);

/**
 * @brief Stop WebSocket connection
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t websocket_client_stop(void);

/**
 * @brief Check if WebSocket is connected
 *
 * @return true if connected, false otherwise
 */
bool websocket_client_is_connected(void);

/**
 * @brief Get current connection state
 *
 * @return Current WebSocket state
 */
ws_state_t websocket_client_get_state(void);

/**
 * @brief Send step data to server
 *
 * @param step_count Current step count
 * @param timestamp Unix timestamp of the step
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t websocket_client_send_step(uint32_t step_count, time_t timestamp);

/**
 * @brief Get WebSocket client handle
 *
 * @return WebSocket client handle or NULL if not initialized
 */
esp_websocket_client_handle_t websocket_client_get_handle(void);

/**
 * @brief Deinitialize WebSocket client
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t websocket_client_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_CLIENT_H
