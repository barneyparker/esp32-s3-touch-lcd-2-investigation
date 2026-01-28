This repo is discovery work around making my new ESP32-S3-Touch-LCD-2 device work.

The plan is to work out how to:

- Turn on the screen
- Draw to the screen
- Read touch input
- Control the backlight
- Read the Battery state
- OTA updates
- Put the device to sleep and wake it up again
- Wifi captive portal for configuration

We will likely also add in the stepper functionality later on.

The code will run on FreeRTOS using the ESP-IDF framework.

We will use the WaveShare SquareLine Studio to design the UI and generate code. This will use LVGL under the hood.

The hardware is a WaveShare ESP32-S3-Touch-LCD-2 device, which has the following specs:

- ESP32-S3 microcontroller
- 2 inch TFT LCD with touch panel
- 320x240 resolution
- 4MB PSRAM
- 16MB Flash
- Built-in battery management
- Supports WiFi and Bluetooth
- Multiple GPIOs for peripherals
- IMU (Inertial Measurement Unit) sensor
- SD Card interface
