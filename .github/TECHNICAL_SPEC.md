# Stepper Project - Technical Specification

## 1. Module Interfaces

### 1.1 Application State (`app_state.h`)

```c
#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE
} wifi_state_t;

typedef enum {
    WS_STATE_DISCONNECTED,
    WS_STATE_CONNECTING,
    WS_STATE_CONNECTED
} ws_state_t;

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_INSTALLING,
    OTA_STATE_COMPLETE,
    OTA_STATE_ERROR
} ota_state_t;

typedef enum {
    SCREEN_STEP_MODE,
    SCREEN_SETUP,
    SCREEN_CONNECTING,
    SCREEN_OTA_UPDATE
} screen_id_t;

typedef struct {
    // WiFi state
    wifi_state_t wifi_state;
    char wifi_ssid[33];
    int8_t wifi_rssi;

    // WebSocket state
    ws_state_t ws_state;

    // Step data
    uint32_t step_count;
    uint8_t backlog_size;

    // Time
    bool time_synced;
    time_t current_time;

    // Battery
    uint8_t battery_percent;
    bool battery_charging;

    // OTA
    ota_state_t ota_state;
    uint8_t ota_progress;

    // UI
    screen_id_t current_screen;
} app_state_t;

// Callback type for state changes
typedef void (*state_change_cb_t)(const app_state_t* state, void* user_data);

// Initialize application state
void app_state_init(void);

// Get current state (thread-safe copy)
void app_state_get(app_state_t* out_state);

// Update state (thread-safe)
void app_state_set_wifi(wifi_state_t state, const char* ssid, int8_t rssi);
void app_state_set_ws(ws_state_t state);
void app_state_set_steps(uint32_t count, uint8_t backlog);
void app_state_set_time(bool synced, time_t time);
void app_state_set_battery(uint8_t percent, bool charging);
void app_state_set_ota(ota_state_t state, uint8_t progress);
void app_state_set_screen(screen_id_t screen);

// Register for state change notifications
void app_state_register_callback(state_change_cb_t cb, void* user_data);

#endif // APP_STATE_H
```

### 1.2 WiFi Manager (`wifi_manager.h`)

```c
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi_types.h"

#define WIFI_MAX_STORED_NETWORKS 10
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char password[WIFI_PASS_MAX_LEN + 1];
    uint8_t priority;  // Lower = higher priority
} wifi_credential_t;

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_scan_result_t;

// Initialize WiFi subsystem
esp_err_t wifi_manager_init(void);

// Station mode operations
esp_err_t wifi_manager_connect(void);
esp_err_t wifi_manager_disconnect(void);
bool wifi_manager_is_connected(void);
int8_t wifi_manager_get_rssi(void);

// AP mode operations
esp_err_t wifi_manager_start_ap(const char* ssid, const char* password);
esp_err_t wifi_manager_stop_ap(void);
bool wifi_manager_is_ap_active(void);

// Network scanning
esp_err_t wifi_manager_scan(wifi_scan_result_t* results, uint8_t max_results, uint8_t* num_found);

// Credential management
esp_err_t wifi_manager_save_credential(const wifi_credential_t* cred);
esp_err_t wifi_manager_get_credentials(wifi_credential_t* creds, uint8_t* count);
esp_err_t wifi_manager_delete_credential(const char* ssid);
esp_err_t wifi_manager_clear_credentials(void);

// Power management
void wifi_manager_power_off(void);
void wifi_manager_power_on(void);
bool wifi_manager_is_powered(void);

#endif // WIFI_MANAGER_H
```

### 1.3 WebSocket Client (`websocket_client.h`)

```c
#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <stdbool.h>
#include "esp_err.h"

typedef void (*ws_message_cb_t)(const char* message, size_t len);
typedef void (*ws_connect_cb_t)(bool connected);

typedef struct {
    const char* host;
    uint16_t port;
    const char* path;
    const char* ca_cert;
    ws_message_cb_t on_message;
    ws_connect_cb_t on_connect;
} ws_client_config_t;

// Initialize WebSocket client
esp_err_t ws_client_init(const ws_client_config_t* config);

// Connection management
esp_err_t ws_client_connect(void);
esp_err_t ws_client_disconnect(void);
bool ws_client_is_connected(void);

// Send message
esp_err_t ws_client_send(const char* message);

// Get last activity timestamp (for idle detection)
uint32_t ws_client_get_last_activity_ms(void);

#endif // WEBSOCKET_CLIENT_H
```

### 1.4 Step Counter (`step_counter.h`)

```c
#ifndef STEP_COUNTER_H
#define STEP_COUNTER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define STEP_BUFFER_SIZE 100

typedef struct {
    uint64_t timestamp_ms;  // Epoch milliseconds
} step_event_t;

// Callback when step detected
typedef void (*step_detected_cb_t)(void);

// Initialize step counter
esp_err_t step_counter_init(int gpio_num, step_detected_cb_t callback);

// Get step count
uint32_t step_counter_get_count(void);

// Buffer operations
uint8_t step_counter_get_buffer_size(void);
bool step_counter_pop_step(step_event_t* out_step);
void step_counter_clear_buffer(void);

// Persistence
esp_err_t step_counter_persist_buffer(void);
esp_err_t step_counter_restore_buffer(void);

#endif // STEP_COUNTER_H
```

### 1.5 Display Driver (`display_driver.h`)

```c
#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "esp_err.h"
#include "lvgl.h"

// Initialize display hardware (LCD + touch)
esp_err_t display_driver_init(void);

// Backlight control (0-100%)
void display_driver_set_brightness(uint8_t percent);
uint8_t display_driver_get_brightness(void);

// Get display resolution
uint16_t display_driver_get_width(void);
uint16_t display_driver_get_height(void);

// LVGL thread-safe wrappers
bool display_driver_lock(int timeout_ms);
void display_driver_unlock(void);

#endif // DISPLAY_DRIVER_H
```

### 1.6 UI Manager (`ui_manager.h`)

```c
#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "esp_err.h"
#include "app_state.h"

// Initialize UI (requires display_driver_init first)
esp_err_t ui_manager_init(void);

// Screen navigation
void ui_manager_show_screen(screen_id_t screen);

// Update UI from state (call when state changes)
void ui_manager_update(const app_state_t* state);

// Screen-specific updates
void ui_manager_update_step_count(uint32_t count);
void ui_manager_update_time(time_t time);
void ui_manager_update_battery(uint8_t percent, bool charging);
void ui_manager_update_wifi(wifi_state_t state, int8_t rssi);
void ui_manager_update_websocket(ws_state_t state);
void ui_manager_update_backlog(uint8_t size);
void ui_manager_update_ota_progress(uint8_t percent);

#endif // UI_MANAGER_H
```

### 1.7 Storage Manager (`storage_manager.h`)

```c
#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Initialize NVS storage
esp_err_t storage_init(void);

// String operations
esp_err_t storage_get_string(const char* namespace, const char* key, char* out, size_t max_len);
esp_err_t storage_set_string(const char* namespace, const char* key, const char* value);

// Integer operations
esp_err_t storage_get_u8(const char* namespace, const char* key, uint8_t* out);
esp_err_t storage_set_u8(const char* namespace, const char* key, uint8_t value);
esp_err_t storage_get_u32(const char* namespace, const char* key, uint32_t* out);
esp_err_t storage_set_u32(const char* namespace, const char* key, uint32_t value);

// Blob operations
esp_err_t storage_get_blob(const char* namespace, const char* key, void* out, size_t* len);
esp_err_t storage_set_blob(const char* namespace, const char* key, const void* data, size_t len);

// Delete operations
esp_err_t storage_delete_key(const char* namespace, const char* key);
esp_err_t storage_delete_namespace(const char* namespace);

// Check if key exists
bool storage_key_exists(const char* namespace, const char* key);

#endif // STORAGE_MANAGER_H
```

### 1.8 OTA Updater (`ota_updater.h`)

```c
#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*ota_progress_cb_t)(uint8_t percent);
typedef void (*ota_complete_cb_t)(bool success);

typedef struct {
    const char* firmware_url;
    const char* ca_cert;
    ota_progress_cb_t on_progress;
    ota_complete_cb_t on_complete;
} ota_config_t;

// Initialize OTA updater
esp_err_t ota_updater_init(const ota_config_t* config);

// Check for updates (compares ETag)
// Returns true if update available
bool ota_updater_check_available(void);

// Perform update (blocking)
esp_err_t ota_updater_perform(void);

// Get stored firmware version/ETag
void ota_updater_get_current_version(char* out, size_t max_len);

#endif // OTA_UPDATER_H
```

### 1.9 NTP Sync (`ntp_sync.h`)

```c
#ifndef NTP_SYNC_H
#define NTP_SYNC_H

#include "esp_err.h"
#include <stdbool.h>
#include <time.h>

// Initialize SNTP
esp_err_t ntp_sync_init(const char* server);

// Synchronize time (blocking, with timeout)
esp_err_t ntp_sync_now(int timeout_ms);

// Check if time is synchronized
bool ntp_sync_is_synced(void);

// Get current time as epoch milliseconds
uint64_t ntp_sync_get_epoch_ms(void);

// Get current time as struct tm
void ntp_sync_get_local_time(struct tm* out);

#endif // NTP_SYNC_H
```

### 1.10 QR Display (`qr_display.h`)

```c
#ifndef QR_DISPLAY_H
#define QR_DISPLAY_H

#include "esp_err.h"
#include "lvgl.h"

// Generate WiFi QR code and return LVGL image object
// QR encodes: WIFI:T:WPA;S:<ssid>;P:<password>;;
lv_obj_t* qr_display_create_wifi_qr(lv_obj_t* parent, const char* ssid, const char* password);

// Generate generic QR code from string
lv_obj_t* qr_display_create_qr(lv_obj_t* parent, const char* data);

// Get optimal QR size for display
uint16_t qr_display_get_optimal_size(uint16_t max_width, uint16_t max_height);

#endif // QR_DISPLAY_H
```

---

## 2. Communication Protocols

### 2.1 WebSocket Message Format

**Step Event (Client → Server):**

```json
{
  "action": "sendStep",
  "data": {
    "sent_at": 1738756800.123,
    "deviceMAC": "aa:bb:cc:dd:ee:ff"
  }
}
```

**Server Acknowledgment (Server → Client):**

```json
{
  "action": "stepAck",
  "data": {
    "received": true
  }
}
```

### 2.2 WiFi QR Code Format

Standard WiFi QR code format:

```
WIFI:T:WPA;S:<SSID>;P:<Password>;;
```

Example:

```
WIFI:T:WPA;S:StepperSetup;P:;;
```

(Empty password for open network)

---

## 3. Hardware Pin Mapping

### ESP32-S3 Touch LCD 2.0

| Function         | GPIO | Notes                 |
| ---------------- | ---- | --------------------- |
| **SPI Bus**      |      |                       |
| SCLK             | 39   | SPI clock             |
| MOSI             | 38   | SPI data out          |
| MISO             | 40   | SPI data in (unused)  |
| **LCD (ST7789)** |      |                       |
| DC               | 42   | Data/Command          |
| CS               | 45   | Chip select           |
| RST              | -1   | Not connected         |
| **I2C Bus**      |      |                       |
| SDA              | 48   | I2C data              |
| SCL              | 47   | I2C clock             |
| **Backlight**    |      |                       |
| BK_LIGHT         | 1    | PWM controlled        |
| **Step Counter** |      |                       |
| MAG_SWITCH       | 18   | Magnetic switch input |
| **Battery ADC**  |      |                       |
| BAT_ADC          | TBD  | Battery voltage sense |

---

## 4. Memory Budget

### SRAM Allocation

| Component           | Estimated Size | Notes               |
| ------------------- | -------------- | ------------------- |
| LVGL Display Buffer | 19.2 KB        | 240 _ 40 _ 2 bytes  |
| LVGL Working Memory | ~40 KB         | UI objects, styles  |
| WiFi                | ~40 KB         | Standard allocation |
| WebSocket           | ~8 KB          | Buffer + TLS        |
| Step Buffer         | 800 B          | 100 \* 8 bytes      |
| Task Stacks         | ~20 KB         | Multiple tasks      |
| **Total**           | ~130 KB        |                     |

ESP32-S3 has 512KB SRAM, leaving ~380KB for heap.

### Flash Allocation (4MB)

See [partitions.csv](../stepper/partitions.csv):

| Partition | Size   | Purpose            |
| --------- | ------ | ------------------ |
| nvs       | 24 KB  | Key-value storage  |
| phy_init  | 4 KB   | WiFi calibration   |
| factory   | 1 MB   | Factory firmware   |
| ota_0     | 1 MB   | OTA slot 1         |
| ota_1     | 1 MB   | OTA slot 2         |
| storage   | 512 KB | SPIFFS (if needed) |

---

## 5. Task Architecture

### FreeRTOS Tasks

| Task       | Priority | Stack | Purpose               |
| ---------- | -------- | ----- | --------------------- |
| main       | 1        | 4096  | Application logic     |
| lv_tick    | 5        | 2048  | LVGL tick increment   |
| lv_handler | 5        | 4096  | LVGL event processing |
| wifi_event | 5        | 4096  | WiFi event handling   |
| ws_task    | 4        | 4096  | WebSocket handling    |
| step_task  | 6        | 2048  | Step event processing |

### Event Flow

```
[GPIO ISR] ─────────────────────────────────────────────────────┐
     │                                                          │
     ▼                                                          │
[Step Task] ──▶ Buffer Step ──▶ Notify Main                     │
                                    │                           │
                                    ▼                           │
                            [Main Task Loop]                    │
                                    │                           │
              ┌─────────────────────┼─────────────────────┐     │
              ▼                     ▼                     ▼     │
        Check WiFi           Send Steps            Update UI    │
              │                     │                     │     │
              ▼                     ▼                     ▼     │
        [WiFi Events]        [WebSocket Task]      [LVGL Handler]
```

---

## 6. State Machine

### WiFi Connection State Machine

```
                    ┌─────────────┐
                    │ POWER_OFF   │
                    └──────┬──────┘
                           │ power_on()
                           ▼
                    ┌─────────────┐
         ┌──────────│ DISCONNECTED│◄─────────────┐
         │          └──────┬──────┘              │
         │                 │ connect()           │ disconnect()
         │                 ▼                     │
         │          ┌─────────────┐              │
         │          │ CONNECTING  │──────────────┤
         │          └──────┬──────┘   timeout    │
         │                 │ connected           │
         │                 ▼                     │
         │          ┌─────────────┐              │
         │          │  CONNECTED  │──────────────┘
         │          └─────────────┘   lost
         │
         │ no_credentials
         ▼
  ┌─────────────┐
  │   AP_MODE   │
  └─────────────┘
```

### Main Application State Machine

```
                         ┌─────────┐
                         │  BOOT   │
                         └────┬────┘
                              │
                              ▼
                    ┌─────────────────┐
                    │ CHECK_WIFI_CREDS│
                    └────────┬────────┘
                             │
              ┌──────────────┴──────────────┐
              │ has_creds                   │ no_creds
              ▼                             ▼
     ┌────────────────┐           ┌────────────────┐
     │  WIFI_CONNECT  │           │  START_AP_MODE │
     └───────┬────────┘           └───────┬────────┘
             │                            │
             │ connected                  │
             ▼                            ▼
     ┌────────────────┐           ┌────────────────┐
     │   CHECK_OTA    │           │ CAPTIVE_PORTAL │
     └───────┬────────┘           └────────────────┘
             │                            │
             │ no_update                  │ creds_received
             ▼                            │
     ┌────────────────┐                   │
     │   SYNC_TIME    │◄──────────────────┘
     └───────┬────────┘
             │
             ▼
     ┌────────────────┐
     │   STEP_MODE    │◄─────────────────────────┐
     └───────┬────────┘                          │
             │                                   │
             │ step_detected                     │
             ▼                                   │
     ┌────────────────┐                          │
     │  SEND_STEPS    │──────────────────────────┘
     └────────────────┘   sent_or_buffered
```

---

## 7. Error Handling

### Error Categories

| Category          | Recovery Strategy                        |
| ----------------- | ---------------------------------------- |
| WiFi Connect Fail | Retry with backoff, fall back to AP mode |
| WebSocket Fail    | Buffer steps locally, retry connection   |
| OTA Fail          | Abort, keep current firmware             |
| NVS Fail          | Use defaults, log error                  |
| Display Fail      | Panic (critical)                         |

### Backoff Strategy

```c
uint32_t calculate_backoff(uint8_t attempt) {
    // Base: 1 second, Max: 60 seconds
    uint32_t delay = 1000 * (1 << min(attempt, 6));
    return min(delay, 60000);
}
```

---

## 8. Security Considerations

### TLS/SSL

- Use ESP-IDF certificate bundle for server verification
- Pin Amazon Root CA 1 for WebSocket server
- Verify firmware URL certificate for OTA

### Credential Storage

- WiFi passwords stored in NVS (encrypted if enabled)
- No credentials logged in release builds
- Clear credentials option in captive portal

### WebSocket

- Use WSS (WebSocket Secure) only
- Validate server certificate
- Timeout idle connections

---

## 9. Build Configuration

### sdkconfig.defaults

```
# Target
CONFIG_IDF_TARGET="esp32s3"

# WiFi
CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM=10
CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM=32
CONFIG_ESP32_WIFI_SOFTAP_SUPPORT=y

# LWIP
CONFIG_LWIP_SNTP_MAX_SERVERS=3
CONFIG_LWIP_DNS_MAX_SERVERS=3

# HTTP Server
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
CONFIG_HTTPD_MAX_URI_LEN=512

# OTA
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y

# Partitions
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# LVGL
CONFIG_LV_COLOR_DEPTH_16=y
CONFIG_LV_USE_PERF_MONITOR=n
CONFIG_LV_USE_MEM_MONITOR=n

# Logging
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
```

---

## 10. Testing Hooks

### Debug Commands (Development Build)

Add serial console commands for testing:

| Command                      | Action                 |
| ---------------------------- | ---------------------- |
| `wifi scan`                  | Scan and list networks |
| `wifi connect <ssid> <pass>` | Manual connect         |
| `wifi status`                | Show connection status |
| `step add`                   | Simulate step          |
| `step show`                  | Show buffer            |
| `ota check`                  | Check for update       |
| `reboot`                     | Restart device         |

### Compile-time Flags

```c
#ifdef CONFIG_STEPPER_DEBUG
#define DEBUG_LOG(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#define DEBUG_LOG(...)
#endif
```
