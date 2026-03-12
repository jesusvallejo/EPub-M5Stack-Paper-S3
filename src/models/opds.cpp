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
  #include "lwip/netdb.h"
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
/// Also upgrades http:// → https:// when the base uses HTTPS and the href
/// refers to the same hostname — avoids a 301 redirect round-trip through
/// Traefik that arrives with a TLS close_notify before esp_http_client can
/// parse the response headers.
static std::string
resolve_url(const std::string & base, const char * href)
{
  if (!href || !href[0]) return {};

  // Already absolute
  if (strncmp(href, "http://",  7) == 0 ||
      strncmp(href, "https://", 8) == 0) {
    // Upgrade http:// → https:// when base is https:// and hostname matches.
    if (strncmp(base.c_str(), "https://", 8) == 0 &&
        strncmp(href, "http://", 7) == 0) {
      // Extract hostname from base (after "https://", up to '/', ':', or end)
      const char * bh = base.c_str() + 8;
      const char * be = bh;
      while (*be && *be != '/' && *be != ':') ++be;

      // Extract hostname from href (after "http://", up to '/', ':', or end)
      const char * hh = href + 7;
      const char * he = hh;
      while (*he && *he != '/' && *he != ':') ++he;

      if ((be - bh) == (he - hh) && strncmp(bh, hh, static_cast<size_t>(be - bh)) == 0) {
        // Same host: rewrite scheme
        return std::string("https://") + (href + 7);
      }
    }
    return std::string(href);
  }

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
// HTTP event handler for body collection (used by fetch_catalog)
// ---------------------------------------------------------------------------

#if EPUB_INKPLATE_BUILD
struct OpdsFetchCtx {
  std::string * body;
  size_t        max_bytes;
  char          location[512]; // captured from Location: header
  bool          finished;      // HTTP_EVENT_ON_FINISH received
};

static esp_err_t opds_http_event(esp_http_client_event_t * evt)
{
  auto * ctx = static_cast<OpdsFetchCtx *>(evt->user_data);
  if (!ctx) return ESP_OK;

  switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
      // Capture redirect location — compare case-insensitively
      if (evt->header_key && strcasecmp(evt->header_key, "Location") == 0
          && evt->header_value) {
        strncpy(ctx->location, evt->header_value, sizeof(ctx->location) - 1);
        ctx->location[sizeof(ctx->location) - 1] = '\0';
      }
      break;
    case HTTP_EVENT_ON_DATA:
      if (evt->data_len > 0) {
        if (ctx->body->size() < ctx->max_bytes) {
          size_t space = ctx->max_bytes - ctx->body->size();
          size_t add = static_cast<size_t>(evt->data_len) < space
                     ? static_cast<size_t>(evt->data_len) : space;
          ctx->body->append(static_cast<const char *>(evt->data), add);
        }
      }
      break;
    case HTTP_EVENT_ON_FINISH:
      ctx->finished = true;
      break;
    default:
      break;
  }
  return ESP_OK;
}
#endif

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
  // 4 KiB == one TLS record; receiving the entire response header block in
  // a single read avoids close_notify arriving mid-header-parse.
  cfg.buffer_size    = 4096;
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

  LOG_I("OPDS fetch_catalog: URL=%s", url);
  LOG_I("OPDS free heap before init: %u B", (unsigned) esp_get_free_heap_size());

  // Manual DNS resolution so we can log what IP the hostname resolves to.
  {
    // Extract hostname from URL (between :// and the next / or :)
    const char * host_start = strstr(url, "://");
    if (host_start) {
      host_start += 3;
      const char * host_end = host_start;
      while (*host_end && *host_end != '/' && *host_end != ':') host_end++;
      char hostname[128] = {};
      size_t hlen = static_cast<size_t>(host_end - host_start);
      if (hlen < sizeof(hostname)) {
        memcpy(hostname, host_start, hlen);
        hostname[hlen] = '\0';
        LOG_I("OPDS DNS lookup: hostname='%s'", hostname);
        struct addrinfo hints = {};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo * res = nullptr;
        int gai_err = getaddrinfo(hostname, nullptr, &hints, &res);
        if (gai_err != 0 || !res) {
          LOG_E("OPDS DNS FAILED for '%s': err=%d", hostname, gai_err);
        } else {
          char ipstr[64] = "?";
          if (res->ai_family == AF_INET) {
            auto * s = reinterpret_cast<struct sockaddr_in *>(res->ai_addr);
            inet_ntoa_r(s->sin_addr, ipstr, sizeof(ipstr));
          } else {
            snprintf(ipstr, sizeof(ipstr), "(IPv6 or unknown family %d)", res->ai_family);
          }
          LOG_I("OPDS DNS resolved '%s' -> %s (family=%d)",
                hostname, ipstr, res->ai_family);
          freeaddrinfo(res);
        }
      }
    }
  }

  // Body is collected via HTTP_EVENT_ON_DATA; ON_HEADER captures Location so
  // we can follow one redirect ourselves without relying on perform()'s
  // auto-redirect — which uses fetch_headers() internally and fails when
  // Traefik sends TLS close_notify immediately after the 301 response headers.
  std::string body;

  // One-shot request helper: creates a fresh client, performs, cleans up.
  // Returns HTTP status code, or <=0 on transport error.
  // Fills redirect_out when a Location header was received.
  auto do_one_request = [&](const char * req_url,
                             std::string & redirect_out) -> int
  {
    body.clear();
    OpdsFetchCtx ctx{};
    ctx.body      = &body;
    ctx.max_bytes = MAX_CATALOG_BYTES;

    LOG_I("OPDS do_one_request: '%s'", req_url);

    cfg.url                   = req_url;
    cfg.event_handler         = opds_http_event;
    cfg.user_data             = &ctx;
    cfg.max_redirection_count = 0;
    cfg.disable_auto_redirect = true;

    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) {
      LOG_E("OPDS init NULL for %.80s", req_url);
      return -2;
    }
    if (user && user[0]) set_auth_header(cl, user, pwd);
    esp_http_client_set_header(cl, "Accept",
      "application/atom+xml,application/xml,text/xml");
    // Do NOT set Connection: close — that forces Traefik to send TLS
    // close_notify in the same burst as the response headers, which causes
    // esp_http_client_fetch_headers() to fail before parsing the status line.

    esp_err_t err  = esp_http_client_perform(cl);
    int       stat = esp_http_client_get_status_code(cl);
    LOG_I("OPDS perform url=%.80s rc=%s status=%d loc='%s' body=%u B fin=%d",
          req_url, esp_err_to_name(err), stat, ctx.location,
          (unsigned) body.size(), (int) ctx.finished);

    // If perform() failed and no data arrived at all, the TLS session was
    // probably stale (close_notify on a reused connection before any response
    // headers were sent).  Retry up to 2 more times with fresh connections.
    for (int attempt = 0; attempt < 2 && (err != ESP_OK && stat <= 0 && body.empty() && !ctx.location[0]); ++attempt) {
      LOG_I("OPDS stale-connection retry #%d for '%s'", attempt + 1, req_url);
      esp_http_client_cleanup(cl);
      cl = nullptr;

      ctx.location[0] = '\0';
      ctx.finished    = false;
      // Brief pause so the TCP stack can finish tearing down the previous
      // connection — avoids the close_notify from the old session being read
      // at the start of the new TLS handshake.
      vTaskDelay(pdMS_TO_TICKS(300));

      cl = esp_http_client_init(&cfg);
      if (!cl) { LOG_E("OPDS retry init NULL"); return -2; }
      if (user && user[0]) set_auth_header(cl, user, pwd);
      esp_http_client_set_header(cl, "Accept",
        "application/atom+xml,application/xml,text/xml");
      err  = esp_http_client_perform(cl);
      stat = esp_http_client_get_status_code(cl);
      LOG_I("OPDS retry#%d rc=%s status=%d body=%u B fin=%d",
            attempt + 1, esp_err_to_name(err), stat,
            (unsigned) body.size(), (int) ctx.finished);
    }

    // If perform() returned an error but we already received body data via
    // ON_DATA events, the full response was delivered and the TLS close_notify
    // arrived just as we were finishing — treat as a successful 200.
    if (err != ESP_OK && stat <= 0 && !body.empty()) {
      LOG_I("OPDS perform error ignored: body received, treating as HTTP 200");
      stat = 200;
    }

    if (ctx.location[0]) redirect_out = ctx.location;

    esp_http_client_cleanup(cl);
    return stat;
  };

  std::string redirect_url;
  int http_status = do_one_request(url, redirect_url);

  // Follow one redirect: covers clean 3xx AND the case where close_notify
  // caused status=-1 but Location was already captured by ON_HEADER event.
  bool need_redirect = (http_status >= 300 && http_status < 400)
                    || (http_status <= 0 && !redirect_url.empty());
  if (need_redirect && !redirect_url.empty()) {
    LOG_I("OPDS following redirect -> %s", redirect_url.c_str());
    std::string unused;
    http_status = do_one_request(redirect_url.c_str(), unused);
  }

  LOG_I("OPDS done. heap=%u B", (unsigned) esp_get_free_heap_size());

  if (http_status != 200) {
    LOG_E("OPDS final HTTP status %d", http_status);
    if (http_status <= 0) {
      snprintf(error_msg, error_size,
               "Connection error (TLS/network). Check URL and network.");
    } else {
      snprintf(error_msg, error_size, "HTTP %d", http_status);
    }
    return false;
  }

  if (body.empty()) {
    LOG_E("OPDS empty body");
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

    // Publication year — try dc:date and dcterms:issued (first 4 chars)
    {
      const char * date_str = nullptr;
      if (entry.child("dc:date"))         date_str = entry.child_value("dc:date");
      else if (entry.child("dcterms:issued")) date_str = entry.child_value("dcterms:issued");
      if (date_str && date_str[0] >= '1' && date_str[0] <= '2' &&
          date_str[1] >= '0' && date_str[1] <= '9' &&
          date_str[2] >= '0' && date_str[2] <= '9' &&
          date_str[3] >= '0' && date_str[3] <= '9') {
        e.year = std::string(date_str, 4);
      }
    }

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

  // bytes < 0 can mean a genuine read error OR a TLS close_notify that the
  // underlying mbedTLS layer surfaces as an error even after all data has
  // been delivered.  Treat as success when the amount written matches the
  // announced Content-Length, or (when unknown) when at least some bytes
  // were received.
  if (bytes < 0) {
    bool complete = (content_length > 0)
                  ? (bytes_done >= content_length)
                  : (bytes_done > 0);
    if (!complete) {
      remove(filepath);
      snprintf(error_msg, error_size, "Read error (got %lld of %lld B)",
               (long long) bytes_done, (long long) content_length);
      return false;
    }
  }

  return true;

#else
  (void)url; (void)user; (void)pwd; (void)filepath; (void)progress_cb;
  snprintf(error_msg, error_size, "Not supported on Linux build");
  return false;
#endif
}
