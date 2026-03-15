// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#define __BOOKS_DIR_CONTROLLER__ 1
#include "controllers/books_dir_controller.hpp"

#include "controllers/app_controller.hpp"
#include "controllers/book_controller.hpp"
#include "models/books_dir.hpp"
#include "models/config.hpp"
#include "viewers/book_viewer.hpp"
#include "viewers/linear_books_dir_viewer.hpp"
#include "viewers/matrix_books_dir_viewer.hpp"
#include "viewers/menu_viewer.hpp"
#include "viewers/msg_viewer.hpp"
#include "screen.hpp"

#if EPUB_INKPLATE_BUILD
  #include "models/nvs_mgr.hpp"
  #include "esp.hpp"
#endif

// ---------------------------------------------------------------------------
// Context menu for long-press on a book entry (touch devices only)
// ---------------------------------------------------------------------------

#if INKPLATE_6PLUS || TOUCH_TRIAL

static void context_cancel()              { books_dir_controller.clear_context_menu();         }
static void context_delete_confirm()      { books_dir_controller.show_delete_confirm();         }
static void context_mark_complete_confirm() { books_dir_controller.show_mark_complete_confirm(); }
static void context_reload_meta_confirm() { books_dir_controller.show_reload_meta_confirm();    }

static MenuViewer::MenuEntry context_menu[] = {
  { MenuViewer::Icon::RETURN,      "Cancel",  context_cancel,               false, true  },  // hidden, tap-outside closes menu
  { MenuViewer::Icon::DELETE,      "Delete",  context_delete_confirm,       true,  true  },
  { MenuViewer::Icon::CLR_HISTORY, "Mark Read",context_mark_complete_confirm,true, true  },
  { MenuViewer::Icon::REFRESH,     "Reload",  context_reload_meta_confirm,  true,  true  },
  { MenuViewer::Icon::END_MENU,    nullptr,   nullptr,                       false, false }
};

#endif // INKPLATE_6PLUS || TOUCH_TRIAL

// ---------------------------------------------------------------------------
// refresh_view() — re-display the book list without going through enter()
// ---------------------------------------------------------------------------
void
BooksDirController::refresh_view()
{
  books_dir_viewer->setup();
  screen.force_full_update();
  if (current_book_index >= books_dir.get_book_count()) {
    current_book_index = books_dir.get_book_count() - 1;
  }
  if (current_book_index < 0) current_book_index = 0;
  current_book_index = books_dir_viewer->show_page_and_highlight(current_book_index);
}

// ---------------------------------------------------------------------------
// Public helpers called by the static context-menu action functions
// ---------------------------------------------------------------------------
void
BooksDirController::clear_context_menu()
{
  book_context_menu_shown = false;
  context_action = ContextAction::NONE;
  refresh_view();
}

void
BooksDirController::show_delete_confirm()
{
  book_context_menu_shown = false;
  context_action          = ContextAction::DELETE;

  const BooksDir::EBookRecord * book = books_dir.get_book_data(context_book_index);
  const char * title = (book != nullptr) ? book->title : "this book";

  msg_viewer.show(
    MsgViewer::MsgType::CONFIRM,
    true, true,
    "Delete Book",
    "Delete \"%s\"? The epub file will be permanently removed.",
    title);
}

void
BooksDirController::show_mark_complete_confirm()
{
  book_context_menu_shown = false;
  context_action          = ContextAction::MARK_COMPLETE;

  const BooksDir::EBookRecord * book = books_dir.get_book_data(context_book_index);
  const char * title = (book != nullptr) ? book->title : "this book";

  msg_viewer.show(
    MsgViewer::MsgType::CONFIRM,
    true, true,
    "Mark as Read",
    "Mark \"%s\" as completed?",
    title);
}

void
BooksDirController::show_reload_meta_confirm()
{
  book_context_menu_shown = false;
  context_action          = ContextAction::RELOAD_META;

  const BooksDir::EBookRecord * book = books_dir.get_book_data(context_book_index);
  const char * title = (book != nullptr) ? book->title : "this book";

  msg_viewer.show(
    MsgViewer::MsgType::CONFIRM,
    true, true,
    "Reload Metadata",
    "Reload cover and metadata for \"%s\"?",
    title);
}

// ---------------------------------------------------------------------------

void
BooksDirController::setup()
{
  // Retrieve the information related to the last book read by the user. 
  // This is stored in the NVS on the ESP32, or in a flat file on Linux.
  // If the user was reading a book at the last entry to deep sleep, it will be
  // shown on screen instead of the books directory list.

  current_book_index         = -1;
  last_read_book_index       = -1;
  book_page_id.itemref_index = -1;
  book_page_id.offset        = -1;
  book_was_shown             = false;

  
  #if EPUB_INKPLATE_BUILD

    int16_t dummy;
    if (!books_dir.read_books_directory(nullptr, dummy)) {
      LOG_E("There was issues reading books directory.");
    }
    else {

      NVSMgr::NVSData nvs_data;
      uint32_t        id;

      if (nvs_mgr.get_last(id, nvs_data)) {
        book_page_id.itemref_index = nvs_data.itemref_index;
        book_page_id.offset        = nvs_data.offset;
        book_was_shown             = nvs_data.was_shown;

        int16_t idx;
        if ((idx = books_dir.get_sorted_idx_from_id(id)) != -1) {

          last_read_book_index = current_book_index = idx;
          
          //LOG_D("Last book filename: %s",  book_fname);
          LOG_D("Last book ref index: %d", book_page_id.itemref_index);
          LOG_D("Last book offset: %d",    book_page_id.offset);
          LOG_D("Show it now: %s",         book_was_shown ? "yes" : "no");
        }
      }
    }

  #else

    char * book_fname          = new char[256];
    char * filename            = nullptr;

    book_fname[0]              =  0;

    FILE * f = fopen(MAIN_FOLDER "/last_book.txt", "r");
    filename = nullptr;
    if (f != nullptr) {

      if (fgets(book_fname, 256, f)) {
        int16_t size = strlen(book_fname) - 1;
        if (book_fname[size] == '\n') book_fname[size] = 0;

        char buffer[20];
        if (fgets(buffer, 20, f)) {
          book_page_id.itemref_index = atoi(buffer);

          if (fgets(buffer, 20, f)) {
            book_page_id.offset = atoi(buffer);

            if (fgets(buffer, 20, f)) {
              int8_t was_shown = atoi(buffer);
              filename       = book_fname;
              book_was_shown = (bool) was_shown;
            }
          }
        }
      }

      fclose(f);
    } 

    int16_t db_idx = -1;
    // Read the directory, returning the book index (db_idx).
    if (!books_dir.read_books_directory(filename, db_idx)) {
      LOG_E("There was issues reading books directory.");
    }
    
    // The retrieved db_idx is the index in the database of the last book
    // read by the user. We need the
    // index in the sorted list of books as this is what the 
    // BookController expect.

    if (db_idx != -1) {
      last_read_book_index = books_dir.get_sorted_idx(db_idx);
      current_book_index   = last_read_book_index;
      book_filename        = book_fname;
    }

    LOG_D("Book to show: idx:%d page:(%d, %d) was_shown:%s", 
          last_read_book_index, book_page_id.itemref_index, book_page_id.offset, book_was_shown ? "yes" : "no");

    delete [] book_fname;
  #endif
}

void
BooksDirController::save_last_book(const PageLocs::PageId & page_id, bool going_to_deep_sleep)
{
  // As we leave, we keep the information required to return to the book
  // in the NVS space. If this is called just before going to deep sleep, we
  // set the "WAS_SHOWN" boolean to true, such that when the device will
  // be booting, it will display the last book at the last page shown.

  book_page_id = page_id;

  #if EPUB_INKPLATE_BUILD

    uint32_t book_id;

    if ((current_book_index != -1) && books_dir.get_book_id(current_book_index, book_id)) {

      // Preserve the existing read_status flag when saving position.
      NVSMgr::NVSData nvs_data = {};
      nvs_mgr.get_location(book_id, nvs_data);
      nvs_data.offset        = page_id.offset;
      nvs_data.itemref_index = page_id.itemref_index;
      nvs_data.was_shown     = (uint8_t) (going_to_deep_sleep ? 1 : 0);
      // nvs_data.read_status preserved from get_location() above.

      if (!nvs_mgr.save_location(book_id, nvs_data)) {
        LOG_E("Unable to save current ebook location");
      }
      last_read_book_index = 
      current_book_index   = books_dir.get_sorted_idx_from_id(book_id);
    }

  #else
  
    FILE * f = fopen(MAIN_FOLDER "/last_book.txt", "w");
    if (f != nullptr) {
      fprintf(f, "%s\n%d\n%d\n%d\n",
        book_filename.c_str(),
        page_id.itemref_index,
        page_id.offset,
        going_to_deep_sleep ? 1 : 0
      );
      fclose(f);
    } 
  #endif  
}

void
BooksDirController::show_last_book()
{

  if (last_read_book_index == -1) return;

  LOG_D("===> show_last_book()...");
  static std::string            book_fname;
  static std::string            book_title;
  const BooksDir::EBookRecord * book;

  book_was_shown = false;  
  book           = books_dir.get_book_data(last_read_book_index);

  if (book != nullptr) {
    book_fname  = BOOKS_FOLDER "/";
    book_fname += book->filename;
    book_title  = book->title;
    if (book_controller.open_book_file(book_title, book_fname, book_page_id)) {
      app_controller.set_controller(AppController::Ctrl::BOOK);
    }
  }
}

void 
BooksDirController::enter()
{

  LOG_D("===> enter()...");
  config.get(Config::Ident::DIR_VIEW, &viewer_id);
  const char *view = (viewer_id == LINEAR_VIEWER) ? "linear" :
                     (viewer_id == MATRIX_VIEWER) ? "matrix" : "unknown";
  log('I', TAG,
      "BooksDirController enter: viewer=%s (id=%d) screen=%dx%d",
      view, viewer_id, Screen::get_width(), Screen::get_height());
  books_dir_viewer = (viewer_id == LINEAR_VIEWER) ? (BooksDirViewer *) &linear_books_dir_viewer : 
                                        (BooksDirViewer *) &matrix_books_dir_viewer;

  books_dir_viewer->setup();
  screen.force_full_update();
  
  if (book_was_shown && (last_read_book_index != -1)) {
    show_last_book();
  }
  else {
    if (current_book_index == -1) current_book_index = 0;
    current_book_index = books_dir_viewer->show_page_and_highlight(current_book_index);
  }
}

void 
BooksDirController::leave(bool going_to_deep_sleep)
{

}

#if INKPLATE_6PLUS || TOUCH_TRIAL
  void 
  BooksDirController::input_event(const EventMgr::Event & event)
  {
    static std::string book_fname;
    static std::string book_title;

    const BooksDir::EBookRecord * book;

    // ---- Priority 1: Waiting for a confirmation dialog response ----
    if (context_action != ContextAction::NONE) {
      bool ok = false;
      if (msg_viewer.confirm(event, ok)) {
        if (ok) {
          switch (context_action) {
            case ContextAction::DELETE:
              books_dir.delete_book(context_book_index);
              // Book is gone; reset selection to avoid invalid index.
              current_book_index   = 0;
              last_read_book_index = -1;
              break;
            case ContextAction::MARK_COMPLETE:
              books_dir.set_read_status(context_book_index, 1);
              current_book_index = context_book_index;
              break;
            case ContextAction::RELOAD_META:
              books_dir.reload_book_metadata(context_book_index);
              current_book_index = 0; // index may shift after rescan
              break;
            default:
              break;
          }
        }
        context_action          = ContextAction::NONE;
        book_context_menu_shown = false;
        if (current_book_index >= books_dir.get_book_count())
          current_book_index = (int16_t)(books_dir.get_book_count() - 1);
        if (current_book_index < 0) current_book_index = 0;
        refresh_view();
      }
      return;
    }

    // ---- Priority 2: Context menu is open ----
    if (book_context_menu_shown) {
      menu_viewer.event(event);
      return;
    }

    // ---- Normal book list input ----
    switch (event.kind) {
      case EventMgr::EventKind::SWIPE_RIGHT:
        current_book_index = books_dir_viewer->prev_page();   
        break;

      case EventMgr::EventKind::SWIPE_LEFT:
        current_book_index = books_dir_viewer->next_page();   
        break;

      case EventMgr::EventKind::TAP:
        if ((viewer_id == MATRIX_VIEWER) || (event.x < (Screen::get_width() / 3))) {
          current_book_index = books_dir_viewer->get_index_at(event.x, event.y);
          if ((current_book_index >= 0) && (current_book_index < books_dir.get_book_count())) {
            book = books_dir.get_book_data(current_book_index);
            if (book != nullptr) {
              last_read_book_index = current_book_index;
              book_fname    = BOOKS_FOLDER "/";
              book_fname   += book->filename;
              book_title    = book->title;
              book_filename = book->filename;
              
              PageLocs::PageId page_id = { 0, 0 };

              #if EPUB_INKPLATE_BUILD
                NVSMgr::NVSData nvs_data;
                if (nvs_mgr.get_location(book->id, nvs_data)) {
                  page_id = { nvs_data.itemref_index, nvs_data.offset };
                }
              #endif
              
              if (book_controller.open_book_file(book_title, book_fname, page_id)) {
                app_controller.set_controller(AppController::Ctrl::BOOK);
              }
            }
          }
          else {
            current_book_index = -1;
            app_controller.set_controller(AppController::Ctrl::OPTION);
          }
        }
        else {
          current_book_index = -1;
          app_controller.set_controller(AppController::Ctrl::OPTION);
        }
        break;

      case EventMgr::EventKind::HOLD:
        context_book_index = books_dir_viewer->get_index_at(event.x, event.y);
        if ((context_book_index >= 0) && (context_book_index < books_dir.get_book_count())) {
          // Open the per-book context menu.
          book_context_menu_shown = true;
          menu_viewer.show(context_menu);
          LOG_I("Context menu opened for book index: %d", context_book_index);
        }
        break;

      case EventMgr::EventKind::RELEASE:
        // Only clear the list highlight when the context menu is NOT open.
        if (!book_context_menu_shown) {
          #if INKPLATE_6PLUS
            ESP::delay(1000);
          #endif
          books_dir_viewer->clear_highlight();
        }
        break;

      default:
        break;
    }
  }
#else
  void 
  BooksDirController::input_event(const EventMgr::Event & event)
  {
    static std::string book_fname;
    static std::string book_title;

    const BooksDir::EBookRecord * book;

    switch (event.kind) {
      #if EXTENDED_CASE
        case EventMgr::EventKind::PREV:
      #else
        case EventMgr::EventKind::DBL_PREV:
      #endif
        current_book_index = books_dir_viewer->prev_column();   
        break;

      #if EXTENDED_CASE
        case EventMgr::EventKind::NEXT:
      #else
        case EventMgr::EventKind::DBL_NEXT:
      #endif
        current_book_index = books_dir_viewer->next_column();
        break;

      #if EXTENDED_CASE
        case EventMgr::EventKind::DBL_PREV:
      #else
        case EventMgr::EventKind::PREV:
      #endif
        current_book_index = books_dir_viewer->prev_item();
        break;

      #if EXTENDED_CASE
        case EventMgr::EventKind::DBL_NEXT:
      #else
        case EventMgr::EventKind::NEXT:
      #endif
        current_book_index = books_dir_viewer->next_item();
        break;

      case EventMgr::EventKind::SELECT:
        if (current_book_index < books_dir.get_book_count()) {
          book = books_dir.get_book_data(current_book_index);
          if (book != nullptr) {
            last_read_book_index = current_book_index;
            book_fname    = BOOKS_FOLDER "/";
            book_fname   += book->filename;
            book_title    = book->title;
            book_filename = book->filename;
            
            PageLocs::PageId page_id = { 0, 0 };

            #if EPUB_INKPLATE_BUILD
              NVSMgr::NVSData nvs_data;
              if (nvs_mgr.get_location(book->id, nvs_data)) {
                page_id.itemref_index = nvs_data.itemref_index;
                page_id.offset        = nvs_data.offset;
              }
            #endif

            if (book_controller.open_book_file(book_title, book_fname, page_id)) {
              app_controller.set_controller(AppController::Ctrl::BOOK);
            }
          }
        }
        break;

      case EventMgr::EventKind::DBL_SELECT:
        app_controller.set_controller(AppController::Ctrl::OPTION);
        break;
        
      case EventMgr::EventKind::NONE:
        break;
    }
  }
#endif