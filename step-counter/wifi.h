#ifndef WIFI_H
#define WIFI_H

#include <Arduino.h>
#include <WiFi.h>

namespace wifi
{
  bool init();
  bool isPoweredOn();
  bool isReconnecting();
  wl_status_t status();
  bool isConnected();
  int scanNetworks();
  String ssid(int index);
  int32_t rssi(int index);
  void disconnect();
  bool connectWiFi();
  void startCaptivePortal();
  const char *getDeviceMAC();
  void setReconnecting(bool value);
}

#endif // WIFI_H