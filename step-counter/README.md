# ESP32 Step Counter - WebSocket Version

## Note:

build and upload using:
https://flashesp.com

## Overview

This firmware monitors a magnetic switch on a stepping machine and sends step events in real-time via WebSocket to a cloud server. The server broadcasts step events to all connected web clients.

## Features

- **WebSocket Communication**: Direct WebSocket connection to the cloud (no HTTP polling)
- **Real-time Updates**: Steps are sent immediately as they occur
- **Auto-reconnect**: WebSocket automatically reconnects on disconnect
- **Idle Timeout**: Automatically disconnects after 1 minute of inactivity to reduce AWS costs
- **Step Buffering**: Buffers up to 100 steps while disconnected; resends when reconnected
- **Wi-Fi Captive Portal**: Easy first-time Wi-Fi setup via phone/browser
- **LED Status**: Visual feedback for connection and step events

## Magnetic Switch Connection

- **Switch Type:** Normally open magnetic reed switch
- **ESP32 Pin:** GPIO 18 (default, can be changed in code)

### Wiring Diagram

```
[ESP32 GPIO 18] ----[Switch]----[GND]
```

- One terminal of the magnetic switch connects to GPIO 18 on the ESP32.
- The other terminal connects to GND on the ESP32.
- No external pull-up resistor is needed; the code enables the internal pull-up.

## Notes

- You can change the GPIO pin in `main.c` by modifying `#define MAGNETIC_SWITCH_GPIO 18`.
- The switch closes (connects GPIO to GND) when the magnet passes, triggering a step event.
- Make sure the switch is rated for low voltage and current (ESP32 GPIO).

## Example Pinout (ESP32 DevKit)

- GPIO 18: Pin 23 on ESP32 DevKit V1
- GND: Any ground pin

## Troubleshooting

- If steps are not detected, check switch orientation and wiring.
- Use a multimeter to verify the switch closes when the magnet passes.

## How It Works

### Startup Sequence

1. **Wi-Fi Setup**: On first boot, if no credentials are stored, the device creates a captive portal ("StepCounterSetup") for easy Wi-Fi configuration via your phone
2. **Time Sync**: Once Wi-Fi is connected, the device syncs time via NTP to ensure accurate step timestamps
3. **WebSocket Connect**: Establishes a WebSocket connection to the server for real-time communication

### Step Detection

- The magnetic reed switch is monitored via **GPIO 18 interrupt**
- When the magnet passes, the switch closes and triggers an interrupt
- An **80ms debounce filter** prevents false triggers
- Detected steps are stored in `pendingStep` with a precise millisecond timestamp

### Step Sending

- **If Connected**: Steps are immediately sent as JSON via WebSocket
  - LED flashes once per step
  - Activity timer is reset to track idle time
- **If Disconnected**: Steps are buffered in memory (up to 100 steps)
  - A log message shows the current buffer size
  - When the buffer exceeds 100 steps, older steps are lost

### Idle Timeout & Cost Optimization

The device implements AWS cost-saving logic:

- **Active State**: When steps are being sent, the connection stays open
- **Idle Detection**: If **no steps are detected for 60 seconds**, the WebSocket connection is cleanly closed
  - This stops AWS charges for that idle hour
  - LED status indicates disconnection
- **Automatic Reconnection**: When a step is detected while disconnected:
  - Device reconnects immediately
  - All buffered steps are flushed to the server in order
  - Normal operation resumes

### Reconnection Behavior

- **WebSocket Down**: Attempts to reconnect every 5 seconds
- **WiFi Down**: Attempts WiFi reconnection every 10 seconds
- **Graceful Handling**: If reconnection fails, buffered steps remain safe in memory

### Data Format

Steps are sent as JSON:
```json
{
  "action": "sendStep",
  "data": {
    "sent_at": "1704553200.123"
  }
}
```

The `sent_at` timestamp is in epoch seconds with millisecond precision.

### LED Indicators

- **Double flash on boot**: WebSocket connected successfully
- **Single flash per step**: Step sent to server
- **No flash**: Device idle or disconnected (normal during idle timeout)

---

For further customization or diagrams, let me know!
