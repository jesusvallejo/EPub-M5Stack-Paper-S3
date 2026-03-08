#pragma once

#include "non_copyable.hpp"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"

#if defined(BOARD_TYPE_PAPER_S3)
  #include "esp_adc/adc_cali.h"
  #include "esp_adc/adc_cali_scheme.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
#endif

class Battery : NonCopyable
{
  public:
#if defined(BOARD_TYPE_PAPER_S3)
    Battery() : adc_handle(nullptr), cali_handle(nullptr), do_calibration(false) {} 
#else
    Battery(class IOExpander & _io_expander) : io_expander(_io_expander), adc_handle(nullptr) {}
#endif

    bool   setup();
    double read_level();
    bool   is_charging();      
    bool   is_usb_connected(); 

  private:
    static constexpr char const * TAG = "Battery";

#if defined(BOARD_TYPE_PAPER_S3)
    static constexpr gpio_num_t BATTERY_EN_PIN  = GPIO_NUM_2;
    // S3 ADC mappings: GPIO 3 is CH2, GPIO 5 is CH4
    static constexpr adc_channel_t ADC_CHAN_VBAT = ADC_CHANNEL_2; 
    static constexpr adc_channel_t ADC_CHAN_USB  = ADC_CHANNEL_4; 
    
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t cali_handle;
    bool do_calibration;
#else
    class IOExpander & io_expander;
    const uint8_t BATTERY_SWITCH = 9; 
    static constexpr adc_channel_t ADC_CHAN_VBAT = ADC_CHANNEL_7;
    adc_oneshot_unit_handle_t adc_handle;
#endif
};