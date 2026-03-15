#include "inkplate_platform.hpp"

#if defined(BOARD_TYPE_PAPER_S3)

#include <cstdio>

#include "logging.hpp"

#include "esp_err.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_types.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// epdiy EPD power control (epd_poweron / epd_poweroff)
extern "C" {
  #include <epdiy.h>
}

#include "bm8563.hpp"
extern BM8563 bm8563;

InkPlatePlatform InkPlatePlatform::singleton;
InkPlatePlatform & inkplate_platform = InkPlatePlatform::get_singleton();

// Simple SD card state for Paper S3
static sdmmc_card_t * s_sd_card = nullptr;
static sdmmc_host_t   s_sd_host = SDSPI_HOST_DEFAULT();
#if defined(BOARD_TYPE_PAPER_S3)
  Battery battery;
#endif

// ---------------------------------------------------------------------------
// Shared touch-detection flag.
// The touch_task in event_mgr_paper_s3.cpp sets this to true whenever it
// detects a touch while the device is in light sleep, so that light_sleep()
// can exit the sleep loop without relying solely on the hardware INT signal.
// ---------------------------------------------------------------------------
volatile bool paper_s3_touch_wakeup = false;

bool InkPlatePlatform::setup(bool sd_card_init)
{

    // Battery
  if (!battery.setup()) {
    LOG_E("Battery setup not completed!");
    return false;
  }

  LOG_I("Paper S3 InkPlatePlatform setup (sd_card_init=%d)", sd_card_init ? 1 : 0);

  if (sd_card_init && (s_sd_card == nullptr)) {
    esp_err_t ret;

    // Mount SD card at /sdcard using SPI host and the SD_CARD_PIN_NUM_* pins.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 10,
      .allocation_unit_size = 16 * 1024
    };

    spi_bus_config_t bus_cfg = {
      .mosi_io_num = SD_CARD_PIN_NUM_MOSI,
      .miso_io_num = SD_CARD_PIN_NUM_MISO,
      .sclk_io_num = SD_CARD_PIN_NUM_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 0,
      .flags = 0,
      .intr_flags = 0
    };

    ret = spi_bus_initialize(static_cast<spi_host_device_t>(s_sd_host.slot), &bus_cfg, SPI_DMA_CH_AUTO);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
      LOG_E("Paper S3: Failed to initialize SD SPI bus (%s)", esp_err_to_name(ret));
      return false;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CARD_PIN_NUM_CS;
    slot_config.host_id = static_cast<spi_host_device_t>(s_sd_host.slot);

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &s_sd_host, &slot_config, &mount_config, &s_sd_card);
    if (ret != ESP_OK) {
      LOG_E("Paper S3: Failed to mount SD card at /sdcard (%s)", esp_err_to_name(ret));
      return false;
    }

    sdmmc_card_print_info(stdout, s_sd_card);
  }


  return true;
}

bool InkPlatePlatform::light_sleep(uint32_t minutes_to_sleep, gpio_num_t gpio_num, int level)
{
  static constexpr char const * TAG = "InkPlatePlatform";

  // 1. Power off the EPD high-voltage supply.
  //    E-ink is bistable: the displayed image persists without power.
  epd_poweroff();

  // 2. Configure the GT911 INT pin (gpio_num, typically GPIO48) as an
  //    input with pull-up. GT911 drives it LOW when touch data is ready.
  {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask  = 1ULL << (uint32_t)gpio_num;
    io_conf.mode          = GPIO_MODE_INPUT;
    io_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type     = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
  }

  // 3. Enable GPIO wakeup from light sleep on the touch INT level.
  gpio_int_type_t wake_type = (level == 0) ? GPIO_INTR_LOW_LEVEL
                                            : GPIO_INTR_HIGH_LEVEL;
  gpio_wakeup_enable(gpio_num, wake_type);
  esp_sleep_enable_gpio_wakeup();

  // 4. Reset the shared touch flag before entering the sleep loop.
  paper_s3_touch_wakeup = false;

  // 5. Sleep loop: wake every 5 seconds (timer fallback) OR immediately
  //    when the GT911 INT assertion is received (GPIO wakeup).
  //    On each timer wakeup, yield 150 ms so the touch_task can run and
  //    set paper_s3_touch_wakeup if a touch is pending.
  const uint64_t INTERVAL_US = 5000000ULL;  // 5-second wake interval
  uint64_t total_us = (uint64_t)minutes_to_sleep * 60ULL * 1000000ULL;
  uint64_t elapsed  = 0;

  while (elapsed < total_us) {
    uint64_t remaining  = total_us - elapsed;
    uint64_t this_sleep = (remaining < INTERVAL_US) ? remaining : INTERVAL_US;

    esp_sleep_enable_timer_wakeup(this_sleep);
    esp_light_sleep_start();

    elapsed += this_sleep;

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // GPIO wakeup: GT911 INT asserted (touch event).
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
      LOG_I("Light sleep ended by GPIO%d (touch INT).", (int)gpio_num);
      epd_poweron();
      paper_s3_touch_wakeup = false;
      return false;   // woken by touch — resume normal operation
    }

    // Timer wakeup: yield briefly so the touch_task can poll GT911 and
    // set the shared flag if a touch occurred during sleep.
    vTaskDelay(pdMS_TO_TICKS(150));

    if (paper_s3_touch_wakeup) {
      LOG_I("Light sleep: touch_task detected a touch.");
      epd_poweron();
      paper_s3_touch_wakeup = false;
      return false;   // woken by touch
    }

    // Fast-path: GT911 INT still asserted after the yield.
    if (gpio_get_level(gpio_num) == level) {
      LOG_I("Light sleep: GT911 INT asserted on GPIO%d.", (int)gpio_num);
      epd_poweron();
      paper_s3_touch_wakeup = false;
      return false;   // woken by touch
    }
  }

  // Reached here: sleep period expired without any touch.
  LOG_I("Light sleep: timed out after %u min.", minutes_to_sleep);
  // Do NOT call epd_poweron() here — deep sleep follows immediately.
  return true;  // timed out
}

void InkPlatePlatform::deep_sleep(gpio_num_t gpio_num, int level)
{
  static constexpr char const * TAG = "InkPlatePlatform";
  (void)gpio_num;
  (void)level;

  LOG_I("Paper S3: entering deep sleep. Press the side button to restart.");

  // 1. Power off the EPD high-voltage supply to minimise current draw.
  //    The e-ink image is bistable; it persists without power.
  epd_poweroff();

  // 2. Cancel any pending BM8563 alarm/timer so the PMS150G power-management
  //    micro does not automatically restart the device after a countdown.
  //    With no alarm pending, only pressing the physical side button can
  //    trigger the PMS150G to power-cycle (restart) the system.
  bm8563.cancel_alarm();

  // 3. Disable all ESP32 software wakeup sources.
  //    The device can only be restarted by the hardware side button, which
  //    causes the PMS150G to perform a full power cycle of the board.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  // 4. Enter ESP32 deep sleep.
  //    ESP32-S3 RTC controller draws ~8 µA.
  //    The PMS150G + BM8563 remain active with < 1 µA combined.
  esp_deep_sleep_start();  // never returns
}

sdmmc_card_t* InkPlatePlatform::get_sd_card() {
    return s_sd_card;
}

#endif // BOARD_TYPE_PAPER_S3
