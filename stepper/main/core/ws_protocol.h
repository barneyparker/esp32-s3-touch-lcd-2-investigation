#ifndef WS_PROTOCOL_H
#define WS_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * WebSocket Protocol Handler for Step Counter Communication
 *
 * Protocol:
 * - Server: wss://steps-ws.barneyparker.com:443
 * - Frame format: WebSocket RFC 6455 with text frames
 * - Message format: JSON
 *
 * Message Format:
 * {
 *   "action": "sendStep",
 *   "data": {
 *     "sent_at": "1234567890.123",  // seconds.milliseconds as string
 *     "deviceMAC": "AA:BB:CC:DD:EE:FF"
 *   }
 * }
 */

/**
 * Create a step message JSON string
 * @param timestamp_ms Timestamp in milliseconds since epoch
 * @param device_mac MAC address as string (e.g., "AA:BB:CC:DD:EE:FF")
 * @return Allocated JSON string, must be freed by caller. NULL on error.
 */
char *ws_protocol_create_step_message(uint64_t timestamp_ms, const char *device_mac);

/**
 * Handle incoming WebSocket message
 * @param message Message data (not necessarily null-terminated)
 * @param length Message length in bytes
 */
void ws_protocol_handle_message(const char *message, size_t length);

/**
 * Get the size of a WebSocket frame header for given payload length
 * @param payload_length Size of payload
 * @return Size of frame header in bytes (2, 4, or 10)
 */
size_t ws_protocol_get_frame_header_size(size_t payload_length);

/**
 * Create a complete WebSocket text frame with masking
 * Implements RFC 6455 client-to-server frame format
 * @param message Message to send
 * @param frame_length Output: total length of returned frame
 * @return Allocated frame buffer, must be freed by caller. NULL on error.
 */
uint8_t *ws_protocol_create_text_frame(const char *message, size_t *frame_length);

/**
 * Parse a WebSocket frame header
 * @param header Frame header bytes
 * @param header_len Number of bytes available in header buffer
 * @param opcode Output: opcode from frame (0x1=text, 0x8=close, 0x9=ping, 0xA=pong)
 * @param payload_length Output: payload length
 * @param header_size Output: actual header size in bytes
 * @return 0 on success, -1 on error
 */
int ws_protocol_parse_frame_header(const uint8_t *header, size_t header_len,
                                    uint8_t *opcode, size_t *payload_length,
                                    size_t *header_size);

#ifdef __cplusplus
}
#endif

#endif // WS_PROTOCOL_H
