#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief WiFi connection result
 */
typedef enum {
    WIFI_RESULT_CONNECTED,
    WIFI_RESULT_NO_CREDENTIALS,
    WIFI_RESULT_FAILED
} wifi_result_t;

/**
 * @brief Initialize WiFi and attempt connection
 *
 * Tries to connect using stored credentials. If unsuccessful or no credentials exist,
 * starts AP mode with captive portal.
 *
 * @return WIFI_RESULT_CONNECTED if connected, WIFI_RESULT_NO_CREDENTIALS if no stored creds,
 *         WIFI_RESULT_FAILED if connection failed
 */
wifi_result_t wifi_manager_init(void);

/**
 * @brief Start AP mode and captive portal
 *
 * Creates an access point with SSID "Stepper" and starts a web server
 * with captive portal for WiFi configuration.
 */
void wifi_manager_start_ap_mode(void);

/**
 * @brief Check if WiFi is connected
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Disconnect from WiFi and stop WiFi for power saving
 *
 * Disconnects from the current network and stops the WiFi driver to save power.
 * Call wifi_manager_reconnect() to reconnect.
 */
void wifi_manager_disconnect(void);

/**
 * @brief Reconnect WiFi after power saving disconnect
 *
 * Restarts WiFi and attempts to connect using stored credentials.
 * Must be called after wifi_manager_disconnect(), not before wifi_manager_init().
 *
 * @return WIFI_RESULT_CONNECTED if connected, WIFI_RESULT_FAILED if connection failed
 */
wifi_result_t wifi_manager_reconnect(void);

/**
 * @brief Get number of stored WiFi credentials
 */
int wifi_manager_get_stored_count(void);

/**
 * @brief Get WiFi QR code string for AP connection
 *
 * Returns a string in format: WIFI:T:nopass;S:<SSID>;;
 * which can be scanned to automatically connect to the AP.
 *
 * @param buffer Buffer to store QR code string
 * @param buffer_size Size of buffer
 * @return true if successful, false if buffer too small
 */
bool wifi_manager_get_ap_qr_string(char *buffer, size_t buffer_size);

#endif // WIFI_MANAGER_H
