#ifndef OTA_H
#define OTA_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialize OTA system and load stored ETag from NVS
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_init(void);

/**
 * @brief Check for firmware updates and perform OTA if available
 *
 * This function:
 * 1. Checks the firmware URL for ETag
 * 2. Compares with stored ETag
 * 3. Downloads new firmware if different (with progress callback)
 * 4. Installs and reboots if successful
 *
 * @return ESP_OK if no update needed or update successful, error code otherwise
 */
esp_err_t ota_check_and_update(void);

/**
 * @brief Get the currently stored firmware ETag
 *
 * @return Pointer to ETag string, or NULL if not set
 */
const char* ota_get_current_etag(void);

#endif // OTA_H
