// Copyright (c) 2024
//
// MIT License. Look at file licenses.txt for details.

#pragma once
#include "global.hpp"

#include "controllers/event_mgr.hpp"
#include "models/opds.hpp"

#include <vector>
#include <string>

/**
 * @brief OPDS catalog browser and downloader
 *
 * State machine:
 *
 *   IDLE
 *    │ enter()
 *    ▼
 *   CONNECTING  (WiFi start)
 *    │ success
 *    ▼
 *   FETCHING    (HTTP catalog download)
 *    │ success
 *    ▼
 *   SHOWING_LIST◄─────────── CONFIRM_DOWNLOAD (Cancel)
 *    │ tap on entry
 *    ▼
 *   CONFIRM_DOWNLOAD
 *    │ OK
 *    ▼
 *   DOWNLOADING
 *    │ complete
 *    ▼
 *   DONE        (tap any → reboot)
 *
 *   Any step on error → ERROR (tap any → return_to_last)
 */
class OPDSController
{
  public:
    OPDSController() : state(State::IDLE) {}

    void enter();
    void leave(bool going_to_deep_sleep = false);
    void input_event(const EventMgr::Event & event);

  private:
    static constexpr char const * TAG = "OPDSCtrl";

    // -----------------------------------------------------------------------
    // Layout constants
    // -----------------------------------------------------------------------
    static const int16_t TITLE_FONT_SIZE    =  14;
    static const int16_t ENTRY_FONT_SIZE    =  11;
    static const int16_t AUTHOR_FONT_SIZE   =   9;
    static const int16_t TITLE_YPOS         =  25;
    static const int16_t FIRST_ENTRY_YPOS   =  70;
    static const int16_t ENTRY_HEIGHT       =  55; ///< px per catalog entry (title + author lines)
    static const int16_t ENTRY_AUTHOR_YOFFSET = 27; ///< y-offset inside an entry for the author line
    static const int16_t ENTRY_MARGIN_LEFT  =  15;
    static const int16_t FOOTER_RESERVE     = 100; ///< px reserved at the bottom for nav hints

    // Progress-bar drawing
    static const int16_t PROGBAR_Y          = 480; ///< Centre-screen Y for progress bar
    static const int16_t PROGBAR_H          =  28;
    static const int16_t PROGBAR_MARGIN     =  40; ///< Left/right margin for the bar
    static const int16_t CANCEL_BTN_Y       = 580; ///< Top Y of the cancel touch area
    static const int16_t CANCEL_BTN_H       =  60; ///< Height of the cancel touch area

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    enum class State { IDLE, CONNECTING, FETCHING,
                       SHOWING_LIST, CONFIRM_DOWNLOAD, DOWNLOADING,
                       DONE, ERROR };

    State state;

    std::vector<OPDSEntry>  entries;
    int  current_page;
    int  entries_per_page;
    int  selected_idx;   ///< Index into entries[] of the book/folder tapped

    // Navigation history — used when the user browses into sub-catalogs.
    std::string              current_url;  ///< URL of the currently displayed catalog
    std::vector<std::string> url_stack;    ///< Stack of parent URLs (for back navigation)

    // Download progress tracking
    int64_t  dl_total_bytes;       ///< Content-Length (-1 if unknown)
    int64_t  dl_bytes_done;
    int      dl_last_pct;          ///< Last percentage shown (to throttle redraws)
    bool     dl_cancelled;

    char     last_error[128];

    // -----------------------------------------------------------------------
    // Helper methods
    // -----------------------------------------------------------------------
    int  pages() const;

    void connect_and_fetch();
    void fetch_page(const std::string & url);  ///< Re-fetch catalog at url (WiFi already up)
    void start_download(int idx);
    void do_return();

    void show_connecting();
    void show_fetching(const std::string & url);
    void show_list();
    void show_confirm();
    void show_progress(int64_t done, int64_t total);
    void show_done();
    void show_error();

    bool is_on_device(const OPDSEntry & e) const; ///< true if the epub file already exists on the SD card
};

#if __OPDS_CONTROLLER__
  OPDSController opds_controller;
#else
  extern OPDSController opds_controller;
#endif
