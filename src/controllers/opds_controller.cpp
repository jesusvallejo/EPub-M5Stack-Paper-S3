// Copyright (c) 2024
//
// MIT License. Look at file licenses.txt for details.

#define __OPDS_CONTROLLER__ 1
#include "controllers/opds_controller.hpp"

#include "controllers/app_controller.hpp"
#include "controllers/common_actions.hpp"
#include "controllers/wifi.hpp"

#include "viewers/page.hpp"
#include "viewers/msg_viewer.hpp"
#include "viewers/screen_bottom.hpp"

#include "models/config.hpp"
#include "models/epub.hpp"
#include "models/fonts.hpp"
#include "models/opds.hpp"
#include "models/page_locs.hpp"

#include "screen.hpp"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#if EPUB_INKPLATE_BUILD
  #include "esp_system.h"
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Sanitise a string so it can be used as a FAT/SD filename component.
/// Keeps alphanumerics, spaces, hyphens, underscores and dots;
/// collapses runs of invalid chars to a single underscore.
static std::string
sanitise_filename(const std::string & s)
{
  std::string out;
  out.reserve(s.size());
  bool prev_replaced = false;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ' ') {
      out += static_cast<char>(c);
      prev_replaced = false;
    } else {
      if (!prev_replaced && !out.empty()) out += '_';
      prev_replaced = true;
    }
  }
  // Trim trailing spaces/underscores
  while (!out.empty() && (out.back() == ' ' || out.back() == '_')) out.pop_back();
  return out;
}

/// Build a .epub filename for a catalog entry.
/// Format: "Title - Author (Year).epub"
/// Falls back gracefully when author/year are missing.
/// Uses the URL last segment only when it already looks like a real filename.
std::string
filename_for_entry(const std::string & title, const std::string & url,
                   const std::string & author, const std::string & year)
{
  // Try to extract last path segment from URL
  const size_t slash = url.rfind('/');
  std::string  seg   = (slash != std::string::npos) ? url.substr(slash + 1) : url;
  // Strip query string
  const size_t q = seg.find('?');
  if (q != std::string::npos) seg = seg.substr(0, q);

  // Generic/numeric segments that are not real filenames
  static const char * const generic[] = {
    "download", "file", "get", "epub", "book", "content", nullptr
  };
  bool seg_generic = false;
  std::string seg_lower = seg;
  for (char & c : seg_lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  std::string seg_base = (seg_lower.size() >= 5 &&
    seg_lower.compare(seg_lower.size()-5,5,".epub")==0)
      ? seg_lower.substr(0, seg_lower.size()-5) : seg_lower;
  for (int i = 0; generic[i]; i++) {
    if (seg_base == generic[i]) { seg_generic = true; break; }
  }
  if (!seg_generic && !seg_base.empty()) {
    bool all_digits = true;
    for (char c : seg_base) if (c < '0' || c > '9') { all_digits = false; break; }
    if (all_digits) seg_generic = true;
  }

  if (!title.empty() && (seg_generic || seg.empty())) {
    // Build "Title - Author (Year)" parts
    std::string name = sanitise_filename(title);
    if (name.empty()) name = "book";
    if (!author.empty()) {
      name += " - ";
      name += sanitise_filename(author);
    }
    if (!year.empty()) {
      name += " (";
      name += year;
      name += ")";
    }
    // Limit length to keep FAT paths short
    if (name.size() > 80) name.resize(80);
    return name + ".epub";
  }

  if (seg.empty()) return "book.epub";
  if (seg.size() < 5 || seg.compare(seg.size()-5,5,".epub") != 0) seg += ".epub";
  return seg;
}

/// Build a zero-initialised Page::Format.
inline Page::Format make_fmt(
    int16_t font_index, int16_t font_size, CSS::Align align,
    int16_t screen_top, int16_t screen_bottom,
    int16_t screen_left = 0, int16_t screen_right = 0,
    int16_t margin_top = 0)
{
  Page::Format f = {};
  f.line_height_factor = 0.9f;
  f.font_index         = font_index;
  f.font_size          = font_size;
  f.screen_left        = screen_left;
  f.screen_right       = screen_right;
  f.screen_top         = screen_top;
  f.screen_bottom      = screen_bottom;
  f.margin_top         = margin_top;
  f.trim               = true;
  f.font_style         = Fonts::FaceStyle::NORMAL;
  f.align              = align;
  f.text_transform     = CSS::TextTransform::NONE;
  f.display            = CSS::Display::INLINE;
  return f;
}

} // namespace

// ---------------------------------------------------------------------------
// OPDSController :: public API
// ---------------------------------------------------------------------------

void
OPDSController::enter()
{
  state         = State::IDLE;
  current_page  = 0;
  selected_idx  = -1;
  dl_cancelled  = false;
  dl_last_pct   = -1;
  entries.clear();
  url_stack.clear();
  current_url.clear();

  connect_and_fetch();   // Blocks until catalog fetched or error

  if (state == State::ERROR) {
    show_error();
  } else {
    show_list();
  }
}

void
OPDSController::leave(bool going_to_deep_sleep)
{
  (void) going_to_deep_sleep;

#if EPUB_INKPLATE_BUILD
  // Make sure WiFi is released on any exit path
  wifi.stop();
  // Restart the page-location worker threads that were killed in
  // connect_and_fetch() before WiFi started.  Without this, a second
  // entry into OPDS (or any book open) would call abort_threads() on
  // non-joinable threads and trigger std::terminate() → abort().
  page_locs.setup();
#endif
}

void
OPDSController::input_event(const EventMgr::Event & event)
{
  if (event.kind != EventMgr::EventKind::TAP &&
      event.kind != EventMgr::EventKind::SWIPE_LEFT &&
      event.kind != EventMgr::EventKind::SWIPE_RIGHT) {
    return;
  }

  switch (state) {

    // ------------------------------------------------------------------
    case State::SHOWING_LIST: {
      if (event.kind == EventMgr::EventKind::SWIPE_LEFT) {
        if (current_page + 1 < pages()) { current_page++; show_list(); }
        break;
      }
      if (event.kind == EventMgr::EventKind::SWIPE_RIGHT) {
        if (current_page > 0) {
          current_page--;
          show_list();
        } else if (!url_stack.empty()) {
          // Navigate back to the parent catalog
          std::string parent = url_stack.back();
          url_stack.pop_back();
          fetch_page(parent);
          if (state == State::ERROR) { show_error(); } else { show_list(); }
        }
        break;
      }
      if (event.kind == EventMgr::EventKind::TAP) {
        // Bottom strip: tap anywhere in the last FOOTER_RESERVE pixels → return
        if (event.y >= static_cast<uint16_t>(Screen::get_height() - FOOTER_RESERVE)) {
          do_return();
          break;
        }
        // Compute which entry was tapped
        if (event.y >= static_cast<uint16_t>(FIRST_ENTRY_YPOS)) {
          int rel_idx = (event.y - FIRST_ENTRY_YPOS) / ENTRY_HEIGHT;
          int abs_idx = current_page * entries_per_page + rel_idx;
          if (rel_idx < entries_per_page &&
              abs_idx >= 0 &&
              abs_idx < static_cast<int>(entries.size())) {
            selected_idx = abs_idx;
            if (entries[abs_idx].is_nav()) {
              // Navigate into the sub-catalog (no confirmation needed)
              url_stack.push_back(current_url);
              fetch_page(entries[abs_idx].nav_url);
              if (state == State::ERROR) { show_error(); } else { show_list(); }
            } else {
              show_confirm();
              state = State::CONFIRM_DOWNLOAD;
            }
          }
        }
      }
      break;
    }

    // ------------------------------------------------------------------
    case State::CONFIRM_DOWNLOAD: {
      if (event.kind != EventMgr::EventKind::TAP) break;
      bool ok = false;
      if (msg_viewer.confirm(event, ok)) {
        if (ok) {
          start_download(selected_idx);
          // start_download sets state to DONE or ERROR upon return
          if (state == State::DONE) {
            show_done();
          } else {
            show_error();
          }
        } else {
          selected_idx = -1;
          state = State::SHOWING_LIST;
          show_list();
        }
      }
      break;
    }

    // ------------------------------------------------------------------
    case State::DONE: {
      // Any tap reboots the device to refresh the book list
#if EPUB_INKPLATE_BUILD
      esp_restart();
#else
      do_return();
#endif
      break;
    }

    // ------------------------------------------------------------------
    case State::ERROR: {
      do_return();
      break;
    }

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// OPDSController :: private helpers
// ---------------------------------------------------------------------------

int
OPDSController::pages() const
{
  if (entries_per_page < 1) return 1;
  return static_cast<int>((entries.size() + entries_per_page - 1) / entries_per_page);
}

void
OPDSController::do_return()
{
  app_controller.set_controller(AppController::Ctrl::LAST);
}

// ---------------------------------------------------------------------------
// connect_and_fetch
// ---------------------------------------------------------------------------

void
OPDSController::connect_and_fetch()
{
  // Read config
  std::string url, user, pwd;
  config.get(Config::Ident::OPDS_URL,  url);
  config.get(Config::Ident::OPDS_USER, user);
  config.get(Config::Ident::OPDS_PWD,  pwd);

  if (url.empty()) {
    snprintf(last_error, sizeof(last_error),
             "No OPDS URL configured.\nAdd opds_url = ... to config.txt");
    state = State::ERROR;
    return;
  }

  // Prevent light sleep during the entire WiFi session.
#if EPUB_INKPLATE_BUILD
  event_mgr.set_stay_on(true);
#endif

  // Show "Connecting..." BEFORE freeing resources so that:
  //  a) Fonts are still loaded → fast render, no SD card reload.
  //  b) The EPD update (which wakes epdiy tasks at configMAX_PRIORITIES-1)
  //     completes well before the WiFi auth handshake begins.
  // This is the same reason NTP needs a user-confirm step between the menu
  // tap and wifi.start(): any display/font activity in that gap would let
  // the high-priority epdiy tasks pre-empt the WiFi system task during auth.
  show_connecting();
  state = State::CONNECTING;

  // Now release memory.  page_locs.join() may itself take 100-400 ms while
  // the worker threads reach a safe abort point — that dead time counts as
  // additional settling after the last EPD update.
#if EPUB_INKPLATE_BUILD
  page_locs.abort_threads();
  epub.close_file();
  fonts.clear(true);
  fonts.clear_glyph_caches();

  // Extended settle: no EPD, no SD, no font work is happening here.
  // 500 ms gives touch/battery I2C tasks and the FreeRTOS scheduler time
  // to reach a fully idle state before the WiFi driver starts its own
  // high-priority tasks and the auth handshake begins.
  vTaskDelay(pdMS_TO_TICKS(500));

  if (!wifi.start()) {
    snprintf(last_error, sizeof(last_error), "WiFi connection failed.\nCheck wifi_ssid / wifi_pwd.");
    state = State::ERROR;
    return;
  }
#endif

  current_url = url;   // remember root URL for history

  show_fetching(url);
  state = State::FETCHING;

  if (!opds.fetch_catalog(url.c_str(), user.c_str(), pwd.c_str(),
                          entries, last_error, sizeof(last_error))) {
    state = State::ERROR;
#if EPUB_INKPLATE_BUILD
    wifi.stop();  // Stop now; download won't happen
#endif
    return;
  }

  // Calculate entries per page (leave FOOTER_RESERVE px for nav hint)
  entries_per_page =
    (Screen::get_height() - FIRST_ENTRY_YPOS - FOOTER_RESERVE) / ENTRY_HEIGHT;
  if (entries_per_page < 1) entries_per_page = 1;

  current_page = 0;
  state = State::SHOWING_LIST;
  // WiFi stays on — we may need it for the download
}

// ---------------------------------------------------------------------------
// fetch_page  — navigate to a sub-catalog while WiFi is already running
// ---------------------------------------------------------------------------

void
OPDSController::fetch_page(const std::string & url)
{
  std::string user, pwd;
  config.get(Config::Ident::OPDS_USER, user);
  config.get(Config::Ident::OPDS_PWD,  pwd);

  current_url   = url;   // copy the URL string BEFORE clearing entries
  current_page  = 0;
  selected_idx  = -1;
  entries.clear();       // invalidates the reference 'url' was pointing into

  show_fetching(url);

  // Use current_url (our own copy), NOT url.c_str() — the reference is
  // dangling after entries.clear() if the caller passed entries[i].nav_url.
  if (!opds.fetch_catalog(current_url.c_str(), user.c_str(), pwd.c_str(),
                          entries, last_error, sizeof(last_error))) {
    state = State::ERROR;
    return;
  }

  entries_per_page =
    (Screen::get_height() - FIRST_ENTRY_YPOS - FOOTER_RESERVE) / ENTRY_HEIGHT;
  if (entries_per_page < 1) entries_per_page = 1;

  state = State::SHOWING_LIST;
}

// ---------------------------------------------------------------------------
// start_download
// ---------------------------------------------------------------------------

void
OPDSController::start_download(int idx)
{
  if (idx < 0 || idx >= static_cast<int>(entries.size())) {
    snprintf(last_error, sizeof(last_error), "Invalid entry index");
    state = State::ERROR;
    return;
  }

  const OPDSEntry & entry = entries[idx];

  std::string  url      = entry.download_url;
  std::string  filename = filename_for_entry(entry.title, url, entry.author, entry.year);
  std::string  filepath = std::string(MAIN_FOLDER) + "/books/" + filename;

  // Initial full-screen progress display
  state          = State::DOWNLOADING;
  dl_total_bytes = -1;
  dl_bytes_done  = 0;
  dl_last_pct    = -1;
  dl_cancelled   = false;

  // Show the initial download screen (full refresh)
  page.set_compute_mode(Page::ComputeMode::DISPLAY);
  Page::Format fmt = make_fmt(1, TITLE_FONT_SIZE, CSS::Align::CENTER,
                               0, 0);
  page.start(fmt);

  // Title bar
  fmt.margin_top = TITLE_YPOS;
  page.set_limits(fmt);
  page.new_paragraph(fmt);
  std::string title_str = "Downloading...";
  page.add_text(title_str, fmt);
  page.end_paragraph(fmt);

  // Book title
  fmt.font_size  = ENTRY_FONT_SIZE;
  fmt.margin_top = 0;
  fmt.screen_top = FIRST_ENTRY_YPOS;
  fmt.screen_bottom = static_cast<int16_t>(Screen::get_height() - FIRST_ENTRY_YPOS - 40);

  page.set_limits(fmt);
  page.new_paragraph(fmt);
  std::string book_title = entry.title;
  if (!entry.author.empty()) book_title += "\n" + entry.author;
  page.add_text(book_title, fmt);
  page.end_paragraph(fmt);

  // Empty progress bar outline
  const int bar_x   = PROGBAR_MARGIN;
  const int max_w   = Screen::get_width() - 2 * PROGBAR_MARGIN;
  page.put_highlight(Dim(max_w, PROGBAR_H), Pos(bar_x, PROGBAR_Y));

  // Footer hint
  fmt.font_size   = 9;
  fmt.margin_top  = 0;
  fmt.screen_top  = static_cast<int16_t>(PROGBAR_Y + PROGBAR_H + 80);
  fmt.screen_bottom = 5;
  page.set_limits(fmt);
  page.new_paragraph(fmt);
  std::string hint_str = "Please wait...";
  page.add_text(hint_str, fmt);
  page.end_paragraph(fmt);

  page.paint(true, false, false);   // Full refresh (clears previous screen)

  // Read config for auth (WiFi still connected)
  std::string user, pwd;
  config.get(Config::Ident::OPDS_USER, user);
  config.get(Config::Ident::OPDS_PWD,  pwd);

  // Progress callback — called every DOWNLOAD_BUF_SIZE bytes.
  // Throttles redraws: every 2% when content-length is known,
  // or every 200 KB when it is unknown (chunked transfer).
  auto progress_cb = [this](int64_t done, int64_t total) -> bool {
    dl_bytes_done = done;

    if (total > 0) {
      // Known total — update every 2 percentage points
      int new_pct = static_cast<int>(100 * done / total);
      if (new_pct == dl_last_pct) return true;
      if (new_pct < dl_last_pct + 2 && new_pct < 100) return true;
      dl_last_pct = new_pct;
    } else {
      // Unknown total — dl_last_pct stores number of 200 KB chunks rendered
      int chunks = static_cast<int>(done / (200LL * 1024));
      if (chunks <= dl_last_pct) return true;
      dl_last_pct = chunks;
    }

    show_progress(done, total);
    return true;   // no cancel support in first iteration
  };

  if (!opds.download_book(url.c_str(), user.c_str(), pwd.c_str(),
                          filepath.c_str(), progress_cb,
                          last_error, sizeof(last_error))) {
    state = State::ERROR;
  } else {
    state = State::DONE;
  }

#if EPUB_INKPLATE_BUILD
  wifi.stop();
#endif
}

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------

void
OPDSController::show_connecting()
{
  msg_viewer.show(MsgViewer::MsgType::WIFI, false, true,
                  "OPDS", "Connecting to WiFi...");
}

void
OPDSController::show_fetching(const std::string & url)
{
  // Insert a space after every '/' so the page renderer can word-wrap the URL
  // (a bare URL with no spaces is treated as one giant word and causes
  // WORD TOO LARGE).  Also strip the scheme prefix to save horizontal space.
  const char * p = url.c_str();
  if (strncmp(p, "https://", 8) == 0) p += 8;
  else if (strncmp(p, "http://",  7) == 0) p += 7;

  std::string display_url;
  display_url.reserve(strlen(p) + 16);
  for (; *p; ++p) {
    display_url += *p;
    // Insert a zero-width break opportunity after '/' and '.' so the page
    // renderer can word-wrap the URL instead of treating it as one giant word.
    if (*p == '/' || *p == '.') display_url += ' ';
  }

  std::string msg = "Fetching OPDS catalog\n" + display_url;
  msg_viewer.show(MsgViewer::MsgType::WIFI, false, true,
                  "OPDS", msg.c_str());
}

void
OPDSController::show_list()
{
  page.set_compute_mode(Page::ComputeMode::DISPLAY);

  // Full-screen format for the title, then reused for entries
  Page::Format fmt = make_fmt(1, TITLE_FONT_SIZE, CSS::Align::CENTER,
                               0, 0);

  page.start(fmt);

  // Header: "OPDS Catalog — page N/M"
  fmt.margin_top = TITLE_YPOS;
  page.set_limits(fmt);
  page.new_paragraph(fmt);
  char hdr[64];
  if (pages() > 1) {
    snprintf(hdr, sizeof(hdr), "OPDS  %d / %d", current_page + 1, pages());
  } else {
    snprintf(hdr, sizeof(hdr), "OPDS  Catalog");
  }
  std::string hdr_str = hdr;
  page.add_text(hdr_str, fmt);
  page.end_paragraph(fmt);

  // Entry rows
  int start_idx = current_page * entries_per_page;
  int end_idx   = std::min(static_cast<int>(entries.size()),
                           start_idx + entries_per_page);

  fmt.font_size     = ENTRY_FONT_SIZE;
  fmt.margin_top    = 0;
  fmt.align         = CSS::Align::LEFT;
  fmt.screen_left   = ENTRY_MARGIN_LEFT;
  fmt.screen_right  = 10;

  int16_t ypos = FIRST_ENTRY_YPOS;

  for (int i = start_idx; i < end_idx; i++) {
    std::string text;
    if (entries[i].is_nav()) {
      text = "[+] ";
      text += entries[i].title;
    } else {
      text = entries[i].title;
      if (!entries[i].author.empty()) {
        text += " \xe2\x80\x94 ";   // UTF-8 em-dash
        text += entries[i].author;
      }
    }
    // Truncate to prevent WORD TOO LARGE when a title contains a long
    // URL-like string with no spaces.
    if (text.size() > 80) {
      text.resize(77);
      text += "...";
    }

    fmt.screen_top    = ypos;
    fmt.screen_bottom = static_cast<int16_t>(Screen::get_height() - (ypos + ENTRY_HEIGHT));

    page.set_limits(fmt);
    page.new_paragraph(fmt);
    page.add_text(text, fmt);
    page.end_paragraph(fmt);

    ypos += ENTRY_HEIGHT;
  }

  // Footer navigation hint
  fmt.font_size     = 9;
  fmt.align         = CSS::Align::CENTER;
  fmt.screen_left   = 0;
  fmt.screen_right  = 0;
  fmt.screen_top    = static_cast<int16_t>(Screen::get_height() - FOOTER_RESERVE + 6);
  fmt.screen_bottom = 5;

  page.set_limits(fmt);
  page.new_paragraph(fmt);

  bool has_nav = false;
  for (const auto & e : entries) { if (e.is_nav()) { has_nav = true; break; } }

  std::string nav_hint;
  if (pages() > 1) {
    nav_hint = "Swipe left/right to page";
  }
  if (has_nav) {
    if (!nav_hint.empty()) nav_hint += "   |   ";
    nav_hint += "Tap [+] to browse, book to download";
  } else {
    if (!nav_hint.empty()) nav_hint += "   |   ";
    nav_hint += "Tap entry to download";
  }
  if (!url_stack.empty()) {
    nav_hint += "   |   Swipe right to go back";
  }
  nav_hint += "   |   Tap here to exit";

  page.add_text(nav_hint, fmt);
  page.end_paragraph(fmt);

  ScreenBottom::show(current_page, pages());
  page.paint(true, false, false);
}

void
OPDSController::show_confirm()
{
  if (selected_idx < 0 || selected_idx >= static_cast<int>(entries.size())) return;
  const OPDSEntry & e = entries[selected_idx];
  std::string msg = e.title;
  if (!e.author.empty()) { msg += "\n"; msg += e.author; }
  msg_viewer.show(MsgViewer::MsgType::CONFIRM, true, false,
                  "Download this book?", msg.c_str());
}

void
OPDSController::show_progress(int64_t done, int64_t total)
{
  page.set_compute_mode(Page::ComputeMode::DISPLAY);

  const int bar_x   = PROGBAR_MARGIN;
  const int max_w   = Screen::get_width() - 2 * PROGBAR_MARGIN;
  const int fill_w  = (total > 0)
                      ? static_cast<int>(max_w * done / total)
                      : static_cast<int>((done / (200 * 1024)) * 40 % max_w);

  // Clear the progress area (bar + text below it)
  const int16_t clear_y = static_cast<int16_t>(PROGBAR_Y - 4);
  const int16_t clear_h = static_cast<int16_t>(PROGBAR_H + 60);

  Page::Format fmt = make_fmt(1, ENTRY_FONT_SIZE, CSS::Align::CENTER,
                               PROGBAR_Y, 5);
  page.start(fmt);

  page.clear_region(Dim(Screen::get_width(), clear_h), Pos(0, clear_y));
  page.put_highlight(Dim(max_w,    PROGBAR_H    ), Pos(bar_x,     PROGBAR_Y    ));  // border
  if (fill_w > 2)
    page.set_region(Dim(fill_w - 2, PROGBAR_H - 4), Pos(bar_x + 2, PROGBAR_Y + 2)); // fill

  // Percentage / size text
  char pct_buf[48];
  if (total > 0) {
    snprintf(pct_buf, sizeof(pct_buf), "%d%%  (%lld / %lld KB)",
             static_cast<int>(100 * done / total),
             done / 1024, total / 1024);
  } else {
    snprintf(pct_buf, sizeof(pct_buf), "%lld KB received", done / 1024);
  }

  fmt.screen_top    = static_cast<int16_t>(PROGBAR_Y + PROGBAR_H + 8);
  fmt.screen_bottom = 5;
  page.set_limits(fmt);
  page.new_paragraph(fmt);
  std::string pct_str = pct_buf;
  page.add_text(pct_str, fmt);
  page.end_paragraph(fmt);

  page.paint(false, true, true);  // Partial refresh, force update
}

void
OPDSController::show_done()
{
  msg_viewer.show(MsgViewer::MsgType::INFO, true, true,
                  "Download Complete",
                  "The book has been saved.\nTap anywhere to reboot and\naccess it from the library.");
}

void
OPDSController::show_error()
{
  msg_viewer.show(MsgViewer::MsgType::ALERT, true, true,
                  "OPDS Error", last_error);
}
