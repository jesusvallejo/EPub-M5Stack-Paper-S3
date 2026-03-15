// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#include "global.hpp"

#if defined(BOARD_TYPE_PAPER_S3)

#include "controllers/event_mgr.hpp"
#include "controllers/app_controller.hpp"
#include "viewers/battery_viewer.hpp"

#include "battery.hpp"
#include "screen.hpp"
#include "bm8563.hpp"
#include "inkplate_platform.hpp"
#include "models/config.hpp"
#include "viewers/msg_viewer.hpp"
#include "esp.hpp"

// Shared flag: set true by touch_task, read by InkPlatePlatform::light_sleep()
// to decide whether to exit the sleep loop.
extern volatile bool paper_s3_touch_wakeup;

#if EPUB_INKPLATE_BUILD
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/queue.h"
  #include "driver/i2c.h"
  #include "esp_log.h"
  #include <cmath> 
  #include <sys/time.h>
#endif

extern Battery battery; 
EventMgr event_mgr;

#if EPUB_INKPLATE_BUILD

static constexpr char const * TAG = "EventMgrPaperS3";

static const gpio_num_t PAPERS3_GT911_SDA_GPIO = GPIO_NUM_41;
static const gpio_num_t PAPERS3_GT911_SCL_GPIO = GPIO_NUM_42;
static const i2c_port_t PAPERS3_GT911_I2C_PORT = I2C_NUM_0;

static uint8_t gt911_addr = 0x14;
static bool    gt911_ok   = false;

static QueueHandle_t input_event_queue = nullptr;

// --- GT911 I2C Helper Functions ---

static esp_err_t gt911_write_reg(uint8_t addr, uint16_t reg, const uint8_t * data, size_t len)
{
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (cmd == nullptr) return ESP_FAIL;

  uint8_t reg_hi = (uint8_t)(reg >> 8);
  uint8_t reg_lo = (uint8_t)(reg & 0xFF);

  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg_hi, true);
  i2c_master_write_byte(cmd, reg_lo, true);
  if ((data != nullptr) && (len != 0)) {
    i2c_master_write(cmd, (uint8_t *)data, len, true);
  }
  i2c_master_stop(cmd);

  esp_err_t ret = i2c_master_cmd_begin(PAPERS3_GT911_I2C_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return ret;
}

static esp_err_t gt911_read_reg(uint8_t addr, uint16_t reg, uint8_t * data, size_t len)
{
  if ((data == nullptr) || (len == 0)) return ESP_ERR_INVALID_ARG;

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (cmd == nullptr) return ESP_FAIL;

  uint8_t reg_hi = (uint8_t)(reg >> 8);
  uint8_t reg_lo = (uint8_t)(reg & 0xFF);

  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg_hi, true);
  i2c_master_write_byte(cmd, reg_lo, true);

  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);

  if (len > 1) {
    i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
  }
  i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
  i2c_master_stop(cmd);

  esp_err_t ret = i2c_master_cmd_begin(PAPERS3_GT911_I2C_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return ret;
}

static bool gt911_read_point(uint16_t * x, uint16_t * y)
{
  if (!gt911_ok || (x == nullptr) || (y == nullptr)) return false;

  uint8_t status = 0;
  if (gt911_read_reg(gt911_addr, 0x814E, &status, 1) != ESP_OK) return false;

  if ((status & 0x80) == 0) return false;

  uint8_t points = status & 0x0F;
  if (points == 0) {
    uint8_t zero = 0;
    gt911_write_reg(gt911_addr, 0x814E, &zero, 1);
    return false;
  }

  uint8_t data[4] = { 0 };
  if (gt911_read_reg(gt911_addr, 0x8150, data, sizeof(data)) != ESP_OK) return false;

  *x = (uint16_t)((data[1] << 8) | data[0]);
  *y = (uint16_t)((data[3] << 8) | data[2]);

  uint8_t zero = 0;
  gt911_write_reg(gt911_addr, 0x814E, &zero, 1);

  return true;
}

// --- Background Monitoring Tasks ---

/**
 * Real-time Battery Monitor Task
 * - Sends BATTERY_UPDATE immediately when USB plug/unplug is detected.
 * - Sends BATTERY_UPDATE every 60 s for the periodic minute refresh.
 */
static void battery_monitor_task(void * param)
{
    (void)param;
    bool  last_usb     = battery.is_usb_connected();
    int   tick_count   = 0;
    const int MINUTE_TICKS = 60;  // fires every 1 s, so 60 = 1 minute

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        tick_count++;

        bool current_usb = battery.is_usb_connected();
        bool usb_changed = (current_usb != last_usb);

        if (usb_changed || (tick_count >= MINUTE_TICKS)) {
            if (usb_changed) last_usb = current_usb;
            if (tick_count >= MINUTE_TICKS) tick_count = 0;

            EventMgr::Event ev;
            ev.kind = EventMgr::EventKind::BATTERY_UPDATE;
            ev.x = 0; ev.y = 0; ev.dist = 0;
            if (input_event_queue) xQueueSend(input_event_queue, &ev, 0);
        }
    }
}

/**
 * Touch Task
 */
static void touch_task(void * param)
{
  (void)param;
  constexpr uint16_t swipe_threshold          = 100; 
  constexpr uint16_t longpress_move_threshold =  30; 
  constexpr uint32_t longpress_ms             = 600; 

  bool       touch_active = false;
  bool       hold_sent    = false;
  uint16_t   start_x      = 0;
  uint16_t   start_y      = 0;
  uint16_t   current_x    = 0;
  uint16_t   current_y    = 0;
  TickType_t start_tick   = 0;

  while (true) {
    uint16_t x = 0;
    uint16_t y = 0; 
    bool has_touch = gt911_read_point(&x, &y);

    if (has_touch) {
      if (!touch_active) {
        touch_active = true;
        hold_sent    = false;
        start_tick   = xTaskGetTickCount();
        start_x = current_x = x;
        start_y = current_y = y;
      } else {
        current_x = x;
        current_y = y;
      }

      if (touch_active && !hold_sent) {
        TickType_t now = xTaskGetTickCount();
        uint32_t dt_ms = (now - start_tick) * portTICK_PERIOD_MS;
        int dx = std::abs((int)current_x - (int)start_x);
        int dy = std::abs((int)current_y - (int)start_y);

        if ((dt_ms >= longpress_ms) && (dx <= (int)longpress_move_threshold) && (dy <= (int)longpress_move_threshold)) {
          EventMgr::Event ev;
          ev.kind = EventMgr::EventKind::HOLD;
          ev.x = start_x; ev.y = start_y;
          paper_s3_touch_wakeup = true; // wake light_sleep() poll loop
          if (input_event_queue) xQueueSend(input_event_queue, &ev, 0);
          hold_sent = true;
        }
      }
    }
    else if (touch_active) {
      touch_active = false;
      EventMgr::Event ev;
      ev.x = start_x; ev.y = start_y;
      ev.kind = EventMgr::EventKind::NONE;

      int dx = (int)current_x - (int)start_x;
      int dy = (int)start_y - (int)current_y;
      int abs_dx = std::abs(dx);
      int abs_dy = std::abs(dy);

      if (hold_sent) {
        ev.kind = EventMgr::EventKind::RELEASE;
      } else if ((abs_dx > abs_dy) && (abs_dx > (int)swipe_threshold)) {
        ev.kind = (dx > 0) ? EventMgr::EventKind::SWIPE_RIGHT : EventMgr::EventKind::SWIPE_LEFT;
      } else {
        ev.kind = EventMgr::EventKind::TAP;
      }

      if (ev.kind != EventMgr::EventKind::NONE && input_event_queue) {
        paper_s3_touch_wakeup = true; // wake light_sleep() poll loop
        xQueueSend(input_event_queue, &ev, 0);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

#endif // EPUB_INKPLATE_BUILD

bool EventMgr::setup()
{
#if EPUB_INKPLATE_BUILD
  if (input_event_queue == nullptr) {
    input_event_queue = xQueueCreate(10, sizeof(Event));
  }

  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = PAPERS3_GT911_SDA_GPIO;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_io_num = PAPERS3_GT911_SCL_GPIO;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = 400000;

  i2c_param_config(PAPERS3_GT911_I2C_PORT, &conf);
  i2c_driver_install(PAPERS3_GT911_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

  // BM8563 RTC sits on the same I2C bus — initialise now that the driver is up.
  bm8563.setup();
  // If the oscillator has been running, sync the ESP32 system clock from the
  // BM8563 so that time() calls are correct immediately (before any NTP sync).
  if (bm8563.is_valid()) {
    time_t rtc_time = 0;
    if (bm8563.get_date_time(&rtc_time)) {
      timeval tv;
      tv.tv_sec  = rtc_time;
      tv.tv_usec = 0;
      settimeofday(&tv, nullptr);
      ESP_LOGI("EventMgr", "System clock set from BM8563: %lld", (long long)rtc_time);
    }
  }

  uint8_t buf = 0;
  if (gt911_read_reg(0x14, 0x8140, &buf, 1) == ESP_OK) { gt911_addr = 0x14; gt911_ok = true; }
  else if (gt911_read_reg(0x5D, 0x8140, &buf, 1) == ESP_OK) { gt911_addr = 0x5D; gt911_ok = true; }

  xTaskCreatePinnedToCore(touch_task,           "touch",   4096, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(battery_monitor_task, "bat_mon", 2048, nullptr, 2, nullptr, 1);

#endif
  return true;
}

void EventMgr::loop()
{
#if EPUB_INKPLATE_BUILD
  while (true) {
    const Event & event = get_event(); // blocks up to 15 s

    if (event.kind != EventKind::NONE) {
      app_controller.input_event(event);
      return;
    }

    // get_event() timed out (no user input for 15 s). Enter light sleep
    // unless something has requested we stay awake (e.g. WiFi active).
    if (!stay_on) {
      int8_t timeout_minutes = 15; // safe default
      config.get(Config::Ident::TIMEOUT, &timeout_minutes);

      LOG_I("No input for 15 s. Light sleep for %d min...", (int)timeout_minutes);
      ESP::delay(500); // brief settle before CPU halts

      if (inkplate_platform.light_sleep((uint32_t)timeout_minutes,
                                        GPIO_NUM_48, 0)) {
        // light_sleep() returned true: the full timeout elapsed without
        // a touch. Proceed to deep sleep.
        app_controller.going_to_deep_sleep();
        screen.force_full_update();
        msg_viewer.show(
          MsgViewer::MsgType::INFO, false, true,
          "Deep Sleep",
          "No activity for %d minutes. The device is entering deep sleep.\n"
          "Press the side button to restart.",
          (int)timeout_minutes);
        ESP::delay(1000);
        inkplate_platform.deep_sleep(); // never returns
      }
      // light_sleep() returned false: a touch woke the device.
      // Loop back to get_event() which will pick up the queued touch event.
    }
  }
#endif
}

const EventMgr::Event & EventMgr::get_event()
{
  static Event event;
  event.kind = EventKind::NONE;

#if EPUB_INKPLATE_BUILD
  if (input_event_queue != nullptr) {
    // Wait up to 15 seconds for an event.  If nothing arrives the caller
    // (loop()) will invoke the light-sleep / deep-sleep sequence.
    if (!xQueueReceive(input_event_queue, &event, pdMS_TO_TICKS(15000))) {
      event.kind = EventKind::NONE; // timeout — no event
    }
  }
#endif
  return event;
}

void EventMgr::set_orientation(Screen::Orientation) {}

#endif // BOARD_TYPE_PAPER_S3