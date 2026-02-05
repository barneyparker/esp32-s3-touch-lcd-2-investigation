#ifndef UTIL_H
#define UTIL_H

#include <Arduino.h>
#include <cstdint>

String base64Encode(const uint8_t *data, size_t len);
uint32_t calculateBackoff(uint8_t attempts);

#endif
