// Copyright (c) 2024
//
// MIT License. Look at file licenses.txt for details.

#define __OPDS__ 1
#include "models/opds.hpp"

#include "logging.hpp"

#include <cstring>
#include <cstdio>

#include "pugixml.hpp"

#if EPUB_INKPLATE_BUILD
  #include "esp_http_client.h"
  #include "esp_crt_bundle.h"
  #include "esp_system.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
#endif

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void
OPDS::base64_encode(const uint8_t * src, size_t src_len,
                    char          * dst, size_t dst_size)
{
  static const char TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  size_t out = 0;
  for (size_t i = 0; i < src_len && (out + 5) < dst_size; i += 3) {
    const uint32_t a = src[i];
    const uint32_t b = (i + 1 < src_len) ? src[i + 1] : 0u;
    const uint32_t c = (i + 2 < src_len) ? src[i + 2] : 0u;
    const uint32_t combined = (a << 16) | (b << 8) | c;
    dst[out++] = TABLE[(combined >> 18) & 0x3F];
    dst[out++] = TABLE[(combined >> 12) & 0x3F];
    dst[out++] = (i + 1 < src_len) ? TABLE[(combined >> 6) & 0x3F] : '=';
    dst[out++] = (i + 2 < src_len) ? TABLE[(combined >> 0) & 0x3F] : '=';
  }
  dst[out] = '\0';
}

void
OPDS::set_auth_header(void * client_ptr, const char * user, const char * pwd)
{
#if EPUB_INKPLATE_BUILD
  char credentials[96];
  snprintf(credentials, sizeof(credentials), "%s:%s", user, pwd ? pwd : "");

  // Max base64 output: ceil(96/3)*4 + 1 = 128 + 1
  char b64[132];
  base64_encode(reinterpret_cast<const uint8_t *>(credentials),
                strlen(credentials), b64, sizeof(b64));

  char header[148]; // "Basic " (6) + 132 = 138, round up
  snprintf(header, sizeof(header), "Basic %s", b64);
  esp_http_client_set_header(
    static_cast<esp_http_client_handle_t>(client_ptr),
    "Authorization", header);
#endif
}

/// Resolve a potentially-relative @p href against @p base catalog URL.
static std::string
resolve_url(const std::string & base, const char * href)
{
  if (!href || !href[0]) return {};

  // Already absolute
  if (strncmp(href, "http://",  7) == 0 ||
      strncmp(href, "https://", 8) == 0)
    return std::string(href);

  // Find end of scheme+host in base ("https://host")
  const char * p = base.c_str();
  const char * ds = strstr(p, "://");
  if (!ds) return std::string(href);

  const char * host_start = ds + 3;
  const char * host_end   = strchr(host_start, '/');
  std::string origin(p, host_end ? static_cast<size_t>(host_end - p) : base.size());

  if (href[0] == '/') {
    // Absolute path – prepend origin
    return origin + href;
  }

  // Relative path – append to directory part of base URL
  std::string dir = base;
  const size_t slash = dir.rfind('/');
  if (slash != std::string::npos) dir = dir.substr(0, slash + 1);
  return dir + href;
}

// ---------------------------------------------------------------------------
// fetch_catalog
// ---------------------------------------------------------------------------

bool
OPDS::fetch_catalog(const char           * url,
                    const char           * user,
                    const char           * pwd,
                    std::vector<OPDSEntry> & entries,
                    char                 * error_msg,
                    size_t                 error_size)
{
  entries.clear();

#if EPUB_INKPLATE_BUILD

  esp_http_client_config_t cfg = {};
  cfg.url               = url;
  cfg.timeout_ms        = 15000;
  cfg.keep_alive_enable = false;
  // Always attach the CRT bundle so that HTTP→HTTPS redirects are handled
  // transparently without needing to reinitialise the client handle.
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  // Use small explicit I/O buffers; the default values can be much larger
  // and are allocated during esp_http_client_init while the WiFi stack is
  // already resident in RAM, leaving little headroom.
  cfg.buffer_size    = 512;
  cfg.buffer_size_tx = 512;

  // Validate URL scheme before passing to esp_http_client_init; a missing
  // "http://" or "https://" prefix causes init to return NULL with no
  // further diagnostics.
  if (strncmp(url, "http://",  7) != 0 &&
      strncmp(url, "https://", 8) != 0) {
    snprintf(error_msg, error_size,
             "Bad URL (must start with http:// or https://):\n%.80s", url);
    return false;
  }

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    snprintf(error_msg, error_size,
             "HTTP client init failed (free heap: %u B).\n"
             "URL: %.80s",
             (unsigned) esp_get_free_heap_size(), url);
    return false;
  }

  if (user && user[0]) set_auth_header(client, user, pwd);
  esp_http_client_set_header(client, "Accept", "application/atom+xml,application/xml,text/xml");

  esp_err_t rc = esp_http_client_open(client, 0);
  if (rc != ESP_OK) {
    esp_http_client_cleanup(client);
    snprintf(error_msg, error_size, "open: %s", esp_err_to_name(rc));
    return false;
  }

  // Follow up to 5 redirects (HTTP 301/302/307/308)
  int http_status = 0;
  for (int redir = 0; redir < 5; redir++) {
    esp_http_client_fetch_headers(client);
    http_status = esp_http_client_get_status_code(client);
    if (http_status < 300 || http_status >= 400) break;

    char * location = nullptr;
    esp_http_client_get_header(client, "Location", &location);
    if (!location || !location[0]) break;

    esp_http_client_close(client);
    esp_http_client_set_url(client, location);
    rc = esp_http_client_open(client, 0);
    if (rc != ESP_OK) {
      esp_http_client_cleanup(client);
      snprintf(error_msg, error_size, "redirect open: %s", esp_err_to_name(rc));
      return false;
    }
  }

  if (http_status != 200) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    snprintf(error_msg, error_size, "HTTP %d", http_status);
    return false;
  }

  // Read body — close the HTTP client first so the transport layer memory
  // is released before allocating the string + XML DOM.
  std::string body;
  {
    static char buf[512];
    int bytes;
    while ((bytes = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
      body.append(buf, static_cast<size_t>(bytes));
      if (body.size() >= MAX_CATALOG_BYTES) break;
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (body.empty()) {
    snprintf(error_msg, error_size, "Empty response");
    return false;
  }

  // Parse OPDS Atom XML
  pugi::xml_document doc;
  pugi::xml_parse_result result = doc.load_string(body.c_str());
  if (!result) {
    snprintf(error_msg, error_size, "XML: %s", result.description());
    return false;
  }

  // Root may be <feed> or <feed xmlns="...">; pugixml gives us the local name.
  pugi::xml_node feed = doc.child("feed");
  if (!feed) {
    // Some servers wrap in a root element – try first child
    feed = doc.first_child();
  }

  const std::string base_url = url;

  for (pugi::xml_node entry : feed.children("entry")) {
    if (entries.size() >= MAX_ENTRIES) break;

    OPDSEntry e;
    e.title = entry.child_value("title");

    pugi::xml_node author_node = entry.child("author");
    if (author_node) e.author = author_node.child_value("name");

    // Find an epub+zip acquisition link, or a navigation/subsection link.
    for (pugi::xml_node link : entry.children("link")) {
      const char * rel  = link.attribute("rel").value();
      const char * type = link.attribute("type").value();
      const char * href = link.attribute("href").value();

      if (strcmp(rel, "http://opds-spec.org/acquisition") == 0 &&
          strstr(type, "epub") != nullptr) {
        e.download_url = resolve_url(base_url, href);
        break; // acquisition takes priority
      }

      // Navigation entries: rel="subsection" or catalog/navigation types
      if (e.nav_url.empty()) {
        if (strcmp(rel, "subsection") == 0 ||
            strcmp(rel, "http://opds-spec.org/sort/new")    == 0 ||
            strcmp(rel, "http://opds-spec.org/sort/popular") == 0 ||
            strstr(type, "opds-catalog") != nullptr) {
          e.nav_url = resolve_url(base_url, href);
        }
      }
    }

    // Keep acquisition entries AND navigation entries (folders).
    if (!e.title.empty() && (!e.download_url.empty() || !e.nav_url.empty())) {
      entries.push_back(std::move(e));
    }
  }

  if (entries.empty()) {
    snprintf(error_msg, error_size, "Empty catalog (no books or folders found)");
    return false;
  }

  return true;

#else
  // Linux / test build – return stubbed entries for UI testing
  entries.push_back({ "Test Book One",   "Author A", "http://example.com/1.epub" });
  entries.push_back({ "Test Book Two",   "Author B", "http://example.com/2.epub" });
  entries.push_back({ "Test Book Three", "Author C", "http://example.com/3.epub" });
  return true;
#endif
}

// ---------------------------------------------------------------------------
// download_book
// ---------------------------------------------------------------------------

bool
OPDS::download_book(const char * url,
                    const char * user,
                    const char * pwd,
                    const char * filepath,
                    ProgressCb   progress_cb,
                    char       * error_msg,
                    size_t       error_size)
{
#if EPUB_INKPLATE_BUILD

  esp_http_client_config_t cfg = {};
  cfg.url               = url;
  cfg.timeout_ms        = 60000;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.keep_alive_enable = false;
  cfg.buffer_size       = DOWNLOAD_BUF_SIZE;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    snprintf(error_msg, error_size, "http_init failed");
    return false;
  }

  if (user && user[0]) set_auth_header(client, user, pwd);

  esp_err_t rc = esp_http_client_open(client, 0);
  if (rc != ESP_OK) {
    esp_http_client_cleanup(client);
    snprintf(error_msg, error_size, "open: %s", esp_err_to_name(rc));
    return false;
  }

  // Follow up to 5 redirects (HTTP 301/302/307/308)
  int64_t content_length = 0;
  int http_status = 0;
  for (int redir = 0; redir < 5; redir++) {
    content_length = esp_http_client_fetch_headers(client);
    http_status = esp_http_client_get_status_code(client);
    if (http_status < 300 || http_status >= 400) break;

    char * location = nullptr;
    esp_http_client_get_header(client, "Location", &location);
    if (!location || !location[0]) break;

    esp_http_client_close(client);
    esp_http_client_set_url(client, location);
    rc = esp_http_client_open(client, 0);
    if (rc != ESP_OK) {
      esp_http_client_cleanup(client);
      snprintf(error_msg, error_size, "redirect open: %s", esp_err_to_name(rc));
      return false;
    }
  }

  if (http_status != 200) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    snprintf(error_msg, error_size, "HTTP %d", http_status);
    return false;
  }

  FILE * fp = fopen(filepath, "wb");
  if (!fp) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    snprintf(error_msg, error_size, "Cannot create file: %s", filepath);
    return false;
  }

  static char buf[DOWNLOAD_BUF_SIZE];
  int64_t  bytes_done = 0;
  bool     cancelled  = false;
  int      bytes;

  while ((bytes = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
    if (fwrite(buf, 1, static_cast<size_t>(bytes), fp) != static_cast<size_t>(bytes)) {
      fclose(fp);
      remove(filepath);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      snprintf(error_msg, error_size, "Write error (disk full?)");
      return false;
    }
    bytes_done += bytes;

    // Invoke the progress callback; abort if it returns false
    if (progress_cb && !progress_cb(bytes_done, content_length)) {
      cancelled = true;
      break;
    }
  }

  fclose(fp);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (cancelled) {
    remove(filepath);  // Clean up partial file
    return true;       // Cancellation is not an error; controller handles it
  }

  if (bytes < 0) {
    remove(filepath);
    snprintf(error_msg, error_size, "Read error");
    return false;
  }

  return true;

#else
  (void)url; (void)user; (void)pwd; (void)filepath; (void)progress_cb;
  snprintf(error_msg, error_size, "Not supported on Linux build");
  return false;
#endif
}
