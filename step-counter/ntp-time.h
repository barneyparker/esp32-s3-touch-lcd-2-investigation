#ifndef NTP_TIME_H
#define NTP_TIME_H

#include <Arduino.h>

namespace ntp_time
{
  extern unsigned long long epochBaseMs;
  extern unsigned long millisBase;

  void sync();
}

#endif // NTP_TIME_H