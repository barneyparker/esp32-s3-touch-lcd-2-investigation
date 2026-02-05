#include "steps.h"
#include "logger.h"
#include "websocket.h"
#include "wifi.h"
#include "led.h"
#include "ntp-time.h"

// --- Pin Definitions ---
#define MAGNETIC_SWITCH_GPIO 18

namespace
{

  // --- Global State ---
  volatile uint32_t lastStepMs = 0;

  const uint8_t MAX_BUFFERED_STEPS = 100; // Buffer up to 100 steps
  unsigned long long stepBuffer[MAX_BUFFERED_STEPS];
  uint8_t stepBufferSize = 0;
  volatile bool stepDetected = false;
  unsigned long lastLedPulseMs = 0; // Timestamp of last LED pulse for buffer full indicator

  // --- Step Buffer Management ---
  bool pushToBuffer(unsigned long long timestampMs)
  {
    if (stepBufferSize < MAX_BUFFERED_STEPS)
    {
      stepBuffer[stepBufferSize++] = timestampMs;
      logger::info("[Step] Buffered step (buffer size: ");
      logger::info(stepBufferSize);
      logger::info("/");
      logger::info(MAX_BUFFERED_STEPS);
      logger::infoLn(")");
      return true;
    }
    else
    {
      logger::infoLn("[Step] Buffer full, dropping step");
      // Pulse LED slowly (500ms on/off) to indicate buffer overflow
      unsigned long nowMs = millis();
      if (nowMs - lastLedPulseMs > 500)
      {
        lastLedPulseMs = nowMs;
        led::toggle();
      }
      return false;
    }
  }

  // --- Step Detection ISR ---
  void IRAM_ATTR magneticSwitchISR()
  {
    uint32_t nowMs = millis();
    if (nowMs - lastStepMs > 80)
    { // 80ms debounce
      lastStepMs = nowMs;
      // Calculate epoch milliseconds: base + elapsed since base was set
      unsigned long long timestampMs = ntp_time::epochBaseMs + (nowMs - ntp_time::millisBase);
      pushToBuffer(timestampMs);
      stepDetected = true;
      logger::infoLn("[ISR] Step detected");
    }
  }

  // --- Send Single Step ---
  bool sendSingleStep(unsigned long long timestampMs)
  {
    // Build the JSON payload
    String json = "{\"action\":\"sendStep\",\"data\":{\"sent_at\":";
    json += String(timestampMs / 1000);
    json += ".";
    json += String((timestampMs % 1000) / 100); // 1 decimal place
    json += String((timestampMs % 100) / 10);   // 2nd decimal
    json += String(timestampMs % 10);           // 3rd decimal (ms)
    json += ",\"deviceMAC\":\"";
    json += wifi::getDeviceMAC();
    json += "\"}}";
    logger::info("[WS] Sending Step payload ");
    logger::infoLn(json);
    if (websocket::sendText(json))
    {
      logger::infoLn("[WS] Step send success");
      led::flash(1, 50);
      return true;
    }
    else
    {
      logger::infoLn("[WS] Step send failed");
      return false;
    }
  }

} // anonymous namespace

namespace steps
{

  void init()
  {
    pinMode(MAGNETIC_SWITCH_GPIO, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(MAGNETIC_SWITCH_GPIO), magneticSwitchISR, CHANGE);
  }

  void clearStepDetected()
  {
    stepDetected = false;
  }

  bool isDetected()
  {
    return stepDetected;
  }

  void clearDetected()
  {
    stepDetected = false;
  }

  // --- Flush Buffered Steps ---
  void flushBuffer()
  {
    if (stepBufferSize == 0 || !websocket::isConnected())
    {
      return;
    }
    unsigned long long step = stepBuffer[0];

    if (sendSingleStep(step))
    {
      // Remove the sent step from buffer
      for (uint8_t i = 1; i < stepBufferSize; i++)
      {
        stepBuffer[i - 1] = stepBuffer[i];
      }
      stepBufferSize -= 1;
    }
    else
    {
      websocket::disconnect();
    }
  }

  uint8_t getBufferSize()
  {
    return stepBufferSize;
  }

}