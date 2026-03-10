// Copyright (c) 2026
// MIT License. Look at file licenses.txt for details.

#if defined(BOARD_TYPE_PAPER_S3)

#include "bm8563.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2c.h"
#include "esp_log.h"

#include <cstring>
#include <cstdlib>
#include <string>
#include <time.h>

static constexpr char const * TAG = "BM8563";

// ---------------------------------------------------------------------------
// BM8563 register addresses (PCF8563-compatible)
// ---------------------------------------------------------------------------
static constexpr uint8_t REG_CTRL1   = 0x00; // Control/Status 1
static constexpr uint8_t REG_CTRL2   = 0x01; // Control/Status 2
static constexpr uint8_t REG_SECONDS = 0x02; // VL_seconds  (VL = bit7)
static constexpr uint8_t REG_MINUTES = 0x03;
static constexpr uint8_t REG_HOURS   = 0x04;
static constexpr uint8_t REG_DAYS    = 0x05;
static constexpr uint8_t REG_WDAYS   = 0x06;
static constexpr uint8_t REG_MONTHS  = 0x07; // Century/Months (C = bit7)
static constexpr uint8_t REG_YEARS   = 0x08;
static constexpr uint8_t REG_ALRM_MIN  = 0x09; // Alarm minute  (AE = bit7)
static constexpr uint8_t REG_ALRM_HOUR = 0x0A; // Alarm hour
static constexpr uint8_t REG_ALRM_DAY  = 0x0B; // Alarm day
static constexpr uint8_t REG_ALRM_WDAY = 0x0C; // Alarm weekday
static constexpr uint8_t REG_CLKOUT    = 0x0D; // CLKOUT control
static constexpr uint8_t REG_TMR_CTRL  = 0x0E; // Timer control  (TE=bit7, TD=bits0-1)
static constexpr uint8_t REG_TMR_CNT   = 0x0F; // Timer countdown value

// CTRL2 bits
static constexpr uint8_t CTRL2_TIE = (1 << 0); // Timer Interrupt Enable
static constexpr uint8_t CTRL2_AIE = (1 << 1); // Alarm Interrupt Enable
static constexpr uint8_t CTRL2_TF  = (1 << 2); // Timer Flag
static constexpr uint8_t CTRL2_AF  = (1 << 3); // Alarm Flag

// Timer clock source (REG_TMR_CTRL bits 0-1)
static constexpr uint8_t TMR_TD_4096 = 0x00; // 4096 Hz
static constexpr uint8_t TMR_TD_64   = 0x01; // 64 Hz
static constexpr uint8_t TMR_TD_1HZ  = 0x02; // 1 Hz
static constexpr uint8_t TMR_TD_1_60 = 0x03; // 1/60 Hz (one tick per minute)
static constexpr uint8_t TMR_TE      = 0x80; // Timer Enable

// The BM8563 sits on the same port/pins as the GT911 touch controller.
static constexpr i2c_port_t I2C_PORT = I2C_NUM_0;

// Global singleton
BM8563 bm8563;

// ---------------------------------------------------------------------------
// Low-level I2C helpers (single-byte register address)
// ---------------------------------------------------------------------------

esp_err_t BM8563::write_reg(uint8_t reg, const uint8_t * data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) return ESP_FAIL;

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    if (data && len) {
        i2c_master_write(cmd, const_cast<uint8_t *>(data), len, true);
    }
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t BM8563::read_reg(uint8_t reg, uint8_t * data, size_t len)
{
    if (!data || !len) return ESP_ERR_INVALID_ARG;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) return ESP_FAIL;

    // Write register pointer
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);

    // Repeated start then read
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void BM8563::setup()
{
    // Clear STOP and TEST bits in Control/Status 1 — chip starts running.
    uint8_t ctrl1 = 0x00;
    if (write_reg(REG_CTRL1, &ctrl1, 1) != ESP_OK) {
        ESP_LOGW(TAG, "BM8563 not responding on I2C");
        valid_ = false;
        return;
    }

    // Clear all interrupt/alarm flags and disable interrupts.
    uint8_t ctrl2 = 0x00;
    write_reg(REG_CTRL2, &ctrl2, 1);

    // Disable timer.
    uint8_t tmr = 0x03; // TE=0, TD=11 (lowest power)
    write_reg(REG_TMR_CTRL, &tmr, 1);

    // Check VL bit in seconds register to know if oscillator has been valid.
    uint8_t sec = 0;
    if (read_reg(REG_SECONDS, &sec, 1) == ESP_OK) {
        valid_ = ((sec & 0x80) == 0); // VL=0 means time is valid
        if (!valid_) {
            ESP_LOGW(TAG, "BM8563 oscillator has stopped — time invalid, NTP sync required");
        } else {
            ESP_LOGI(TAG, "BM8563 OK — oscillator running");
        }
    }
}

bool BM8563::get_date_time(time_t * t) const
{
    if (!t) return false;
    *t = 0;

    uint8_t buf[7];
    if (read_reg(REG_SECONDS, buf, 7) != ESP_OK) return false;

    // VL bit set means the oscillator stopped — time is not reliable.
    if (buf[0] & 0x80) return false;

    struct tm tm_info = {};
    tm_info.tm_sec  = bcd_to_dec(buf[0] & 0x7F);
    tm_info.tm_min  = bcd_to_dec(buf[1] & 0x7F);
    tm_info.tm_hour = bcd_to_dec(buf[2] & 0x3F);
    tm_info.tm_mday = bcd_to_dec(buf[3] & 0x3F);
    // buf[4] = weekday — ignored, timegm() derives it
    uint8_t month_raw = buf[5];
    tm_info.tm_mon  = bcd_to_dec(month_raw & 0x1F) - 1; // tm_mon is 0-based
    // Century bit: 1=1900s, 0=2000s
    uint16_t year = bcd_to_dec(buf[6]);
    tm_info.tm_year = (month_raw & 0x80) ? (1900 + year - 1900) : (2000 + year - 1900);
    tm_info.tm_isdst = -1;

    // The BM8563 always stores UTC.  timegm() is a GNU extension not
    // available in ESP-IDF's newlib, so we temporarily override the TZ
    // environment variable to "UTC0", call mktime() (which then treats
    // the struct tm as UTC), then restore the original TZ.
    char * saved_tz = getenv("TZ");
    std::string tz_backup;
    if (saved_tz) tz_backup = saved_tz;

    setenv("TZ", "UTC0", 1);
    tzset();

    *t = mktime(&tm_info);

    if (!tz_backup.empty()) setenv("TZ", tz_backup.c_str(), 1);
    else                     unsetenv("TZ");
    tzset();

    return (*t != (time_t)-1);
}

bool BM8563::set_date_time(const time_t * t)
{
    if (!t) return false;

    struct tm tm_info;
    gmtime_r(t, &tm_info);

    // BM8563 year register: 0-99 relative to century
    int full_year = tm_info.tm_year + 1900; // e.g. 2026
    uint8_t year_bcd = dec_to_bcd((uint8_t)(full_year % 100));
    // Century bit: 0 = 2000-2099
    uint8_t century_bit = (full_year >= 2000) ? 0x00 : 0x80;

    uint8_t buf[7];
    buf[0] = dec_to_bcd((uint8_t)tm_info.tm_sec);             // seconds, VL=0
    buf[1] = dec_to_bcd((uint8_t)tm_info.tm_min);
    buf[2] = dec_to_bcd((uint8_t)tm_info.tm_hour);
    buf[3] = dec_to_bcd((uint8_t)tm_info.tm_mday);
    buf[4] = (uint8_t)tm_info.tm_wday;                        // weekday 0-6
    buf[5] = century_bit | dec_to_bcd((uint8_t)(tm_info.tm_mon + 1));
    buf[6] = year_bcd;

    esp_err_t ret = write_reg(REG_SECONDS, buf, 7);
    if (ret == ESP_OK) {
        const_cast<BM8563 *>(this)->valid_ = true;
    }
    return ret == ESP_OK;
}

bool BM8563::set_wakeup_alarm(uint32_t seconds_from_now)
{
    if (seconds_from_now == 0) return cancel_alarm();

    // First cancel any previously pending alarm/timer.
    cancel_alarm();

    uint8_t td_mode;
    uint8_t cnt;

    if (seconds_from_now <= 255) {
        // Use 1 Hz countdown: 1 tick = 1 second
        td_mode = TMR_TD_1HZ;
        cnt     = (uint8_t)seconds_from_now;
    } else if (seconds_from_now <= 255u * 60u) {
        // Use 1/60 Hz countdown: 1 tick = 1 minute
        td_mode = TMR_TD_1_60;
        cnt     = (uint8_t)((seconds_from_now + 59) / 60);
    } else {
        // For longer intervals (hours/days) use the alarm registers instead.
        // Compute target absolute time, then set minute+hour+day alarm.
        time_t now = 0;
        if (!get_date_time(&now)) return false;
        now += (time_t)seconds_from_now;

        struct tm tm_target;
        gmtime_r(&now, &tm_target);

        // Alarm registers: AE=0 means this field participates in match.
        uint8_t alrm[4];
        alrm[0] = dec_to_bcd((uint8_t)tm_target.tm_min);  // minute alarm
        alrm[1] = dec_to_bcd((uint8_t)tm_target.tm_hour); // hour alarm
        alrm[2] = dec_to_bcd((uint8_t)tm_target.tm_mday); // day alarm
        alrm[3] = 0x80; // weekday alarm disabled (AE=1)
        write_reg(REG_ALRM_MIN, alrm, 4);

        // Enable alarm interrupt in Control/Status 2.
        uint8_t ctrl2 = CTRL2_AIE;
        return write_reg(REG_CTRL2, &ctrl2, 1) == ESP_OK;
    }

    // Write countdown value first, then enable timer with chosen frequency.
    write_reg(REG_TMR_CNT, &cnt, 1);
    uint8_t tmr_ctrl = TMR_TE | td_mode;
    write_reg(REG_TMR_CTRL, &tmr_ctrl, 1);

    // Enable timer interrupt in Control/Status 2.
    uint8_t ctrl2 = CTRL2_TIE;
    return write_reg(REG_CTRL2, &ctrl2, 1) == ESP_OK;
}

bool BM8563::cancel_alarm()
{
    // Disable timer.
    uint8_t tmr = TMR_TD_1_60; // TE=0, low-power clock
    write_reg(REG_TMR_CTRL, &tmr, 1);

    // Disable all alarm interrupts and clear flags.
    uint8_t ctrl2 = 0x00;
    write_reg(REG_CTRL2, &ctrl2, 1);

    // Mask all alarm registers (AE=1 = disabled).
    uint8_t alrm_disabled = 0x80;
    for (uint8_t r = REG_ALRM_MIN; r <= REG_ALRM_WDAY; r++) {
        write_reg(r, &alrm_disabled, 1);
    }
    return true;
}

#endif // BOARD_TYPE_PAPER_S3
