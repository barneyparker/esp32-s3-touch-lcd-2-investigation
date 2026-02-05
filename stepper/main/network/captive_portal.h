#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include "esp_err.h"

// AP configuration
#define CAPTIVE_PORTAL_AP_SSID "Stepper"

esp_err_t captive_portal_start(void);
esp_err_t captive_portal_stop(void);

#endif
