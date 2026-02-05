#ifndef QR_DISPLAY_H
#define QR_DISPLAY_H

#include "esp_err.h"

/**
 * @brief Display QR code for WiFi network
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return ESP_OK on success
 */
esp_err_t qr_display_show_wifi(const char* ssid, const char* password);

/**
 * @brief Clear QR code display
 */
void qr_display_clear(void);

#endif
