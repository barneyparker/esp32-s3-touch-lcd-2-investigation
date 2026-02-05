#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <Arduino.h>

namespace websocket
{
  bool connect();
  bool sendText(const String &message);
  void handleIncoming();
  bool isConnected();
  bool isClientConnected();
  unsigned long getLastActivityMs();
  void disconnect();
}

#endif // WEBSOCKET_H