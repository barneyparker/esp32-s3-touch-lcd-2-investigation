/*
 * ESP32 Step Counter - WebSocket Version (Self-contained)
 *
 * Monitors a magnetic switch on a stepping machine and sends step events
 * via WebSocket to a cloud server.
 *
 * Uses ONLY built-in ESP32 Arduino libraries - no external dependencies.
 * Implements minimal WebSocket client protocol over WiFiClientSecure.
 */

#include "logger.h"
#include "led.h"
#include "ota.h"
#include "ntp-time.h"
#include "wifi.h"
#include "util.h"
#include "websocket.h"
#include "steps.h"

const unsigned long WS_IDLE_TIMEOUT = 60000; // 1 minute idle timeout

// --- Setup ---
void setup()
{
  logger::init();
  logger::infoLn("\n[Boot] ESP32 Step Counter starting...");

  // Seed random for WebSocket key generation
  randomSeed(analogRead(0) ^ micros());

  // LED setup
  led::init();
  led::off();

  // Magnetic switch setup
  steps::init();

  // set up wifi
  if (!wifi::init())
  {
    wifi::startCaptivePortal();
  }
  else
  {
    // Sync time
    ntp_time::sync();

    // Check for firmware update on cold boot
    if (wifi::isConnected())
    {
      logger::infoLn("[OTA] Checking for updates after cold boot...");
      ota::performOTAUpdate();
    }
  }

  // Connect WebSocket
  websocket::connect();

  logger::infoLn("[Boot] Setup complete!");
}

// --- Main Loop ---
void loop()
{
  // Handle WebSocket and WiFi power management
  if (wifi::isPoweredOn() && wifi::isConnected())
  {
    if (websocket::isConnected() && websocket::isClientConnected())
    {
      // Check for idle timeout - disconnect if no activity for 1 minute
      if (millis() - websocket::getLastActivityMs() > WS_IDLE_TIMEOUT)
      {
        logger::infoLn("[WS] Idle timeout, disconnecting");
        websocket::disconnect();

        // Power down WiFi to save power
        logger::infoLn("[WiFi] Powering down WiFi for idle period");
        wifi::disconnect();
      }
      else
      {
        websocket::handleIncoming();
      }
    }
    // Note: We don't auto-reconnect here. Connection is restored when a step is detected.
  }
  else if (!wifi::isReconnecting())
  {
    // WiFi is off, don't try to reconnect unless a step wakes us up
    websocket::disconnect();
  }

  // Process pending steps
  if (steps::isDetected())
  {
    steps::clearDetected();

    // Power up WiFi and reconnect if needed
    if (!wifi::isPoweredOn())
    {
      logger::infoLn("[Step] WiFi powered down, initiating reconnection...");
      wifi::setReconnecting(true);
      wifi::connectWiFi();

      // Attempt WebSocket connection if WiFi reconnected successfully
      if (wifi::isPoweredOn() && !websocket::isConnected())
      {
        logger::infoLn("[Step] WiFi reconnected, now connecting WebSocket...");
        websocket::connect();
      }
    }
    // Attempt WebSocket connection if WiFi is already up but WS is not connected
    else if (wifi::isPoweredOn() && !websocket::isConnected() && !wifi::isReconnecting())
    {
      logger::infoLn("[Step] WebSocket disconnected, reconnecting...");
      wifi::setReconnecting(true);
      websocket::connect();
    }
  }

  // Auto-flush buffered steps once reconnection is complete
  if (!wifi::isReconnecting() && websocket::isConnected() && steps::getBufferSize() > 0)
  {
    steps::flushBuffer();
  }

  delay(10);
}
