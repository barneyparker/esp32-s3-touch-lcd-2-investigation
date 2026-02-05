#include "util.h"

#include <Arduino.h>

const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(const uint8_t *data, size_t len)
{
  String result;
  int i = 0;
  uint8_t arr3[3], arr4[4];

  while (len--)
  {
    arr3[i++] = *(data++);
    if (i == 3)
    {
      arr4[0] = (arr3[0] & 0xfc) >> 2;
      arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
      arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
      arr4[3] = arr3[2] & 0x3f;
      for (i = 0; i < 4; i++)
      {
        result += base64_chars[arr4[i]];
      }
      i = 0;
    }
  }

  if (i)
  {
    for (int j = i; j < 3; j++)
    {
      arr3[j] = 0;
    }
    arr4[0] = (arr3[0] & 0xfc) >> 2;
    arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
    arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
    arr4[3] = arr3[2] & 0x3f;
    for (int j = 0; j < i + 1; j++)
    {
      result += base64_chars[arr4[j]];
    }
    while (i++ < 3)
    {
      result += '=';
    }
  }

  return result;
}

// --- Exponential Backoff Calculation ---
const uint32_t MAX_BACKOFF_MS = 20000; // Maximum backoff delay (20 seconds)

uint32_t calculateBackoff(uint8_t attempts)
{
  if (attempts == 0)
    return 0;
  // Calculate 2^(attempts-1) seconds, capped at MAX_BACKOFF_MS
  uint32_t backoffMs = 1000; // Start at 1 second
  for (uint8_t i = 1; i < attempts && backoffMs < MAX_BACKOFF_MS; i++)
  {
    backoffMs *= 2;
  }
  return backoffMs > MAX_BACKOFF_MS ? MAX_BACKOFF_MS : backoffMs;
}