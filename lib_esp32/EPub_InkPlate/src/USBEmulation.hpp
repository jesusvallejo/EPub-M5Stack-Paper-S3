#pragma once
#if defined(BOARD_TYPE_PAPER_S3)
#include "sdmmc_cmd.h"
#include "esp_err.h"
#include "tinyusb.h"
#include "tinyusb_msc.h"
#include "tusb.h" // Gives us access to tud_mounted()

class USBEmulation {
public:
    static void run_msc_session(sdmmc_card_t* card);
    static bool is_connected();

private:
    static esp_err_t start_msc(sdmmc_card_t* card);
    static void stop_msc();
};
#endif // BOARD_TYPE_PAPER_S3