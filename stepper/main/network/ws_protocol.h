#ifndef WS_PROTOCOL_H
#define WS_PROTOCOL_H

#include "esp_err.h"
#include <cJSON.h>
#include <time.h>

/**
 * @brief Step data message for WebSocket transmission
 */
typedef struct {
    uint32_t step_count;      // Total step count
    time_t timestamp;         // When step was detected (Unix time)
    uint32_t backlog_size;    // Number of pending steps
} ws_step_message_t;

/**
 * @brief Create a step count JSON message
 *
 * Message format:
 * {
 *   "type": "step",
 *   "data": {
 *     "count": 1234,
 *     "timestamp": 1738759200,
 *     "backlog": 0
 *   }
 * }
 */
cJSON* ws_protocol_create_step_message(const ws_step_message_t* step);

/**
 * @brief Create a status JSON message
 *
 * Message format:
 * {
 *   "type": "status",
 *   "data": {
 *     "battery": 85,
 *     "charging": false,
 *     "wifi_rssi": -45,
 *     "backlog": 0
 *   }
 * }
 */
cJSON* ws_protocol_create_status_message(
    uint8_t battery_level,
    bool charging,
    int8_t wifi_rssi,
    uint32_t backlog_size
);

/**
 * @brief Parse incoming WebSocket message
 * Returns NULL if message cannot be parsed
 */
cJSON* ws_protocol_parse_message(const char* data, size_t len);

/**
 * @brief Extract message type from parsed JSON
 */
const char* ws_protocol_get_message_type(cJSON* json);

/**
 * @brief Free parsed message
 */
void ws_protocol_free_message(cJSON* json);

#endif
