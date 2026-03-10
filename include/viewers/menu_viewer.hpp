// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#pragma once
#include "global.hpp"

#include "controllers/event_mgr.hpp"

class MenuViewer
{
  public:
    static constexpr uint8_t MAX_MENU_ENTRY = 15;

    enum class Icon { RETURN,      CLR_HISTORY, REFRESH,   BOOK,   BOOK_LIST, MAIN_PARAMS, 
                      FONT_PARAMS, POWEROFF,    WIFI,      INFO,   TOC,       DEBUG, 
                      DELETE,      CLOCK,       NTP_CLOCK, CALIB,  PREV_MENU, NEXT_MENU, REVERT, 
                      END_MENU,    USB,         OPDS };
    char icon_char[22] = { 
                      '@',         'T',         'R',       'E',    'F',       'C', 
                      'A',         'Z',         'S',       'I',    'L',       'H', 
                      'K',         'N',         'Y',       'M',    'O',       'P',   
                      'U',         '-',         'V',       'D' };
    struct MenuEntry {
      Icon icon;
      const char * caption;
      void (*func)();
      bool visible;
      bool highlight;
    };
    void  show(MenuEntry * the_menu, uint8_t entry_index = 0, bool clear_screen = false);
    bool event(const EventMgr::Event & event);
    void clear_highlight();
    
  private:
    static constexpr char const * TAG = "MenuViewer";

    #if defined(BOARD_TYPE_PAPER_S3)
      static const int16_t ICON_SIZE             = 24;
      static const int16_t CAPTION_SIZE          = 9;
      // 81 px pitch allows up to 7 icons in 540 px (25 + 6×81 + 24 = 535)
      static const int16_t SPACE_BETWEEN_ICONS   = 81;
      static const int16_t ICONS_LEFT_OFFSET     = 15;
      static const int16_t NEXT_MENU_RIGHT_OFFSET = 48;
    #elif INKPLATE_6PLUS
      static const int16_t ICON_SIZE             = 18;
      static const int16_t CAPTION_SIZE          = 10;
      static const int16_t SPACE_BETWEEN_ICONS   = 70;
      static const int16_t ICONS_LEFT_OFFSET     = 20;
      static const int16_t NEXT_MENU_RIGHT_OFFSET = SPACE_BETWEEN_ICONS;
    #else
      static const int16_t ICON_SIZE             = 18;
      static const int16_t CAPTION_SIZE          = 10;
      static const int16_t SPACE_BETWEEN_ICONS   = 70;
      static const int16_t ICONS_LEFT_OFFSET     = 20;
      static const int16_t NEXT_MENU_RIGHT_OFFSET = SPACE_BETWEEN_ICONS;
    #endif

    uint8_t  current_entry_index;
    uint8_t  max_index;
    uint16_t icon_height, 
             text_height, 
             line_height,
             region_height;
    uint16_t icon_ypos,
             text_ypos;

    // Paper S3: function called when the user taps outside the menu icon row.
    // Auto-discovered from the menu array (RETURN → PREV_MENU fallback).
    void (*tap_outside_func)() = nullptr;

    #if (INKPLATE_6PLUS || TOUCH_TRIAL)
      bool    hint_shown;
      uint8_t find_index(uint16_t x, uint16_t y);
    #endif

    struct EntryLoc {
      Pos pos;
      Dim dim;
    } entry_locs[MAX_MENU_ENTRY];
    MenuEntry * menu;
};

#if __MENU_VIEWER__
  MenuViewer menu_viewer;
#else
  extern MenuViewer menu_viewer;
#endif
