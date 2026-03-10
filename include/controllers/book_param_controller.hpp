// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#pragma once
#include "global.hpp"

#include "controllers/event_mgr.hpp"

class BookParamController
{
  private:
    static constexpr char const * TAG = "BookParamController";

    bool book_params_form_is_shown;
    bool wait_for_key_after_wifi;
    bool delete_current_book;
    bool waiting_revert_confirm;
    bool waiting_wifi_confirm;
    #if defined(BOARD_TYPE_PAPER_S3)
      bool on_sub_menu;
      bool waiting_usb_confirm;
    #endif

  public:
    BookParamController() : 
      book_params_form_is_shown(false), 
        wait_for_key_after_wifi(false),
            delete_current_book(false),
         waiting_revert_confirm(false),
           waiting_wifi_confirm(false)
    #if defined(BOARD_TYPE_PAPER_S3)
           ,on_sub_menu(false)
           ,waiting_usb_confirm(false)
    #endif
    { };

    void    input_event(const EventMgr::Event & event);
    void          enter();
    void          leave(bool going_to_deep_sleep = false);
    void set_font_count(uint8_t count);

    inline void set_book_params_form_is_shown() { book_params_form_is_shown  = true; }
    inline void   set_wait_for_key_after_wifi() { wait_for_key_after_wifi    = true; }
    inline void       set_delete_current_book() { delete_current_book        = true; }
    inline void    set_waiting_revert_confirm() { waiting_revert_confirm     = true; }
    inline void      set_waiting_wifi_confirm() { waiting_wifi_confirm       = true; }
    #if defined(BOARD_TYPE_PAPER_S3)
    inline void            set_on_sub_menu(bool val) { on_sub_menu = val; }
    inline void      set_waiting_usb_confirm() { waiting_usb_confirm = true; }
    #endif
};

#if __BOOK_PARAM_CONTROLLER__
  BookParamController book_param_controller;
#else
  extern BookParamController book_param_controller;
#endif
