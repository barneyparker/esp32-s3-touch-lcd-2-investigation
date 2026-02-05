#include "logger.h"

namespace logger
{
  void init()
  {
    Serial.begin(115200);
  }

  // Specializations for bool
  template <>
  void info<bool>(const bool &msg)
  {
    Serial.print(msg ? "true" : "false");
  }

  template <>
  void infoLn<bool>(const bool &msg)
  {
    Serial.println(msg ? "true" : "false");
  }
}