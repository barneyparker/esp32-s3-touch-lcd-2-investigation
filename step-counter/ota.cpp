#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <Preferences.h>
#include "ota.h"
#include "logger.h"
#include "led.h"

extern String currentFirmwareETag;

const char *FIRMWARE_URL = "https://steps.barneyparker.com/firmware/step-counter.bin";

Preferences otaPreferences;
String currentFirmwareETag;

const char *headerKeys[] = {"ETag", "Content-Length"};

namespace ota
{
  void performOTAUpdate()
  {
    otaPreferences.begin("firmware", true);
    currentFirmwareETag = otaPreferences.getString("etag", "");
    otaPreferences.end();
    if (currentFirmwareETag.length() > 0)
    {
      logger::info("[OTA] Loaded firmware ETag: ");
      logger::infoLn(currentFirmwareETag);
    }
    else
    {
      logger::infoLn("[OTA] No stored firmware ETag");
    }

    logger::infoLn("[OTA] Starting firmware download...");

    HTTPClient http;
    http.begin(FIRMWARE_URL);
    http.collectHeaders(headerKeys, 2);

    int httpCode = http.GET();

    if (httpCode != 200)
    {
      logger::info("[OTA] Download failed, HTTP code: ");
      logger::infoLn(httpCode);
      http.end();
      return;
    }

    String newETag = http.header("ETag");

    // compare ETags
    if (newETag.length() > 0 && newETag == currentFirmwareETag)
    {
      logger::infoLn("[OTA] Firmware is already up to date (ETag match)");
      http.end();
      return;
    }
    else
    {
      logger::info("[OTA] New firmware available - ETag: ");
      logger::infoLn(newETag);
    }

    int contentLength = http.header("Content-Length").toInt();

    if (contentLength <= 0)
    {
      logger::info("[OTA] Invalid content length: ");
      logger::infoLn(contentLength);
      http.end();
      return;
    }

    logger::info("[OTA] Firmware size: ");
    logger::infoLn(contentLength);

    if (!Update.begin(contentLength))
    {
      logger::infoLn("[OTA] Not enough space for OTA update");
      http.end();
      return;
    }

    logger::infoLn("[OTA] Starting update process...");

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buff[256];
    size_t written = 0;
    unsigned long startMs = millis();

    while (http.connected() && written < contentLength)
    {
      size_t available = stream->available();
      if (available)
      {
        int c = stream->readBytes(buff, min(available, (size_t)256));
        Update.write(buff, c);
        written += c;

        // Sine wave LED breathing (2 second period)
        float phase = (millis() - startMs) / 2000.0 * 2.0 * 3.14159265;
        int brightness = (sin(phase) + 1.0) * 127.5; // 0-255
        led::level(brightness);

        // Log progress every 10KB
        if (written % 10240 == 0)
        {
          logger::infoLn("[OTA] Downloaded: ");
          logger::infoLn(written / 1024);
          logger::infoLn(" KB");
        }
      }
      delay(1);
    }

    led::off(); // Turn off LED after download

    logger::infoLn("[OTA] Download complete. Downloaded: ");
    logger::infoLn(written);
    logger::infoLn(" bytes");

    if (written != contentLength)
    {
      logger::info("[OTA] Incomplete download: ");
      logger::info(written);
      logger::info(" / ");
      logger::infoLn(contentLength);
      Update.abort();
      http.end();
      return;
    }

    if (!Update.end(true))
    {
      logger::infoLn("[OTA] Update validation failed: ");
      logger::infoLn(Update.getError());
      http.end();
      return;
    }

    http.end();

    // Success! Save the new ETag and reboot
    logger::infoLn("[OTA] Saving Firmware ETag...");
    otaPreferences.begin("firmware", false);
    otaPreferences.putString("etag", newETag);
    otaPreferences.end();
    currentFirmwareETag = newETag;
    logger::info("[OTA] Saved firmware ETag: ");
    logger::infoLn(newETag);

    logger::infoLn("[OTA] Update successful, rebooting...");
    delay(1000);
    ESP.restart();
  }
}