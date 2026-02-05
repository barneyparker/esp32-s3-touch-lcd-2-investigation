#include "led.h"

#define STATUS_LED_GPIO 2

namespace led
{
  void init()
  {
    pinMode(STATUS_LED_GPIO, OUTPUT);
    off();
  }

  void on()
  {
    digitalWrite(STATUS_LED_GPIO, HIGH);
  }

  void off()
  {
    digitalWrite(STATUS_LED_GPIO, LOW);
  }

  void toggle()
  {
    digitalWrite(STATUS_LED_GPIO, !digitalRead(STATUS_LED_GPIO));
  }

  void flash(int times, int delayMs)
  {
    for (int i = 0; i < times; i++)
    {
      on();
      delay(delayMs);
      off();
      delay(delayMs);
    }
  }

  void level(int brightness)
  {
    analogWrite(STATUS_LED_GPIO, brightness);
  }
}