#ifndef LED_H
#define LED_H

#include <Arduino.h>

namespace led
{
  void init();
  void on();
  void off();
  void toggle();
  void flash(int times, int delayMs);
  void level(int brightness);
}

#endif // LED_H