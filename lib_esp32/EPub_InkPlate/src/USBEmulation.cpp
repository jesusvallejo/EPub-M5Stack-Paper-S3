#include "USBEmulation.hpp"
#include "logging.hpp"
#include "esp_vfs_fat.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb_default_config.h"
#include "inkplate_platform.hpp"
#include "battery.hpp"

static const char* TAG = "USB_EMU";

// Note: We do NOT define tud_mount_cb here anymore to avoid the 
// "multiple definition" linker error.

void USBEmulation::run_msc_session(sdmmc_card_t* card) {
    if (!card) {
        LOG_E("Cannot start MSC: SD card pointer is null.");
        return;
    }

    if (start_msc(card) == ESP_OK) {        
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(500)); // Yield CPU; no need to spin at 100%
            if (!battery.is_usb_connected()) {
                //esp_restart(); // Reboot if USB is disconnected
            }
        }
    }
}

esp_err_t USBEmulation::start_msc(sdmmc_card_t* card) {
    tinyusb_msc_storage_handle_t storage_hdl;
    const tinyusb_msc_storage_config_t cfg = {
        .medium = {
            .card = card
        }
    };
    esp_err_t ret = tinyusb_msc_new_storage_sdmmc(&cfg, &storage_hdl);
    if (ret != ESP_OK) return ret;
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

    tusb_cfg.task.priority = configMAX_PRIORITIES - 2;
    return tinyusb_driver_install(&tusb_cfg);
}

bool USBEmulation::is_connected() {
    return tud_mounted();
}

void USBEmulation::stop_msc() {
    LOG_I("Shutting down USB MSC...");
    tinyusb_driver_uninstall();
}