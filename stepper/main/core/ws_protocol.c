#include "ws_protocol.h"
#include "cJSON.h"
#include "../app_state.h"
#include "esp_log.h"

static const char *TAG = "ws_protocol";

/**
 * Format: {"action":"sendStep","data":{"sent_at":TIMESTAMP,"deviceMAC":"XX:XX:XX:XX:XX:XX"}}
 * sent_at format: seconds.milliseconds (e.g., 1234567890.123)
 */
char *ws_protocol_create_step_message(uint64_t timestamp_ms, const char *device_mac)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON root");
        return NULL;
    }

    // Add action field
    if (!cJSON_AddStringToObject(root, "action", "sendStep")) {
        cJSON_Delete(root);
        return NULL;
    }

    // Create data object
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "data", data);

    // Add device MAC
    if (!cJSON_AddStringToObject(data, "deviceMAC", device_mac)) {
        cJSON_Delete(root);
        return NULL;
    }

    // Add sent_at as a string with millisecond precision
    char timestamp_str[32];
    uint64_t seconds = timestamp_ms / 1000;
    uint32_t millis = timestamp_ms % 1000;
    snprintf(timestamp_str, sizeof(timestamp_str), "%llu.%03lu", seconds, (unsigned long)millis);

    if (!cJSON_AddStringToObject(data, "sent_at", timestamp_str)) {
        cJSON_Delete(root);
        return NULL;
    }

    // Convert to string
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_str;
}

/**
 * Parse incoming WebSocket text message
 * Currently just logs the message - server messages not yet fully specified
 */
void ws_protocol_handle_message(const char *message, size_t length)
{
    if (!message || length == 0) {
        ESP_LOGW(TAG, "Empty message received");
        return;
    }

    // Try to parse as JSON
    cJSON *root = cJSON_Parse(message);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON message");
        return;
    }

    // Check for command field
    cJSON *command = cJSON_GetObjectItem(root, "command");
    if (command && command->valuestring) {
        ESP_LOGI(TAG, "Received command: %s", command->valuestring);

        // Handle specific commands if needed
        if (strcmp(command->valuestring, "reset") == 0) {
            ESP_LOGI(TAG, "Reset command received");
            // Could reset step counter here if needed
        }
    }

    cJSON_Delete(root);
}

/**
 * Get size of WebSocket frame header for given payload length
 * Returns the number of bytes needed for the frame header
 */
size_t ws_protocol_get_frame_header_size(size_t payload_length)
{
    if (payload_length < 126) {
        return 2; // FIN + opcode + length
    } else if (payload_length < 65536) {
        return 4; // FIN + opcode + extended 16-bit length
    } else {
        return 10; // FIN + opcode + extended 64-bit length
    }
}

/**
 * Create a WebSocket text frame with proper masking (client to server)
 * Returns allocated buffer with complete frame, or NULL on error
 * Caller must free the returned buffer
 *
 * Frame format: [opcode + length][masking key][masked payload]
 */
uint8_t *ws_protocol_create_text_frame(const char *message, size_t *frame_length)
{
    if (!message || !frame_length) {
        return NULL;
    }

    size_t payload_len = strlen(message);
    size_t header_size = ws_protocol_get_frame_header_size(payload_len);

    // Total size: header + 4 byte mask + payload
    size_t total_size = header_size + 4 + payload_len;

    uint8_t *frame = malloc(total_size);
    if (!frame) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer");
        return NULL;
    }

    size_t pos = 0;

    // Byte 0: FIN (1) + RSV (0,0,0) + Opcode (0001 = text)
    frame[pos++] = 0x81;

    // Payload length with MASK bit set (required for client->server)
    if (payload_len < 126) {
        frame[pos++] = 0x80 | payload_len;
    } else if (payload_len < 65536) {
        frame[pos++] = 0xFE; // 0x80 | 126
        frame[pos++] = (payload_len >> 8) & 0xFF;
        frame[pos++] = payload_len & 0xFF;
    } else {
        frame[pos++] = 0xFF; // 0x80 | 127
        for (int i = 7; i >= 0; i--) {
            frame[pos++] = (payload_len >> (i * 8)) & 0xFF;
        }
    }

    // Masking key (4 random bytes)
    // NOTE: In a real implementation, this should use a proper PRNG
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) {
        mask[i] = (uint8_t)rand();
    }

    for (int i = 0; i < 4; i++) {
        frame[pos++] = mask[i];
    }

    // Masked payload
    for (size_t i = 0; i < payload_len; i++) {
        frame[pos++] = message[i] ^ mask[i % 4];
    }

    *frame_length = total_size;
    return frame;
}

/**
 * Parse a WebSocket frame header to get opcode and payload length
 * Returns 0 on success, -1 on error
 */
int ws_protocol_parse_frame_header(const uint8_t *header, size_t header_len,
                                    uint8_t *opcode, size_t *payload_length,
                                    size_t *header_size)
{
    if (!header || header_len < 2) {
        return -1;
    }

    // Extract opcode (lower 4 bits of first byte)
    *opcode = header[0] & 0x0F;

    // Extract payload length (lower 7 bits of second byte)
    uint8_t len_code = header[1] & 0x7F;
    size_t pos = 2;

    if (len_code < 126) {
        *payload_length = len_code;
    } else if (len_code == 126) {
        if (header_len < 4) {
            return -1;
        }
        *payload_length = ((size_t)header[2] << 8) | header[3];
        pos = 4;
    } else { // len_code == 127
        if (header_len < 10) {
            return -1;
        }
        *payload_length = 0;
        for (int i = 0; i < 8; i++) {
            *payload_length = (*payload_length << 8) | header[2 + i];
        }
        pos = 10;
    }

    *header_size = pos;
    return 0;
}
