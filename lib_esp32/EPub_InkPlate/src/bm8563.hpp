// Copyright (c) 2026
// MIT License. Look at file licenses.txt for details.
//
// BM8563 RTC driver for the M5Stack Paper S3.
// The BM8563 is a PCF8563-compatible RTC chip with a built-in alarm whose
// INT output is wired to the PMS150G power-management micro.  Setting an
// alarm therefore triggers a power cycle via the PMS150G, waking the device
// from true power-off.
//
// The chip shares I2C_NUM_0 with the GT911 touch controller
// (SDA = GPIO_41, SCL = GPIO_42) at address 0x51.

#pragma once

#if defined(BOARD_TYPE_PAPER_S3)

#include "esp_err.h"
#include <ctime>
#include <cstdint>

class BM8563
{
  public:
    // I2C bus address
    static constexpr uint8_t I2C_ADDR = 0x51;

    // Call once after EventMgr has installed the I2C driver.
    // Clears stop/test bits in Control/Status registers.
    void setup();

    // Returns true if the hardware was found and the oscillator has not lost
    // power since the last set_date_time() call (VL flag = 0).
    bool is_valid() const { return valid_; }

    // Read the current time from the RTC. On success returns true and fills *t.
    // If the oscillator has lost power (VL bit) returns false and sets *t = 0.
    bool get_date_time(time_t * t) const;

    // Write the current time to the RTC and clear the VL flag.
    bool set_date_time(const time_t * t);

    // Set a one-shot alarm N seconds in the future that will drive INT low.
    // INT is wired to the PMS150G: when it fires the PMS150G power-cycles
    // the device (equivalent to pressing the button).
    // Pass seconds_from_now = 0 to cancel any pending alarm.
    bool set_wakeup_alarm(uint32_t seconds_from_now);

    // Cancel any pending alarm and disable the alarm interrupt.
    bool cancel_alarm();

  private:
    bool valid_ = false;

    // Raw I2C helpers — single-byte register address (unlike GT911's 2-byte).
    static esp_err_t write_reg(uint8_t reg, const uint8_t * data, size_t len);
    static esp_err_t read_reg (uint8_t reg,       uint8_t * data, size_t len);

    static uint8_t bcd_to_dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
    static uint8_t dec_to_bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }
};

// Global singleton — defined in bm8563.cpp
extern BM8563 bm8563;

#endif // BOARD_TYPE_PAPER_S3
