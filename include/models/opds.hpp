// Copyright (c) 2024
//
// MIT License. Look at file licenses.txt for details.

#pragma once
#include "global.hpp"

#include <string>
#include <vector>
#include <functional>

/**
 * @brief Single entry returned from an OPDS Atom catalog
 *
 * Exactly one of download_url or nav_url will be non-empty:
 *  - download_url: acquisition entry (epub+zip) — user can download the book.
 *  - nav_url:      navigation/subsection entry — user can browse into it.
 */
struct OPDSEntry {
  std::string title;
  std::string author;
  std::string download_url; ///< Full URL to the .epub file (acquisition entry)
  std::string nav_url;      ///< Full URL of the sub-catalog (navigation entry)

  bool is_nav() const { return !nav_url.empty() && download_url.empty(); }
};

/**
 * @brief OPDS client model
 *
 * Performs HTTP/HTTPS catalog fetching and streaming book downloads.
 * Supports optional HTTP Basic authentication (no SDK macro needed — header
 * is built manually with a small compile-time base64 encoder).
 *
 * Usage:
 *   OPDS opds;
 *   std::vector<OPDSEntry> entries;
 *   char err[128];
 *   if (opds.fetch_catalog(url, user, pwd, entries, err, sizeof(err))) { ... }
 *   if (opds.download_book(url, user, pwd, filepath, progress_cb, err, sizeof(err))) { ... }
 */
class OPDS
{
  public:
    static constexpr size_t MAX_CATALOG_BYTES = 128 * 1024; ///< Safety cap on catalog body
    static constexpr size_t MAX_ENTRIES       = 200;        ///< Max entries kept in memory
    static constexpr size_t DOWNLOAD_BUF_SIZE = 4096;       ///< Read buffer for streaming download

    /**
     * @brief Progress callback for download_book.
     *
     * @param bytes_done  Bytes written to disk so far.
     * @param total_bytes Total content length (-1 if unknown).
     *
     * Return false to abort the download.
     */
    using ProgressCb = std::function<bool(int64_t bytes_done, int64_t total_bytes)>;

    /**
     * @brief Fetch and parse an OPDS Atom catalog.
     *
     * Connects to @p url (http or https), optionally sets a Basic-Auth header,
     * downloads the Atom XML and fills @p entries with acquisition entries.
     *
     * @param url        OPDS catalog URL (http:// or https://).
     * @param user       Username for Basic auth, or "" to skip auth.
     * @param pwd        Password for Basic auth.
     * @param entries    Output vector filled with parsed entries.
     * @param error_msg  Buffer for a human-readable error on failure.
     * @param error_size Size of @p error_msg buffer.
     * @return true on success, false on any error.
     */
    bool fetch_catalog(const char           * url,
                       const char           * user,
                       const char           * pwd,
                       std::vector<OPDSEntry> & entries,
                       char                 * error_msg,
                       size_t                 error_size);

    /**
     * @brief Download a single book to the SD card.
     *
     * Streams the file at @p url directly to @p filepath on the SD card.
     * Calls @p progress_cb after every DOWNLOAD_BUF_SIZE bytes are flushed;
     * if the callback returns false the download is aborted and the partial
     * file is removed.
     *
     * @param url         Direct download URL for the epub file.
     * @param user        Username (may be "").
     * @param pwd         Password.
     * @param filepath    Destination path, e.g. "/sdcard/books/book.epub".
     * @param progress_cb Called with (bytes_done, total_bytes). Return false to cancel.
     * @param error_msg   Buffer for human-readable error on failure.
     * @param error_size  Size of @p error_msg buffer.
     * @return true on success / cancelled cleanly, false on I/O or network error.
     */
    bool download_book(const char * url,
                       const char * user,
                       const char * pwd,
                       const char * filepath,
                       ProgressCb   progress_cb,
                       char       * error_msg,
                       size_t       error_size);

  private:
    static constexpr char const * TAG = "OPDS";

    /**
     * @brief Encode @p src to Base64 in @p dst (null-terminated).
     * @param src      Input bytes.
     * @param src_len  Number of input bytes.
     * @param dst      Output buffer; must be at least ceil(src_len/3)*4+1 bytes.
     * @param dst_size Size of output buffer.
     */
    static void base64_encode(const uint8_t * src, size_t src_len,
                               char          * dst, size_t dst_size);

    /**
     * @brief Set the Authorization: Basic header on an esp_http_client handle.
     * @param client  Initialised (not yet opened) client handle.
     * @param user    Username string.
     * @param pwd     Password string.
     */
    void set_auth_header(void * client, const char * user, const char * pwd);
};

#if __OPDS__
  OPDS opds;
#else
  extern OPDS opds;
#endif
