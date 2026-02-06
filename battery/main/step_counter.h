#ifndef STEP_COUNTER_H
#define STEP_COUNTER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize step counter with ISR on GPIO 18
 *
 * Sets up the GPIO interrupt for step detection on both rising and falling edges.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t step_counter_init(void);

/**
 * @brief Get the current number of buffered steps
 *
 * @return Number of steps waiting to be sent
 */
uint8_t step_counter_get_buffer_size(void);

/**
 * @brief Try to send one buffered step to the server
 *
 * Attempts to send the oldest buffered step via WebSocket.
 * Only sends if WebSocket is connected.
 *
 * @return ESP_OK if a step was sent successfully,
 *         ESP_ERR_INVALID_STATE if not connected,
 *         ESP_ERR_NOT_FOUND if buffer is empty
 */
esp_err_t step_counter_flush_one(void);

/**
 * @brief Get MAC address as string
 *
 * @param mac_str Buffer to store MAC address string (format: XX:XX:XX:XX:XX:XX)
 * @param size Size of buffer (should be at least 18 bytes)
 * @return ESP_OK on success
 */
esp_err_t step_counter_get_mac_string(char *mac_str, size_t size);

#ifdef __cplusplus
}
#endif

#endif // STEP_COUNTER_H
