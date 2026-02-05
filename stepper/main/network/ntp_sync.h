#ifndef NTP_SYNC_H
#define NTP_SYNC_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stdbool.h>
#include <time.h>

esp_err_t ntp_sync_init(void);
bool ntp_sync_is_synced(void);
esp_err_t ntp_sync_wait_for_sync(TickType_t timeout_ms);
bool ntp_sync_get_time(time_t *out_time);

#endif
