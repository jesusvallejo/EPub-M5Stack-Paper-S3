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

    fflush(NULL);
    esp_vfs_fat_unregister_path("/sdcard");

    if (start_msc(card) == ESP_OK) {
        // LOG_I("USB MSC Active. Waiting for host...");
        // uint32_t timeout = 0;
        
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(500)); // Yield CPU; no need to spin at 100%
            if (!battery.is_usb_connected()) {
                esp_restart(); // Reboot if USB is disconnected
            }
        //     //tud_task();  // VERY IMPORTANT

        //     vTaskDelay(pdMS_TO_TICKS(5));

        //     // if (!is_connected()) {
        //     //     if (++timeout > 50) { 
        //     //         LOG_I("USB disconnected or timeout. Rebooting...");
        //     //         // Back in the main app
        //     //         //USBEmulation::stop_msc();

        //     //         // Re-mount the SD card so the E-Reader can see files again
        //     //         //inkplate_platform.setup(true);
        //     //     }
        //     // } else {
        //     //     timeout = 0;
        //     // }
        }
    }
}

esp_err_t USBEmulation::start_msc(sdmmc_card_t* card) {
    // 1. Basic TinyUSB Driver Configuration
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) return ret;

    // 2. Map the SD Card to the MSC Storage using the new API
    tinyusb_msc_storage_handle_t storage_hdl;
    const tinyusb_msc_storage_config_t cfg = {
        .medium = {
            .card = card // Using the medium.card field you found
        }
    };

    // This is the correct function for SDMMC-based Mass Storage
    return tinyusb_msc_new_storage_sdmmc(&cfg, &storage_hdl);
}

bool USBEmulation::is_connected() {
    return tud_mounted();
}

void USBEmulation::stop_msc() {
    LOG_I("Shutting down USB MSC...");
    // 1. Uninstall the TinyUSB driver
    tinyusb_driver_uninstall();
    
    // 2. Clear the global card pointer
    // card = nullptr;
}