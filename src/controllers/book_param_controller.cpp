// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#define __BOOK_PARAM_CONTROLLER__ 1
#include "controllers/book_param_controller.hpp"

#include "controllers/app_controller.hpp"
#include "controllers/common_actions.hpp"
#include "controllers/books_dir_controller.hpp"
#include "controllers/book_controller.hpp"
#include "models/books_dir.hpp"
#include "models/epub.hpp"
#include "models/config.hpp"
#include "models/page_locs.hpp"
#include "models/toc.hpp"
#include "viewers/menu_viewer.hpp"
#include "viewers/form_viewer.hpp"
#include "viewers/msg_viewer.hpp"

#if EPUB_INKPLATE_BUILD
  #include "esp_system.h"
#endif
#if EPUB_INKPLATE_BUILD && !defined(BOARD_TYPE_PAPER_S3)
  #include "eink.hpp"
  #include "esp.hpp"
  #include "soc/rtc.h"
#endif
#if defined(BOARD_TYPE_PAPER_S3)
  #include "inkplate_platform.hpp"
  #include "USBEmulation.hpp"
  extern InkPlatePlatform & inkplate_platform;
#endif

#include <sys/stat.h>

static int8_t show_images;
static int8_t font_size;
static int8_t use_fonts_in_book;
static int8_t font;
static int8_t done_res;

static int8_t old_font_size;
static int8_t old_show_images;
static int8_t old_use_fonts_in_book;
static int8_t old_font;

#if INKPLATE_6PLUS || TOUCH_TRIAL
  static constexpr int8_t BOOK_PARAMS_FORM_SIZE = 5;
#else
  static constexpr int8_t BOOK_PARAMS_FORM_SIZE = 4;
#endif
static FormEntry book_params_form_entries[BOOK_PARAMS_FORM_SIZE] = {
  { .caption = "Font Size:",
    .u = { .ch = { .value = &font_size,
                   .choice_count = 4,
                   .choices = FormChoiceField::font_size_choices } },
    .entry_type = FormEntryType::HORIZONTAL },
  { .caption = "Use fonts in book:",
    .u = { .ch = { .value = &use_fonts_in_book,
                   .choice_count = 2,
                   .choices = FormChoiceField::yes_no_choices } },
    .entry_type = FormEntryType::HORIZONTAL },
  { .caption = "Font:",
    .u = { .ch = { .value = &font,
                   .choice_count = 8,
                   .choices = FormChoiceField::font_choices } },
    .entry_type = FormEntryType::VERTICAL },
  { .caption = "Show Images in book:",
    .u = { .ch = { .value = &show_images,
                   .choice_count = 2,
                   .choices = FormChoiceField::yes_no_choices } },
    .entry_type = FormEntryType::HORIZONTAL },
  #if INKPLATE_6PLUS || TOUCH_TRIAL
    { .caption = " DONE ",
      .u = { .ch = { .value = &done_res,
                     .choice_count = 0,
                     .choices = nullptr } },
      .entry_type = FormEntryType::DONE }
  #endif
};

static void
book_parameters()
{
  BookParams * book_params = epub.get_book_params();

  book_params->get(BookParams::Ident::SHOW_IMAGES,        &show_images      );
  book_params->get(BookParams::Ident::FONT_SIZE,          &font_size        );
  book_params->get(BookParams::Ident::USE_FONTS_IN_BOOK,  &use_fonts_in_book);
  book_params->get(BookParams::Ident::FONT,               &font             );
  
  if (show_images       == -1) config.get(Config::Ident::SHOW_IMAGES,        &show_images      );
  if (font_size         == -1) config.get(Config::Ident::FONT_SIZE,          &font_size        );
  if (use_fonts_in_book == -1) config.get(Config::Ident::USE_FONTS_IN_BOOKS, &use_fonts_in_book);
  if (font              == -1) config.get(Config::Ident::DEFAULT_FONT,       &font             );
  
  old_show_images        = show_images;
  old_use_fonts_in_book  = use_fonts_in_book;
  old_font               = font;
  old_font_size          = font_size;
  done_res               = 1;

  form_viewer.show(
    book_params_form_entries, 
    BOOK_PARAMS_FORM_SIZE, 
    "(Any item change will trigger book refresh)");

  book_param_controller.set_book_params_form_is_shown();
}

static void
revert_to_defaults()
{
  msg_viewer.show(MsgViewer::MsgType::CONFIRM, true, true,
                  "Revert e-book parameters",
                  "All parameters for this e-book will be reset to the "
                  "application defaults. Continue?");
  book_param_controller.set_waiting_revert_confirm();
}

static void 
books_list()
{
  app_controller.set_controller(AppController::Ctrl::DIR);
}

static void
delete_book()
{
  msg_viewer.show(MsgViewer::MsgType::CONFIRM, true, false,
                  "Delete e-book", 
                  "The e-book \"%s\" will be deleted. Are you sure?", 
                  epub.get_title());
  book_param_controller.set_delete_current_book();
}

static void 
toc_ctrl()
{
  app_controller.set_controller(AppController::Ctrl::TOC);
}

extern bool start_web_server();
extern bool  stop_web_server();

static void
wifi_mode()
{
  #if EPUB_INKPLATE_BUILD
    msg_viewer.show(MsgViewer::MsgType::CONFIRM, true, true,
                    "WiFi Access",
                    "The e-book will be closed and WiFi file access will start. "
                    "The device will restart when done. Continue?");
    book_param_controller.set_waiting_wifi_confirm();
  #endif
}

static void
power_off()
{
  books_dir_controller.save_last_book(book_controller.get_current_page_id(), true); 
  
  CommonActions::power_it_off();
}

// IMPORTANT!!
// The first (menu[0]) and the last menu entry (the one before END_MENU) MUST ALWAYS BE VISIBLE!!!

#if defined(BOARD_TYPE_PAPER_S3)
  static void goto_next();
  static void goto_prev();
  static void usb_emulation_mode();

  static MenuViewer::MenuEntry menu[] = {
    { MenuViewer::Icon::RETURN,      "Return to the e-books reader",         CommonActions::return_to_last, true , true },
    { MenuViewer::Icon::TOC,         "Table of Content",                     toc_ctrl                     , false, true },
    { MenuViewer::Icon::BOOK_LIST,   "E-Books list",                         books_list                   , true , true },
    { MenuViewer::Icon::FONT_PARAMS, "Current e-book parameters",            book_parameters              , true , true },
    { MenuViewer::Icon::REVERT,      "Revert e-book parameters to "
                                     "default values",                       revert_to_defaults           , true , true },
    { MenuViewer::Icon::POWEROFF,    "Power OFF (Deep Sleep)",               power_off                    , true , true },
    { MenuViewer::Icon::NEXT_MENU,   "More options",                         goto_next                    , true , true },
    { MenuViewer::Icon::END_MENU,    nullptr,                                nullptr                      , false, true }
  };

  static MenuViewer::MenuEntry sub_menu[] = {
    { MenuViewer::Icon::PREV_MENU,   "Back to previous options",             goto_prev                    , true , true },
    { MenuViewer::Icon::RETURN,      "Return to the e-books reader",         CommonActions::return_to_last, true , true },
    { MenuViewer::Icon::DELETE,      "Delete the current e-book",            delete_book                  , true , true },
    { MenuViewer::Icon::WIFI,        "WiFi Access to the e-books folder",    wifi_mode                    , true , true },
    { MenuViewer::Icon::USB,         "USB SD-Card emulation",                usb_emulation_mode           , true , true },
    { MenuViewer::Icon::POWEROFF,    "Power OFF (Deep Sleep)",               power_off                    , true , true },
    { MenuViewer::Icon::END_MENU,    nullptr,                                nullptr                      , false, true }
  };

  static void
  goto_next()
  {
    book_param_controller.set_on_sub_menu(true);
    menu_viewer.show(sub_menu);
  }

  static void
  goto_prev()
  {
    book_param_controller.set_on_sub_menu(false);
    menu_viewer.show(menu);
  }

  static void
  usb_emulation_mode()
  {
    static const char* TAG = "BKPAR_CTRL";
    sdmmc_card_t* card = inkplate_platform.get_sd_card();
    if (!card) {
      LOG_E("No SD Card detected. Cannot enter USB mode.");
      msg_viewer.show(
          MsgViewer::MsgType::ALERT,
          false, true,
          "USB Drive Mode",
          "No SD card detected. Please insert an SD card and try again.");
      return;
    }
    msg_viewer.show(
        MsgViewer::MsgType::CONFIRM,
        true, true,
        "USB Drive Mode",
        "The SD card will be shared with your computer.\n\n"
        "IMPORTANT: Always safely eject / unmount the drive from your computer "
        "before unplugging the USB cable. Skipping this step can corrupt your SD card.");
    book_param_controller.set_waiting_usb_confirm();
  }

#else
  static MenuViewer::MenuEntry menu[10] = {
    { MenuViewer::Icon::RETURN,      "Return to the e-books reader",         CommonActions::return_to_last, true , true },
    { MenuViewer::Icon::TOC,         "Table of Content",                     toc_ctrl                     , false, true },
    { MenuViewer::Icon::BOOK_LIST,   "E-Books list",                         books_list                   , true , true },
    { MenuViewer::Icon::FONT_PARAMS, "Current e-book parameters",            book_parameters              , true , true },
    { MenuViewer::Icon::REVERT,      "Revert e-book parameters to "
                                     "default values",                       revert_to_defaults           , true , true },
    { MenuViewer::Icon::DELETE,      "Delete the current e-book",            delete_book                  , true , true },
    { MenuViewer::Icon::WIFI,        "WiFi Access to the e-books folder",    wifi_mode                    , true , true },
    //{ MenuViewer::Icon::INFO,        "About the EPub-InkPlate application",  CommonActions::about         , true , true },
    { MenuViewer::Icon::POWEROFF,    "Power OFF (Deep Sleep)",               power_off                    , true , true },
    { MenuViewer::Icon::END_MENU,    nullptr,                                nullptr                      , false, true }
  };
#endif

void
BookParamController::set_font_count(uint8_t count)
{
  book_params_form_entries[2].u.ch.choice_count = count;
}

void 
BookParamController::enter()
{
  menu[1].visible = toc.is_ready() && !toc.is_empty();
  #if defined(BOARD_TYPE_PAPER_S3)
    on_sub_menu = false;
  #endif
  menu_viewer.show(menu);
  book_params_form_is_shown = false;
}

void 
BookParamController::leave(bool going_to_deep_sleep)
{

}

void 
BookParamController::input_event(const EventMgr::Event & event)
{
  if (book_params_form_is_shown) {
    if (form_viewer.event(event)) {
      book_params_form_is_shown = false;
      // if (ok) {
        BookParams * book_params = epub.get_book_params();

        if (show_images       !=       old_show_images) book_params->put(BookParams::Ident::SHOW_IMAGES,        show_images      );
        if (font_size         !=         old_font_size) book_params->put(BookParams::Ident::FONT_SIZE,          font_size        );
        if (font              !=              old_font) book_params->put(BookParams::Ident::FONT,               font             );
        if (use_fonts_in_book != old_use_fonts_in_book) book_params->put(BookParams::Ident::USE_FONTS_IN_BOOK,  use_fonts_in_book);
        
        if (book_params->is_modified()) epub.update_book_format_params();

        book_params->save();

        if (old_use_fonts_in_book != use_fonts_in_book) {
          if (use_fonts_in_book) {
            epub.load_fonts();
          }
          else {
            fonts.clear();
            fonts.clear_glyph_caches();
          }
        }
 
        if (old_font != font) {
          fonts.adjust_default_font(font);
        }
     // }
      menu_viewer.clear_highlight();
    }
  }
  else if (delete_current_book) {
    bool ok;
    if (msg_viewer.confirm(event, ok)) {
      if (ok) {
        std::string filepath = epub.get_current_filename();        struct stat file_stat;

        if (stat(filepath.c_str(), &file_stat) != -1) {
          LOG_I("Deleting %s...", filepath.c_str());

          epub.close_file();
          unlink(filepath.c_str());

          int16_t pos = filepath.find_last_of('.');

          filepath.replace(pos, 5, ".pars");

          if (stat(filepath.c_str(), &file_stat) != -1) {
            LOG_I("Deleting file : %s", filepath.c_str());
            unlink(filepath.c_str());
          }

          filepath.replace(pos, 5, ".locs");

          if (stat(filepath.c_str(), &file_stat) != -1) {
            LOG_I("Deleting file : %s", filepath.c_str());
            unlink(filepath.c_str());
          }

          filepath.replace(pos, 5, ".toc");

          if (stat(filepath.c_str(), &file_stat) != -1) {
            LOG_I("Deleting file : %s", filepath.c_str());
            unlink(filepath.c_str());
          }

          int16_t dummy;
          books_dir.refresh(nullptr, dummy, false);

          app_controller.set_controller(AppController::Ctrl::DIR);
        }
      }
      else {
        msg_viewer.show(MsgViewer::MsgType::INFO, false, false, 
                        "Canceled", "The e-book was not deleted.");
      }
      delete_current_book = false;
    }
  }
  else if (waiting_revert_confirm) {
    bool ok;
    if (msg_viewer.confirm(event, ok)) {
      waiting_revert_confirm = false;
      if (ok) {
        page_locs.stop_document();

        EPub::BookFormatParams * book_format_params = epub.get_book_format_params();
        BookParams * book_params = epub.get_book_params();

        old_use_fonts_in_book = book_format_params->use_fonts_in_book;
        old_font              = book_format_params->font;

        constexpr int8_t default_value = -1;
        book_params->put(BookParams::Ident::SHOW_IMAGES,       default_value);
        book_params->put(BookParams::Ident::FONT_SIZE,         default_value);
        book_params->put(BookParams::Ident::FONT,              default_value);
        book_params->put(BookParams::Ident::USE_FONTS_IN_BOOK, default_value);

        epub.update_book_format_params();
        book_params->save();

        msg_viewer.show(MsgViewer::MsgType::INFO, false, false,
                        "E-book parameters reverted",
                        "E-book parameters reverted to default values.");

        if (old_use_fonts_in_book != book_format_params->use_fonts_in_book) {
          if (book_format_params->use_fonts_in_book) epub.load_fonts();
          else { fonts.clear(); fonts.clear_glyph_caches(); }
        }
        if (old_font != book_format_params->font) {
          fonts.adjust_default_font(book_format_params->font);
        }
      } else {
        #if defined(BOARD_TYPE_PAPER_S3)
          menu_viewer.show(on_sub_menu ? sub_menu : menu, 0, true);
        #else
          menu_viewer.show(menu, 0, true);
        #endif
      }
    }
  }
  #if EPUB_INKPLATE_BUILD
  else if (waiting_wifi_confirm) {
    bool ok;
    if (msg_viewer.confirm(event, ok)) {
      waiting_wifi_confirm = false;
      if (ok) {
        epub.close_file();
        fonts.clear(true);
        fonts.clear_glyph_caches();
        event_mgr.set_stay_on(true);
        if (start_web_server()) {
          book_param_controller.set_wait_for_key_after_wifi();
        }
      } else {
        #if defined(BOARD_TYPE_PAPER_S3)
          menu_viewer.show(on_sub_menu ? sub_menu : menu, 0, true);
        #else
          menu_viewer.show(menu, 0, true);
        #endif
      }
    }
  }
  #endif
  #if EPUB_INKPLATE_BUILD
    else if (wait_for_key_after_wifi) {
      msg_viewer.show(MsgViewer::MsgType::INFO, 
                      false, true, 
                      "Restarting", 
                      "The device is now restarting. Please wait.");
      wait_for_key_after_wifi = false;
      stop_web_server();
      esp_restart();
    }
  #endif
  #if defined(BOARD_TYPE_PAPER_S3)
  else if (waiting_usb_confirm) {
    bool ok;
    if (msg_viewer.confirm(event, ok)) {
      waiting_usb_confirm = false;
      if (ok) {
        static const char* TAG = "BKPAR_CTRL";
        LOG_I("USB confirmed — stopping services...");
        epub.close_file();
        fonts.clear(true);
        fonts.clear_glyph_caches();
        event_mgr.set_stay_on(true);
        sdmmc_card_t* card = inkplate_platform.get_sd_card();
        if (card) {
          msg_viewer.show(
              MsgViewer::MsgType::ALERT,
              false, true,
              "USB Drive Mode Active",
              "Your SD card is now shared with your computer.\n\n"
              "\xe2\x9a\xa0  Before unplugging the USB cable, you MUST first safely "
              "eject / unmount the drive from your computer.\n\n"
              "Unplugging without ejecting can corrupt your SD card and your books.");
          USBEmulation::run_msc_session(card);
        } else {
          LOG_E("Card disappeared before USB session could start.");
          menu_viewer.show(on_sub_menu ? sub_menu : menu, 0, true);
        }
      } else {
        menu_viewer.show(on_sub_menu ? sub_menu : menu, 0, true);
      }
    }
  }
  #endif
  else {
    if (menu_viewer.event(event)) {
      app_controller.set_controller(AppController::Ctrl::LAST);
    }
  }
}
