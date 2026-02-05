#include "websocket.h"
#include "logger.h"
#include "util.h"
#include "wifi.h"
#include <WiFiClientSecure.h>

const char *WS_HOST = "steps-ws.barneyparker.com";
const char *WS_PATH = "/";
const uint16_t WS_PORT = 443;

// --- Amazon Root CA 1 ---
const char *amazon_root_ca = R"(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)";

WiFiClientSecure wsClient;
volatile bool wsConnected = false;
uint8_t wsReconnectAttempts = 0;     // WebSocket reconnection attempt counter
unsigned long wsLastReconnectMs = 0; // Timestamp of last WebSocket reconnection attempt
unsigned long wsLastActivityMs = 0;  // Track last step for idle disconnect

String generateWebSocketKey()
{
  uint8_t key[16];
  for (int i = 0; i < 16; i++)
  {
    key[i] = random(256);
  }
  return base64Encode(key, 16);
}

// --- WebSocket Frame Reading ---
void handleIncomingFrame()
{
  if (!wsClient.available())
  {
    return;
  }

  uint8_t opcode = wsClient.read();
  if (opcode == -1)
  {
    return;
  }

  bool fin = (opcode & 0x80) != 0;
  opcode &= 0x0F;

  uint8_t len1 = wsClient.read();
  bool masked = (len1 & 0x80) != 0;
  uint64_t payloadLen = len1 & 0x7F;

  if (payloadLen == 126)
  {
    payloadLen = ((uint64_t)wsClient.read() << 8) | wsClient.read();
  }
  else if (payloadLen == 127)
  {
    payloadLen = 0;
    for (int i = 0; i < 8; i++)
    {
      payloadLen = (payloadLen << 8) | wsClient.read();
    }
  }

  uint8_t mask[4] = {0};
  if (masked)
  {
    for (int i = 0; i < 4; i++)
    {
      mask[i] = wsClient.read();
    }
  }

  // Read payload (limit to reasonable size)
  String payload;
  size_t toRead = min((uint64_t)1024, payloadLen);
  for (size_t i = 0; i < toRead; i++)
  {
    uint8_t b = wsClient.read();
    if (masked)
    {
      b ^= mask[i % 4];
    }
    payload += (char)b;
  }
  // Discard rest if too long
  for (size_t i = toRead; i < payloadLen; i++)
  {
    wsClient.read();
  }

  switch (opcode)
  {
  case 0x01: // Text frame
    logger::info("[WS] Received: ");
    logger::infoLn(payload);
    break;
  case 0x08: // Close
    logger::infoLn("[WS] Server closed connection");
    wsConnected = false;
    wsClient.stop();
    break;
  case 0x09: // Ping - send pong
    logger::infoLn("[WS] Got Ping, sending Pong");
    {
      uint8_t pong[2] = {0x8A, 0x00}; // Pong with no payload
      wsClient.write(pong, 2);
    }
    break;
  case 0x0A: // Pong
    logger::infoLn("[WS] Got Pong");
    break;
  }

  // Update activity timestamp after processing any frame
  wsLastActivityMs = millis();
}

namespace websocket
{
  bool connect()
  {
    unsigned long nowMs = millis();

    // Check backoff delay before attempting reconnection
    if (wsReconnectAttempts > 0)
    {
      uint32_t backoffDelay = calculateBackoff(wsReconnectAttempts);
      if (nowMs - wsLastReconnectMs < backoffDelay)
      {
        return false; // Still in backoff period, don't attempt
      }
    }

    wsLastReconnectMs = nowMs;

    logger::infoLn("[WS] Connecting to WebSocket...");

    wsClient.setCACert(amazon_root_ca);

    if (!wsClient.connect(WS_HOST, WS_PORT))
    {
      logger::infoLn("[WS] TCP connection failed");
      wsReconnectAttempts++;
      if (wsReconnectAttempts >= 10)
      {
        logger::infoLn("[WS] Max retries (10) reached, giving up");
        wsReconnectAttempts = 0; // Reset for next cycle
      }
      return false;
    }

    wsLastActivityMs = millis(); // Reset idle timer on connection
    logger::infoLn("[WS] TCP connected, sending upgrade request...");

    // Generate WebSocket key
    String wsKey = generateWebSocketKey();

    // Send HTTP upgrade request
    wsClient.print("GET ");
    wsClient.print(WS_PATH);
    wsClient.print(" HTTP/1.1\r\n");
    wsClient.print("Host: ");
    wsClient.print(WS_HOST);
    wsClient.print("\r\n");
    wsClient.print("Upgrade: websocket\r\n");
    wsClient.print("Connection: Upgrade\r\n");
    wsClient.print("Sec-WebSocket-Key: ");
    wsClient.print(wsKey);
    wsClient.print("\r\n");
    wsClient.print("Sec-WebSocket-Version: 13\r\n");
    wsClient.print("\r\n");

    // Wait for response
    unsigned long timeout = millis() + 5000;
    while (!wsClient.available() && millis() < timeout)
    {
      delay(10);
    }

    if (!wsClient.available())
    {
      logger::infoLn("[WS] No response from server");
      wsClient.stop();
      wsReconnectAttempts++;
      if (wsReconnectAttempts >= 10)
      {
        logger::infoLn("[WS] Max retries (10) reached, giving up");
        wsReconnectAttempts = 0; // Reset for next cycle
      }
      return false;
    }

    // Read response
    String response;
    while (wsClient.available())
    {
      char c = wsClient.read();
      response += c;
      if (response.endsWith("\r\n\r\n"))
      {
        break;
      }
    }

    logger::info("[WS] Response: ");
    logger::infoLn(response.substring(0, 50));

    if (response.indexOf("101") == -1)
    {
      logger::infoLn("[WS] Upgrade failed - not 101");
      wsClient.stop();
      wsReconnectAttempts++;
      if (wsReconnectAttempts >= 10)
      {
        logger::infoLn("[WS] Max retries (10) reached, giving up");
        wsReconnectAttempts = 0; // Reset for next cycle
      }
      return false;
    }

    logger::infoLn("[WS] Connected!");
    wsConnected = true;
    wsReconnectAttempts = 0;      // Reset counter on success
    wifi::setReconnecting(false); // Mark reconnection as complete
    wsLastActivityMs = millis();  // Reset idle timer on successful connection
    // flushStepBuffer(); // Removed to keep isolated
    // LED flash moved to step sending for cleaner feedback
    return true;
  }

  bool sendText(const String &message)
  {
    if (!wsConnected || !wsClient.connected())
    {
      wsConnected = false;
      return false;
    }

    size_t len = message.length();
    uint8_t header[14];
    size_t headerLen = 0;

    // FIN + Text opcode
    header[headerLen++] = 0x81;

    // Mask bit set (required for client->server) + payload length
    if (len < 126)
    {
      header[headerLen++] = 0x80 | len;
    }
    else if (len < 65536)
    {
      header[headerLen++] = 0x80 | 126;
      header[headerLen++] = (len >> 8) & 0xFF;
      header[headerLen++] = len & 0xFF;
    }
    else
    {
      header[headerLen++] = 0x80 | 127;
      for (int i = 7; i >= 0; i--)
      {
        header[headerLen++] = (len >> (i * 8)) & 0xFF;
      }
    }

    // Generate masking key
    uint8_t mask[4];
    for (int i = 0; i < 4; i++)
    {
      mask[i] = random(256);
      header[headerLen++] = mask[i];
    }

    // Send header
    wsClient.write(header, headerLen);

    // Send masked payload
    for (size_t i = 0; i < len; i++)
    {
      uint8_t masked = message[i] ^ mask[i % 4];
      wsClient.write(masked);
    }

    wsLastActivityMs = millis();
    return true;
  }

  void handleIncoming()
  {
    handleIncomingFrame();
  }

  bool isConnected()
  {
    return wsConnected;
  }

  bool isClientConnected()
  {
    return wsClient.connected();
  }

  unsigned long getLastActivityMs()
  {
    return wsLastActivityMs;
  }

  void disconnect()
  {
    wsClient.stop();
    wsConnected = false;
  }
}