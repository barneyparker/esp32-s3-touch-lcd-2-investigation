#include <time.h>
#include "ntp-time.h"
#include "logger.h"

const char *NTP_SERVER = "pool.ntp.org";

namespace ntp_time
{
  unsigned long long epochBaseMs = 0;
  unsigned long millisBase = 0;

  void sync()
  {
    logger::info("[NTP] Syncing time...");
    configTime(0, 0, NTP_SERVER);

    struct tm timeinfo;
    int attempts = 0;
    while (!getLocalTime(&timeinfo) && attempts < 10)
    {
      delay(500);
      attempts++;
    }
    if (getLocalTime(&timeinfo))
    {
      logger::info("[NTP] Time synchronized");
      // Use time() from <ctime>
      time_t epochSec = time(nullptr);
      epochBaseMs = (unsigned long long)epochSec * 1000ULL;
      millisBase = millis();
      logger::info("[NTP] Time base set: ");
      logger::info((unsigned long)(epochBaseMs / 1000));
      logger::infoLn(" seconds");
    }
    else
    {
      logger::info("[NTP] Time sync failed");
    }
  }

}