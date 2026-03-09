// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#pragma once
#include "global.hpp"

#include "controllers/event_mgr.hpp"

class OptionController
{
  private:
    static constexpr char const * TAG = "OptionController";

    bool main_form_is_shown;
    bool font_form_is_shown;
    bool books_refresh_needed;

    #if DATE_TIME_RTC
      bool date_time_form_is_shown;
    #endif
    #if INKPLATE_6PLUS
      bool calibration_is_shown;
    #endif

    bool wait_for_key_after_wifi;
    bool waiting_usb_confirm;
    bool waiting_clr_history_confirm;
    bool waiting_wifi_confirm;
    bool waiting_poweroff_confirm;
    bool on_sub_menu;

  public:
    OptionController() : main_form_is_shown(false), 
                         font_form_is_shown(false),
                         books_refresh_needed(false), 
                         #if DATE_TIME_RTC
                           date_time_form_is_shown(false),
                         #endif
                         #if INKPLATE_6PLUS
                           calibration_is_shown(false),
                         #endif
                         wait_for_key_after_wifi(false),
                         waiting_usb_confirm(false),
                         waiting_clr_history_confirm(false),
                         waiting_wifi_confirm(false),
                         waiting_poweroff_confirm(false),
                         on_sub_menu(false) { };
                         
    void    input_event(const EventMgr::Event & event);
    void          enter();
    void          leave(bool going_to_deep_sleep = false);
    void set_font_count(uint8_t count);
     
    inline void        set_main_form_is_shown() { main_form_is_shown      = true; }
    inline void        set_font_form_is_shown() { font_form_is_shown      = true; }

    #if DATE_TIME_RTC
      inline void set_date_time_form_is_shown() { date_time_form_is_shown = true; }
    #endif

    #if INKPLATE_6PLUS
      inline void    set_calibration_is_shown() { calibration_is_shown    = true; }
    #endif

    inline void set_wait_for_key_after_wifi() { 
      wait_for_key_after_wifi   = true; 
      main_form_is_shown        = false;
      font_form_is_shown        = false;
      #if DATE_TIME_RTC
        date_time_form_is_shown = false;
      #endif
      #if INKPLATE_6PLUS
        calibration_is_shown    = false;
      #endif
    }

    inline void set_waiting_usb_confirm() {
      waiting_usb_confirm         = true;
      main_form_is_shown          = false;
      font_form_is_shown          = false;
      wait_for_key_after_wifi     = false;
      #if DATE_TIME_RTC
        date_time_form_is_shown   = false;
      #endif
      #if INKPLATE_6PLUS
        calibration_is_shown      = false;
      #endif
    }

    inline void set_waiting_clr_history_confirm() {
      waiting_clr_history_confirm = true;
      main_form_is_shown          = false;
      font_form_is_shown          = false;
      wait_for_key_after_wifi     = false;
      waiting_usb_confirm         = false;
      waiting_wifi_confirm        = false;
    }

    inline void set_waiting_wifi_confirm() {
      waiting_wifi_confirm        = true;
      main_form_is_shown          = false;
      font_form_is_shown          = false;
      wait_for_key_after_wifi     = false;
      waiting_usb_confirm         = false;
      waiting_clr_history_confirm = false;
    }

    inline void set_waiting_poweroff_confirm() {
      waiting_poweroff_confirm    = true;
      main_form_is_shown          = false;
      font_form_is_shown          = false;
      wait_for_key_after_wifi     = false;
      waiting_usb_confirm         = false;
      waiting_clr_history_confirm = false;
      waiting_wifi_confirm        = false;
    }

    inline void set_on_sub_menu(bool val) { on_sub_menu = val; }
};

#if __OPTION_CONTROLLER__
  OptionController option_controller;
#else
  extern OptionController option_controller;
#endif
