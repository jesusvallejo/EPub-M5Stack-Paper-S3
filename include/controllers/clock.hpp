#pragma once

#include "global.hpp"

#if DATE_TIME_RTC

#if EPUB_INKPLATE_BUILD
  #include "inkplate_platform.hpp"
  #if defined(BOARD_TYPE_PAPER_S3)
    #include "bm8563.hpp"
  #endif
#endif

#include "logging.hpp"

#include <sys/time.h>

class Clock
{
  private:
    static constexpr char const * TAG = "Clock";

  public:
    static void set_date_time(const time_t & tm) {
      #if EPUB_INKPLATE_BUILD && defined(BOARD_TYPE_PAPER_S3)
        // Write to the hardware BM8563 so time survives deep sleep.
        bm8563.set_date_time(&tm);
        // Also sync the ESP32 system clock for immediate use.
        timeval tv;
        tv.tv_sec = tm;
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
      #elif EPUB_INKPLATE_BUILD
        if (rtc.is_present()) {
          rtc.set_date_time(&tm);
        }
        else {
          timeval tv;
          tv.tv_sec = tm;
          tv.tv_usec = 0;
          settimeofday(&tv, nullptr);
        }
      #else
        timeval tv;
        tv.tv_sec = tm;
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
      #endif
    }

    static void get_date_time(time_t & t) {
      #if EPUB_INKPLATE_BUILD && defined(BOARD_TYPE_PAPER_S3)
        // Prefer the BM8563 hardware clock; fall back to system time if
        // the oscillator has not been set yet (VL flag, before first NTP sync).
        if (!bm8563.get_date_time(&t)) {
          LOG_D("BM8563 time invalid — using system clock");
          time(&t);
        }
      #elif EPUB_INKPLATE_BUILD
        if (rtc.is_present()) {
          LOG_D("RTC chip is present");
          rtc.get_date_time(&t);
        }
        else {
          LOG_D("RTC chip is NOT present");
          time(&t);
        }
      #else
        time(&t);
      #endif
    }
};

#endif