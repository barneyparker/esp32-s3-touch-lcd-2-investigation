#ifndef STEPS_H
#define STEPS_H

#include <Arduino.h>

namespace steps
{
  void init();
  bool isDetected();
  void clearDetected();
  void flushBuffer();
  uint8_t getBufferSize();
}

#endif // STEPS_H