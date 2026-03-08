#define __BATTERY__ 1
#include "battery.hpp"

// S3 Implementation
#if defined(BOARD_TYPE_PAPER_S3)

bool Battery::setup() {
    // 1. Setup Enable Pin
    gpio_reset_pin(BATTERY_EN_PIN);
    gpio_set_direction(BATTERY_EN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(BATTERY_EN_PIN, 0);

    // 2. Initialize ADC Unit 1
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE, 
    };
    adc_oneshot_new_unit(&init_config, &adc_handle);

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    
    // Configure BOTH channels
    adc_oneshot_config_channel(adc_handle, ADC_CHAN_VBAT, &config);
    adc_oneshot_config_channel(adc_handle, ADC_CHAN_USB, &config);

    // 3. Calibration (Only strictly needed for VBAT)
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHAN_VBAT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) == ESP_OK) {
        do_calibration = true;
    }
    
    return true;
}

bool Battery::is_usb_connected() {
    int adc_raw = 0;
    // Read the analog voltage on GPIO 5
    adc_oneshot_read(adc_handle, ADC_CHAN_USB, &adc_raw);
    
    // 12-bit ADC max is 4095 (~3.1V). 
    // 0.2V is roughly raw value 264. 
    // If it's greater than 150, the 5V rail is active.
    return adc_raw > 150; 
}

bool Battery::is_charging() {
    // Without a dedicated, working digital CHG pin, the presence of USB 
    // power on this circuit dictates that the PMIC is charging the battery.
    return is_usb_connected();
}

double Battery::read_level() {
    gpio_set_level(BATTERY_EN_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10)); 
    
    int adc_raw = 0;
    adc_oneshot_read(adc_handle, ADC_CHAN_VBAT, &adc_raw);
    gpio_set_level(BATTERY_EN_PIN, 0);

    if (do_calibration) {
        int voltage_mv = 0;
        adc_cali_raw_to_voltage(cali_handle, adc_raw, &voltage_mv);
        return (double)voltage_mv * 2.0 / 1000.0; 
    }
    return (double(adc_raw) * 3.3 * 2.0) / 4095.0;
}

#else
// Legacy M5Paper implementation...
bool Battery::setup() { return true; }
double Battery::read_level() { return 3.7; }
bool Battery::is_charging() { return false; }
bool Battery::is_usb_connected() { return false; }
#endif