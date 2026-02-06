#ifndef NTP_TIME_H
#define NTP_TIME_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Synchronize time with NTP server
 *
 * This function connects to an NTP server and synchronizes the system time.
 * It should be called after WiFi connection is established.
 *
 * @return true if time synchronization was successful, false otherwise
 */
bool ntp_time_sync(void);

/**
 * @brief Get current time as Unix timestamp
 *
 * @return Current time in seconds since Unix epoch, or 0 if time not set
 */
time_t ntp_time_get_current(void);

/**
 * @brief Check if time has been synchronized
 *
 * @return true if time has been synchronized, false otherwise
 */
bool ntp_time_is_synced(void);

/**
 * @brief Get current time as formatted string
 *
 * @param buffer Buffer to store the formatted time string
 * @param buffer_size Size of the buffer
 * @param format Format string (see strftime)
 * @return true if successful, false otherwise
 */
bool ntp_time_get_string(char *buffer, size_t buffer_size, const char *format);

#ifdef __cplusplus
}
#endif

#endif // NTP_TIME_H
