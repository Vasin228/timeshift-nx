/**
 * @file        rex/platform/switch_compat.h
 * @brief       Small shims for names newlib on devkitA64 does not provide.
 *
 * newlib is freestanding-ish: it has no timegm(), no BSD sockets and no
 * <netinet/ip.h>. Only the pieces the runtime actually needs are shimmed here;
 * anything bigger (sockets) gets a real Horizon backend instead of a fake one.
 */

#pragma once

#if defined(__SWITCH__)

#include <time.h>

// newlib has no timegm(). This computes the same value without touching the
// process time zone: days since the epoch from the civil date (Howard Hinnant's
// algorithm), then the time of day. Correct for the whole range of struct tm.
// Kept C-compatible: rex/platform.h is included from C sources too.
static inline time_t rex_switch_timegm(struct tm* tm) {
  const int year = tm->tm_year + 1900;
  const int month = tm->tm_mon;  // 0-based
  const int day = tm->tm_mday;

  const int y = year - (month <= 1 ? 1 : 0);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);                                   // [0, 399]
  const unsigned doy = (153u * (unsigned)(month + (month > 1 ? -3 : 9)) + 2u) / 5u + (unsigned)(day - 1);
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;                    // [0, 146096]
  const long long days = (long long)era * 146097LL + (long long)doe - 719468LL;

  const long long seconds =
      days * 86400LL + (long long)tm->tm_hour * 3600LL + (long long)tm->tm_min * 60LL + (long long)tm->tm_sec;
  return (time_t)seconds;
}

#ifndef timegm
#define timegm rex_switch_timegm
#endif

#endif  // __SWITCH__
