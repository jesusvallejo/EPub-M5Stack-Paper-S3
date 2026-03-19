// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#define __APP_CONTROLLER__ 1
#include "controllers/app_controller.hpp"

#include "controllers/books_dir_controller.hpp"
#include "controllers/book_controller.hpp"
#include "controllers/book_param_controller.hpp"
#include "controllers/option_controller.hpp"
#include "controllers/toc_controller.hpp"
#include "controllers/opds_controller.hpp"
#include "controllers/event_mgr.hpp"
#include "models/books_dir.hpp"

#if INKPLATE_6PLUS
  #include "controllers/back_lit.hpp"
#endif

#include "stb_image_resize.h"  // declarations only; implementation is in image.cpp
#include "alloc.hpp"
#include "screen.hpp"
#include "viewers/battery_viewer.hpp"

#include <stdio.h>

AppController::AppController() : 
  current_ctrl(Ctrl::DIR),
     next_ctrl(Ctrl::NONE)
{
  for (int i = 0; i < LAST_COUNT; i++) {
    last_ctrl[i] = Ctrl::DIR;
  }
}

void
AppController::start()
{
  current_ctrl = Ctrl::NONE;
  next_ctrl    = Ctrl::DIR;

  #if EPUB_LINUX_BUILD
    launch();
    event_mgr.loop(); // Will start gtk. Will not return.
  #else
    while (true) {
      while (next_ctrl != Ctrl::NONE) launch();
      event_mgr.loop();
    }
  #endif
}

void 
AppController::set_controller(Ctrl new_ctrl) 
{
  LOG_D("===> set_controller()...");
  
  next_ctrl = new_ctrl;
}

void AppController::launch()
{
  #if EPUB_LINUX_BUILD
    if (next_ctrl == Ctrl::NONE) return;
  #endif
  
  Ctrl the_ctrl = next_ctrl;
  next_ctrl = Ctrl::NONE;

  if (((the_ctrl == Ctrl::LAST) && (last_ctrl[0] != current_ctrl)) || (the_ctrl != current_ctrl)) {

    switch (current_ctrl) {
      case Ctrl::DIR:     books_dir_controller.leave(); break;
      case Ctrl::BOOK:         book_controller.leave(); break;
      case Ctrl::PARAM:  book_param_controller.leave(); break;
      case Ctrl::OPTION:     option_controller.leave(); break;
      case Ctrl::TOC:           toc_controller.leave(); break;
      case Ctrl::OPDS:         opds_controller.leave(); break;
      case Ctrl::NONE:
      case Ctrl::LAST:                                  break;
    }

    Ctrl tmp = current_ctrl;
    current_ctrl = (the_ctrl == Ctrl::LAST) ? last_ctrl[0] : the_ctrl;

    if (the_ctrl == Ctrl::LAST) {
      for (int i = 1; i < LAST_COUNT; i++) last_ctrl[i - 1] = last_ctrl[i];
      last_ctrl[LAST_COUNT - 1] = Ctrl::DIR;
    }
    else {
      for (int i = 1; i < LAST_COUNT; i++) last_ctrl[i] = last_ctrl[i - 1];
      last_ctrl[0] = tmp;
    }

    switch (current_ctrl) {
      case Ctrl::DIR:     books_dir_controller.enter(); break;
      case Ctrl::BOOK:         book_controller.enter(); break;
      case Ctrl::PARAM:  book_param_controller.enter(); break;
      case Ctrl::OPTION:     option_controller.enter(); break;
      case Ctrl::TOC:           toc_controller.enter(); break;
      case Ctrl::OPDS:         opds_controller.enter(); break;
      case Ctrl::NONE:
      case Ctrl::LAST:                                  break;
    }
  }
}

void 
AppController::input_event(const EventMgr::Event & event)
{
  if (next_ctrl != Ctrl::NONE) launch();

  #if defined(BOARD_TYPE_PAPER_S3)
    if (event.kind == EventMgr::EventKind::BATTERY_UPDATE) {
      BatteryViewer::update();
      return;
    }
  #endif

  #if INKPLATE_6PLUS
    if (event.kind == EventMgr::EventKind::PINCH_ENLARGE) {
      back_lit.adjust(event.dist);
      return;
    }
    else if (event.kind == EventMgr::EventKind::PINCH_REDUCE) {
      back_lit.adjust(-event.dist);
      return;
    }
  #endif

  switch (current_ctrl) {
    case Ctrl::DIR:     books_dir_controller.input_event(event); break;
    case Ctrl::BOOK:         book_controller.input_event(event); break;
    case Ctrl::PARAM:  book_param_controller.input_event(event); break;
    case Ctrl::OPTION:     option_controller.input_event(event); break;
    case Ctrl::TOC:           toc_controller.input_event(event); break;
    case Ctrl::OPDS:         opds_controller.input_event(event); break;
    case Ctrl::NONE:
    case Ctrl::LAST:                                             break;
  }
}

void
AppController::going_to_deep_sleep()
{
  if (next_ctrl != Ctrl::NONE) launch();

  #if INKPLATE_6PLUS
    back_lit.turn_off();
    touch_screen.shutdown();
  #endif

  switch (current_ctrl) {
    case Ctrl::DIR:     books_dir_controller.leave(true); break;
    case Ctrl::BOOK:         book_controller.leave(true); break;
    case Ctrl::PARAM:  book_param_controller.leave(true); break;
    case Ctrl::OPTION:     option_controller.leave(true); break;
    case Ctrl::TOC:           toc_controller.leave(true); break;
    case Ctrl::OPDS:         opds_controller.leave(true); break;
    case Ctrl::NONE:
    case Ctrl::LAST:                                      break;
  }
}

bool
AppController::show_sleep_book_cover()
{
#if EPUB_INKPLATE_BUILD
  std::string cover_path = books_dir.get_sleep_cover_path();
  if (cover_path.empty()) return false;

  FILE * f = fopen(cover_path.c_str(), "rb");
  if (f == nullptr) {
    LOG_E("show_sleep_book_cover: cannot open %s", cover_path.c_str());
    return false;
  }

  // Read the simple file header: magic[4] + width(uint16) + height(uint16)
  uint8_t  magic[4];
  uint16_t w = 0, h = 0;
  bool ok = (fread(magic, 1, 4, f) == 4)
         && (magic[0] == 'C') && (magic[1] == 'O')
         && (magic[2] == 'V') && (magic[3] == 'R')
         && (fread(&w, 2, 1, f) == 1)
         && (fread(&h, 2, 1, f) == 1)
         && (w > 0) && (h > 0);

  if (!ok) { fclose(f); return false; }

  uint32_t  size   = (uint32_t) w * h;
  uint8_t * bitmap = (uint8_t *) allocate(size);
  if (bitmap == nullptr) { fclose(f); return false; }

  if (fread(bitmap, 1, size, f) != size) {
    fclose(f);
    free(bitmap);
    return false;
  }
  fclose(f);

  // Centre the cover on screen and display it.
  uint16_t sw = Screen::get_width();
  uint16_t sh = Screen::get_height();

  // Scale to fit the full screen while preserving aspect ratio.
  uint32_t new_w = (uint32_t)w * sh / h;
  uint32_t new_h = sh;
  if (new_w > sw) {
    new_h = (uint32_t)h * sw / w;
    new_w = sw;
  }

  uint8_t * scaled = (uint8_t *) allocate(new_w * new_h);
  if (scaled == nullptr) { free(bitmap); return false; }

  stbir_resize_uint8(bitmap,  (int)w,     (int)h,     0,
                     scaled,  (int)new_w, (int)new_h, 0, 1);
  free(bitmap);

  uint16_t px = (uint16_t)((sw - new_w) / 2);
  uint16_t py = (uint16_t)((sh - new_h) / 2);

  screen.clear();
  screen.draw_bitmap(scaled, Dim((uint16_t)new_w, (uint16_t)new_h), Pos(px, py));
  free(scaled);

  screen.update(false); // full GC16 update — best quality for sleep image
  return true;
#else
  return false;
#endif
}
