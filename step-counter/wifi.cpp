#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>

#include "wifi.h"
#include "logger.h"
#include "util.h"

Preferences wifiPreferences;
String wifiSSID;
String wifiPass;
String deviceMAC;
bool isReconnectingFlag = false;       // WiFi/WebSocket reconnection in progress
uint8_t wifiReconnectAttempts = 0;     // WiFi reconnection attempt counter
unsigned long wifiLastReconnectMs = 0; // Timestamp of last WiFi reconnection attempt

WebServer server(80);

String scanNetworksHTML()
{
  String html = "<select name='ssid' id='ssid'>";
  int n = wifi::scanNetworks();
  for (int i = 0; i < n; i++)
  {
    html += "<option value=\"" + wifi::ssid(i) + "\">" + wifi::ssid(i) + " (" + wifi::rssi(i) + "dBm)</option>";
  }
  html += "</select>";
  return html;
}

bool loadWiFiCredentials()
{
  wifiPreferences.begin("wifi", true);
  wifiSSID = wifiPreferences.getString("ssid", "");
  wifiPass = wifiPreferences.getString("pass", "");
  wifiPreferences.end();
  return wifiSSID.length() > 0;
}

void saveWiFiCredentials(const String &ssid, const String &pass)
{
  wifiPreferences.begin("wifi", false);
  wifiPreferences.putString("ssid", ssid);
  wifiPreferences.putString("pass", pass);
  wifiPreferences.end();
}

void handleRoot()
{
  String html = R"(
<!DOCTYPE html>
<html>
<head>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    body { font-family: sans-serif; background: #f8f8f8; margin: 0; padding: 0; }
    .container { max-width: 400px; margin: 2em auto; background: #fff; padding: 2em 1.5em; border-radius: 12px; box-shadow: 0 2px 8px #0001; }
    h2 { text-align: center; margin-top: 0; }
    label { display: block; margin: 1em 0 0.3em 0; font-weight: bold; }
    select, input[type=password] { width: 100%; font-size: 1.1em; padding: 0.5em; margin-bottom: 1em; border-radius: 6px; border: 1px solid #ccc; box-sizing: border-box; }
    input[type=submit] { width: 100%; background: #1976d2; color: #fff; border: none; border-radius: 6px; padding: 0.8em; font-size: 1.1em; font-weight: bold; cursor: pointer; margin-top: 1em; }
  </style>
</head>
<body>
  <div class='container'>
    <h2>Step Counter Setup</h2>
    <form method='POST' action='/save'>
      <label for='ssid'>Wi-Fi Network</label>
      )";
  html += scanNetworksHTML();
  html += R"(
      <label for='pass'>Wi-Fi Password</label>
      <input name='pass' id='pass' type='password' autocomplete='off'>
      <input type='submit' value='Save & Connect'>
    </form>
  </div>
</body>
</html>
)";
  server.send(200, "text/html", html);
}

void handleSave()
{
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  if (ssid.length() > 0)
  {
    saveWiFiCredentials(ssid, pass);
    server.send(200, "text/html", "<html><body><h2>Saved! Rebooting...</h2></body></html>");
    delay(1000);
    ESP.restart();
  }
  else
  {
    server.send(400, "text/html", "<html><body><h2>Error: No SSID</h2></body></html>");
  }
}

namespace wifi
{
  bool init()
  {
    if (!loadWiFiCredentials())
    {
      return false;
    }

    // Connect to Wi-Fi
    if (!connectWiFi())
    {
      return false;
    }

    return true;
  }

  bool isPoweredOn()
  {
    return WiFi.getMode() != WIFI_OFF;
  }

  bool isReconnecting()
  {
    return isReconnectingFlag;
  }

  wl_status_t status()
  {
    return WiFi.status();
  }

  bool isConnected()
  {
    return WiFi.status() == WL_CONNECTED;
  }

  int scanNetworks()
  {
    return WiFi.scanNetworks();
  }

  String ssid(int index)
  {
    return WiFi.SSID(index);
  }

  int32_t rssi(int index)
  {
    return WiFi.RSSI(index);
  }

  void disconnect()
  {
    WiFi.disconnect(true);
    isReconnectingFlag = false;
    wifiReconnectAttempts = 0;
  }

  bool connectWiFi()
  {
    unsigned long nowMs = millis();
    if (wifiReconnectAttempts > 0)
    {
      uint32_t backoffDelay = calculateBackoff(wifiReconnectAttempts);
      if (nowMs - wifiLastReconnectMs < backoffDelay)
      {
        return false;
      }
    }
    wifiLastReconnectMs = nowMs;
    logger::info("[WiFi] Connecting to ");
    logger::infoLn(wifiSSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30)
    {
      delay(500);
      logger::info(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED)
    {
      logger::info("[WiFi] Connected! IP: ");
      logger::infoLn(WiFi.localIP().toString());
      // Extract and store MAC address for device identification
      uint8_t mac[6];
      WiFi.macAddress(mac);
      deviceMAC = "";
      for (int i = 0; i < 6; i++)
      {
        if (mac[i] < 0x10)
          deviceMAC += "0";
        deviceMAC += String(mac[i], HEX);
      }
      deviceMAC.toLowerCase();
      logger::info("[WiFi] Device MAC: ");
      logger::infoLn(deviceMAC);
      wifiReconnectAttempts = 0;
      return true;
    }
    logger::infoLn("[WiFi] Connection failed");
    wifiReconnectAttempts++;
    if (wifiReconnectAttempts >= 10)
    {
      logger::infoLn("[WiFi] Max retries (10) reached, giving up");
      wifiReconnectAttempts = 0;
    }
    return false;
  }

  void startCaptivePortal()
  {
    logger::infoLn("[Portal] Starting captive portal...");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    logger::infoLn("[Portal] Scanning for networks...");
    int n = WiFi.scanNetworks();
    logger::info("[Portal] Found ");
    logger::info(n);
    logger::infoLn(" networks");

    WiFi.mode(WIFI_AP);
    WiFi.softAP("StepCounterSetup");
    logger::info("[Portal] AP IP: ");
    logger::infoLn(WiFi.softAPIP());

    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();

    logger::infoLn("[Portal] Connect to 'StepCounterSetup' WiFi and open 192.168.4.1");

    while (true)
    {
      server.handleClient();
      delay(10);
    }
  }

  const char *getDeviceMAC()
  {
    return deviceMAC.c_str();
  }

  void setReconnecting(bool value)
  {
    isReconnectingFlag = value;
  }

}