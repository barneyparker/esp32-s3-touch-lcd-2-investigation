#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

namespace logger
{
  void init();

  template <typename T>
  void info(const T &msg)
  {
    Serial.print(msg);
  }

  template <typename T>
  void infoLn(const T &msg)
  {
    Serial.println(msg);
  }

  // Specializations for bool to print "true"/"false" instead of 1/0
  template <>
  void info<bool>(const bool &msg);

  template <>
  void infoLn<bool>(const bool &msg);
}

#endif // LOGGER_H