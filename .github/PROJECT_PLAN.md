# Stepper Project - Implementation Plan

> **Project:** ESP32-S3 Step Counter with LCD Display
> **Framework:** ESP-IDF
> **Last Updated:** 2026-02-05
> **Status:** 🟡 In Progress

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Module Breakdown](#module-breakdown)
4. [Implementation Phases](#implementation-phases)
5. [Task Checklist](#task-checklist)
6. [File Structure](#file-structure)
7. [Dependencies](#dependencies)
8. [Build & Deployment](#build--deployment)
9. [Testing Strategy](#testing-strategy)

---

## Overview

### Project Goal

Build an ESP-IDF application for the ESP32-S3 microcontroller that:

- Counts steps from a magnetic switch using an ISR
- Displays step count, time, and status on a 320x240 LCD screen
- Connects to WiFi (with captive portal fallback for credential entry)
- Sends step data to a WebSocket server
- Supports OTA firmware updates
- Stores step data locally when offline (buffer/backlog)

### Source Reference

The [step-counter](../step-counter/) directory contains the original Arduino implementation. This must be ported to ESP-IDF while adding LCD display functionality.

### Hardware

- **MCU:** ESP32-S3
- **Display:** 320x240 LCD (ST7789 controller) with touch (CST816S)
- **Input:** Magnetic switch (GPIO-based step detection)
- **Connectivity:** WiFi (2.4GHz)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                          APPLICATION                                 │
├─────────────────────────────────────────────────────────────────────┤
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐            │
│  │   WiFi   │  │   OTA    │  │   NTP    │  │  Steps   │            │
│  │ Manager  │  │ Updater  │  │   Sync   │  │ Counter  │            │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘            │
│       │             │             │             │                    │
│  ┌────┴─────────────┴─────────────┴─────────────┴────┐              │
│  │              Application State Manager            │              │
│  └────┬─────────────────────────────────────────────┬┘              │
│       │                                             │                │
│  ┌────┴─────┐                              ┌────────┴───────┐       │
│  │ WebSocket│                              │   UI Manager   │       │
│  │  Client  │                              │    (LVGL)      │       │
│  └──────────┘                              └────────────────┘       │
├─────────────────────────────────────────────────────────────────────┤
│                         ESP-IDF COMPONENTS                           │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐       │
│  │  NVS    │ │  WiFi   │ │  HTTP   │ │  SNTP   │ │  GPIO   │       │
│  │ Storage │ │  Stack  │ │  Client │ │  Client │ │   ISR   │       │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘       │
├─────────────────────────────────────────────────────────────────────┤
│                         HARDWARE ABSTRACTION                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                  │
│  │  LCD/LVGL   │  │    Touch    │  │  Mag Switch │                  │
│  │  (ST7789)   │  │  (CST816S)  │  │    (GPIO)   │                  │
│  └─────────────┘  └─────────────┘  └─────────────┘                  │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Module Breakdown

### 1. WiFi Manager (`wifi_manager.c/h`)

**Purpose:** Handle WiFi connectivity, credential storage, and captive portal.

**Features:**

- [ ] Load/save up to 10 WiFi credentials from NVS
- [ ] Scan for available networks
- [ ] Connect to known networks (with priority/signal strength)
- [ ] Exponential backoff for reconnection attempts
- [ ] Start AP mode for captive portal
- [ ] DNS captive portal redirect
- [ ] HTTP server for credential entry page
- [ ] Mobile-first responsive HTML form
- [ ] Network management (add/delete stored networks)

**Reference:** [step-counter/wifi.cpp](../step-counter/wifi.cpp)

### 2. Captive Portal (`captive_portal.c/h`)

**Purpose:** Provide web interface for WiFi configuration.

**Features:**

- [ ] Start softAP with configurable SSID
- [ ] DNS server for captive portal detection
- [ ] HTTP server with routes:
  - `GET /` - Main configuration page
  - `GET /scan` - Scan for networks (AJAX)
  - `POST /connect` - Submit credentials
  - `GET /status` - Connection status
  - `GET /networks` - List saved networks
  - `DELETE /networks/{id}` - Remove saved network
- [ ] Mobile-first HTML/CSS design
- [ ] Show available networks with signal strength
- [ ] Password input with show/hide toggle
- [ ] Connection status feedback

### 3. QR Code Display (`qr_display.c/h`)

**Purpose:** Generate and display WiFi QR code on LCD.

**Features:**

- [ ] Generate WiFi QR code (WIFI:T:WPA;S:ssid;P:password;;)
- [ ] Display QR code centered on 320x240 screen
- [ ] Make QR code as large as practical for easy scanning
- [ ] Use qrcode library for generation
- [ ] Render to LVGL canvas/image

### 4. OTA Updater (`ota_updater.c/h`)

**Purpose:** Check for and apply firmware updates.

**Features:**

- [ ] Store current firmware version/ETag in NVS
- [ ] Check firmware URL on boot (after WiFi connect)
- [ ] Compare ETag to detect new firmware
- [ ] Download firmware with progress tracking
- [ ] Apply update using esp_ota_ops
- [ ] Rollback support on boot failure

**Reference:** [step-counter/ota.cpp](../step-counter/ota.cpp)

### 5. NTP Time Sync (`ntp_sync.c/h`)

**Purpose:** Synchronize system time via NTP.

**Features:**

- [ ] Configure SNTP client
- [ ] Sync time from pool.ntp.org
- [ ] Store time base for accurate step timestamps
- [ ] Retry on failure
- [ ] Expose current time for display

**Reference:** [step-counter/ntp-time.cpp](../step-counter/ntp-time.cpp)

### 6. Step Counter (`step_counter.c/h`)

**Purpose:** Detect and count steps using magnetic switch ISR.

**Features:**

- [ ] Configure GPIO interrupt for magnetic switch
- [ ] Debounce logic (80ms minimum between steps)
- [ ] Buffer steps with millisecond timestamps
- [ ] Store up to 100 buffered steps
- [ ] Persist backlog to NVS on overflow risk
- [ ] Expose step count and buffer status
- [ ] Signal step detection to main task

**Reference:** [step-counter/steps.cpp](../step-counter/steps.cpp)

### 7. WebSocket Client (`websocket_client.c/h`)

**Purpose:** Send step data to cloud server.

**Features:**

- [ ] Connect to secure WebSocket (WSS)
- [ ] Handle connection lifecycle
- [ ] Send step events as JSON
- [ ] Handle server responses/acknowledgments
- [ ] Automatic reconnection with backoff
- [ ] Idle timeout disconnect (power saving)
- [ ] Flush buffered steps on reconnect

**Reference:** [step-counter/websocket.cpp](../step-counter/websocket.cpp)

### 8. UI Manager (`ui_manager.c/h`)

**Purpose:** Manage LVGL-based user interface.

**Features:**

- [ ] Initialize LVGL with ST7789 display driver
- [ ] Initialize touch input (CST816S)
- [ ] Create screen layouts:
  - **Step Mode Screen:** Main operational display
  - **Setup Screen:** QR code + instructions for captive portal
  - **Connecting Screen:** WiFi connection progress
  - **Update Screen:** OTA update progress
- [ ] Thread-safe LVGL operations (mutex)
- [ ] Update UI elements from application state

### 9. Step Mode UI (`ui_step_mode.c/h`)

**Purpose:** Main step counting display.

**Layout (320x240):**

```
┌────────────────────────────────────────┐
│ 🔋 85%          📶 ●●●○     🔌 ✓      │  <- Status bar (32px)
├────────────────────────────────────────┤
│                                        │
│              12,345                    │  <- Step count (large, centered)
│              STEPS                     │
│                                        │
├────────────────────────────────────────┤
│         14:32  •  3 pending            │  <- Time + backlog (24px)
└────────────────────────────────────────┘
```

**Elements:**

- [ ] Battery level indicator (icon + percentage)
- [ ] WiFi signal strength indicator
- [ ] WebSocket connection indicator
- [ ] Large step count (primary focus)
- [ ] Current time (HH:MM format)
- [ ] Pending/backlog count
- [ ] Touch interaction (future: settings access)

### 10. Application State (`app_state.c/h`)

**Purpose:** Central state management and event coordination.

**State Variables:**

```c
typedef struct {
    // WiFi
    wifi_state_t wifi_state;        // DISCONNECTED, CONNECTING, CONNECTED, AP_MODE
    char wifi_ssid[33];
    int wifi_rssi;

    // WebSocket
    ws_state_t ws_state;            // DISCONNECTED, CONNECTING, CONNECTED

    // Steps
    uint32_t step_count;
    uint8_t backlog_size;

    // Time
    bool time_synced;
    time_t current_time;

    // Battery
    uint8_t battery_percent;
    bool battery_charging;

    // OTA
    ota_state_t ota_state;          // IDLE, CHECKING, DOWNLOADING, INSTALLING
    uint8_t ota_progress;
} app_state_t;
```

**Features:**

- [ ] Thread-safe state access
- [ ] State change notifications (callbacks/events)
- [ ] Persist critical state to NVS

### 11. Display Driver (`display_driver.c/h`)

**Purpose:** Low-level LCD and touch hardware initialization.

**Features:**

- [ ] SPI bus initialization for ST7789
- [ ] LCD panel initialization and configuration
- [ ] Backlight PWM control
- [ ] I2C bus initialization for touch
- [ ] Touch controller initialization (CST816S)
- [ ] LVGL display driver registration
- [ ] LVGL input device registration

**Reference:** [stepper/main/main.c](../stepper/main/main.c) (existing display_init)

### 12. Storage Manager (`storage_manager.c/h`)

**Purpose:** Abstract NVS operations for all modules.

**Features:**

- [ ] Initialize NVS flash
- [ ] WiFi credential storage (up to 10 networks)
- [ ] Firmware ETag storage
- [ ] Step backlog persistence
- [ ] Configuration settings storage

---

## Implementation Phases

### Phase 1: Foundation (Week 1)

> Core infrastructure and hardware initialization

- [ ] **1.1** Project structure setup
- [ ] **1.2** CMake configuration and dependencies
- [ ] **1.3** Display driver initialization (existing code cleanup)
- [ ] **1.4** LVGL integration and basic UI framework
- [ ] **1.5** Storage manager (NVS abstraction)
- [ ] **1.6** Basic application state management

### Phase 2: Connectivity (Week 2)

> WiFi and network functionality

- [ ] **2.1** WiFi manager - station mode connection
- [ ] **2.2** WiFi manager - credential storage
- [ ] **2.3** WiFi manager - AP mode
- [ ] **2.4** Captive portal - HTTP server
- [ ] **2.5** Captive portal - HTML/CSS UI
- [ ] **2.6** QR code generation and display
- [ ] **2.7** DNS captive portal redirect

### Phase 3: Cloud Integration (Week 3)

> Server communication

- [ ] **3.1** NTP time synchronization
- [ ] **3.2** WebSocket client - connection
- [ ] **3.3** WebSocket client - message handling
- [ ] **3.4** OTA updater - version check
- [ ] **3.5** OTA updater - download and install

### Phase 4: Step Counting (Week 4)

> Core functionality

- [ ] **4.1** Step counter - GPIO ISR setup
- [ ] **4.2** Step counter - debounce logic
- [ ] **4.3** Step counter - buffering
- [ ] **4.4** Step data transmission
- [ ] **4.5** Backlog persistence and retry

### Phase 5: User Interface (Week 5)

> Complete UI implementation

- [ ] **5.1** Step mode screen layout
- [ ] **5.2** Status bar indicators
- [ ] **5.3** Setup/QR code screen
- [ ] **5.4** Connection progress screen
- [ ] **5.5** OTA update progress screen
- [ ] **5.6** Screen transitions and navigation

### Phase 6: Polish & Testing (Week 6)

> Quality assurance and optimization

- [ ] **6.1** Error handling and recovery
- [ ] **6.2** Power management optimization
- [ ] **6.3** Memory optimization
- [ ] **6.4** Integration testing
- [ ] **6.5** Documentation
- [ ] **6.6** GitHub Actions CI/CD

---

## Task Checklist

### Immediate Tasks (Current Sprint)

- [ ] Clean up existing stepper/main/main.c
- [ ] Create modular file structure
- [ ] Implement basic application state management
- [ ] Extract display initialization to separate module

### Backlog

See [Implementation Phases](#implementation-phases) for complete task breakdown.

---

## File Structure

```
stepper/
├── CMakeLists.txt
├── idf_component.yml
├── sdkconfig.defaults
├── partitions.csv                    # Custom partition table for OTA
│
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                        # Application entry point
│   ├── app_state.c                   # Central state management
│   ├── app_state.h
│   │
│   ├── drivers/
│   │   ├── display_driver.c          # LCD + touch initialization
│   │   ├── display_driver.h
│   │   └── battery_monitor.c         # ADC battery reading
│   │   └── battery_monitor.h
│   │
│   ├── network/
│   │   ├── wifi_manager.c            # WiFi connectivity
│   │   ├── wifi_manager.h
│   │   ├── captive_portal.c          # AP mode web server
│   │   ├── captive_portal.h
│   │   ├── websocket_client.c        # Cloud communication
│   │   ├── websocket_client.h
│   │   ├── ntp_sync.c                # Time synchronization
│   │   ├── ntp_sync.h
│   │   ├── ota_updater.c             # Firmware updates
│   │   └── ota_updater.h
│   │
│   ├── core/
│   │   ├── step_counter.c            # ISR-based step detection
│   │   ├── step_counter.h
│   │   ├── storage_manager.c         # NVS abstraction
│   │   └── storage_manager.h
│   │
│   ├── ui/
│   │   ├── ui_manager.c              # LVGL management
│   │   ├── ui_manager.h
│   │   ├── ui_step_mode.c            # Main step display
│   │   ├── ui_step_mode.h
│   │   ├── ui_setup.c                # QR code/setup screen
│   │   ├── ui_setup.h
│   │   ├── ui_common.c               # Shared UI components
│   │   ├── ui_common.h
│   │   ├── qr_display.c              # QR code generation
│   │   └── qr_display.h
│   │
│   └── web/
│       ├── portal_html.h             # Embedded HTML/CSS
│       └── portal_routes.c           # HTTP route handlers
│       └── portal_routes.h
│
├── components/
│   ├── esp_lcd_touch/                # (existing)
│   ├── esp_lcd_touch_cst816s/        # (existing)
│   ├── lvgl/                         # (existing)
│   └── qrcode/                       # QR code generation library
│
└── build/                            # Build output
```

---

## Dependencies

### ESP-IDF Components (idf_component.yml)

```yaml
dependencies:
  # Display
  lvgl/lvgl: '~8.4.0'
  espressif/esp_lcd_touch_cst816s: '*'

  # QR Code generation
  espressif/qrcode: '*'

  # JSON handling (for WebSocket messages)
  espressif/cJSON: '*'

  # Certificate bundle for HTTPS/WSS
  espressif/esp_tls: '*'
```

### ESP-IDF Kconfig Options

```
# WiFi
CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y

# HTTP Server (for captive portal)
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
CONFIG_HTTPD_MAX_URI_LEN=512

# WebSocket
CONFIG_ESP_WS_CLIENT_ENABLE=y

# SNTP
CONFIG_LWIP_SNTP_MAX_SERVERS=3

# OTA
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y

# Partition table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# NVS
CONFIG_NVS_ENCRYPTION=n
```

---

## Build & Deployment

### Local Build

```bash
cd stepper
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### GitHub Actions CI/CD

```yaml
# .github/workflows/build.yml
name: Build Stepper Firmware

on:
  push:
    branches: [main]
    paths: ['stepper/**']
  pull_request:
    branches: [main]
    paths: ['stepper/**']

jobs:
  build:
    runs-on: ubuntu-latest
    container:
      image: espressif/idf:v5.1
    steps:
      - uses: actions/checkout@v4
      - name: Build
        run: |
          cd stepper
          idf.py build
      - name: Upload firmware
        uses: actions/upload-artifact@v4
        with:
          name: firmware
          path: stepper/build/stepper.bin
```

### Partition Table (partitions.csv)

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 1M,
ota_0,    app,  ota_0,   0x110000,1M,
ota_1,    app,  ota_1,   0x210000,1M,
storage,  data, spiffs,  0x310000,512K,
```

---

## Testing Strategy

### Unit Tests

- [ ] Storage manager - NVS read/write
- [ ] WiFi credential serialization
- [ ] Step timestamp calculation
- [ ] JSON message formatting

### Integration Tests

- [ ] WiFi connection flow
- [ ] Captive portal HTTP endpoints
- [ ] WebSocket connection and messaging
- [ ] OTA download simulation

### Hardware Tests

- [ ] LCD display output verification
- [ ] Touch input responsiveness
- [ ] Magnetic switch debounce accuracy
- [ ] Battery level reading calibration
- [ ] WiFi range and reconnection

### Manual Testing Checklist

- [ ] Fresh device (no credentials) → captive portal
- [ ] Scan and select WiFi network
- [ ] Submit credentials → connect
- [ ] Verify time sync
- [ ] Trigger step → verify count updates
- [ ] Disconnect WiFi → verify buffering
- [ ] Reconnect → verify buffer flush
- [ ] OTA update available → download and install
- [ ] Verify rollback on bad firmware

---

## Configuration Constants

```c
// Network
#define CONFIG_WIFI_MAX_STORED_NETWORKS    10
#define CONFIG_WIFI_RECONNECT_MAX_ATTEMPTS 10
#define CONFIG_WIFI_AP_SSID                "StepperSetup"
#define CONFIG_WIFI_AP_CHANNEL             1

// WebSocket
#define CONFIG_WS_HOST                     "steps-ws.barneyparker.com"
#define CONFIG_WS_PORT                     443
#define CONFIG_WS_PATH                     "/"
#define CONFIG_WS_IDLE_TIMEOUT_MS          60000

// OTA
#define CONFIG_OTA_FIRMWARE_URL            "https://steps.barneyparker.com/firmware/stepper.bin"

// NTP
#define CONFIG_NTP_SERVER                  "pool.ntp.org"

// Steps
#define CONFIG_STEP_DEBOUNCE_MS            80
#define CONFIG_STEP_BUFFER_SIZE            100
#define CONFIG_STEP_GPIO                   18

// Display
#define CONFIG_LCD_H_RES                   240
#define CONFIG_LCD_V_RES                   320
#define CONFIG_LCD_SPI_CLOCK_HZ            (80 * 1000 * 1000)
```

---

## Notes & Decisions

### Why ESP-IDF over Arduino?

1. **Better control** over FreeRTOS tasks and memory
2. **Native OTA support** with rollback
3. **Component-based architecture** for modularity
4. **Better WiFi power management**
5. **CI/CD friendly** with deterministic builds

### LCD Orientation

- Display is 240x320 (portrait)
- Step count UI designed for portrait mode
- QR code also displayed in portrait

### Power Management Strategy

1. WiFi powered off when idle (no steps for 1 minute)
2. WebSocket disconnected during idle
3. Step ISR wakes system and reconnects
4. Battery monitoring for low-power warnings

---

## Changelog

| Date       | Change               | Author |
| ---------- | -------------------- | ------ |
| 2026-02-05 | Initial plan created | Agent  |
