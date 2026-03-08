#define __SCREEN__ 1
#include "screen.hpp"

#if defined(BOARD_TYPE_PAPER_S3)

extern "C" {
  #include <epdiy.h>
  #include <epd_highlevel.h>
  #include <epd_display.h>
}

extern "C" {
  extern const EpdBoardDefinition paper_s3_board;
}

#ifndef EPD_WIDTH
#define EPD_WIDTH 960
#endif

#ifndef EPD_HEIGHT
#define EPD_HEIGHT 540
#endif

static EpdiyHighlevelState s_hl;
static bool s_epd_initialized = false;
static uint8_t *s_framebuffer = nullptr;
static bool s_force_full = true;
static int16_t s_partial_count = 0;
static const int16_t PARTIAL_COUNT_ALLOWED = 10;
static int s_temperature = 20; 

Screen Screen::singleton;

uint16_t Screen::width  = EPD_WIDTH;
uint16_t Screen::height = EPD_HEIGHT;

void Screen::clear()
{
  if (!s_epd_initialized) return;
  epd_hl_set_all_white(&s_hl);
}

/**
 * Updated Update Logic:
 * @param full If true, forces a high-quality GC16 flash. 
 * If false, attempts a fast GL16 partial refresh.
 */
void Screen::update(bool full)
{
  if (!s_epd_initialized) return;

  // Force a full refresh if requested, forced, or if we've done too many partials
  if (s_force_full || full || s_partial_count <= 0) {
    epd_hl_update_screen(&s_hl, MODE_GC16, s_temperature);
    s_force_full = false;
    s_partial_count = PARTIAL_COUNT_ALLOWED;
  } else {
    // Fast partial update
    epd_hl_update_screen(&s_hl, MODE_GL16, s_temperature);
    s_partial_count--;
  }
}

void Screen::force_full_update()
{
  s_force_full = true;
}

void Screen::setup(PixelResolution resolution, Orientation orientation)
{
  if (!s_epd_initialized) {
    epd_set_board(&paper_s3_board);
    epd_init(epd_current_board(), &ED047TC2, EPD_OPTIONS_DEFAULT);
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    epd_set_lcd_pixel_clock_MHz(5);

    s_hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    epd_hl_set_all_white(&s_hl);
    s_framebuffer = epd_hl_get_framebuffer(&s_hl);

    epd_poweron();
    epd_fullclear(&s_hl, s_temperature);
    s_epd_initialized = true;
    s_force_full = false;
    s_partial_count = PARTIAL_COUNT_ALLOWED;
  }

  (void)resolution;
  set_pixel_resolution(PixelResolution::THREE_BITS, true);
  set_orientation(orientation);
  clear();
}

void Screen::set_pixel_resolution(PixelResolution resolution, bool force)
{
  if (force || (pixel_resolution != resolution)) {
    pixel_resolution = resolution;
  }
}

void Screen::set_orientation(Orientation orient)
{
  orientation = orient;
  width  = EPD_HEIGHT;  // 540
  height = EPD_WIDTH;   // 960
}

static inline uint8_t gray8_to_nibble(uint8_t v)
{
  return (uint8_t)(v >> 4);
}

static inline uint8_t alpha8_to_nibble(uint8_t a)
{
  return (uint8_t)(15 - (a >> 4));
}

static inline uint8_t gray3_to_nibble(uint8_t v)
{
  return (uint8_t)((v * 15 + 3) / 7);
}

static inline void set_pixel_nibble_physical(uint16_t x, uint16_t y, uint8_t nibble)
{
  uint8_t * buf_ptr = &s_framebuffer[y * (EPD_WIDTH / 2) + (x >> 1)];
  if (x & 1) {
    *buf_ptr = (uint8_t)((*buf_ptr & 0x0F) | ((nibble & 0x0F) << 4));
  } else {
    *buf_ptr = (uint8_t)((*buf_ptr & 0xF0) | (nibble & 0x0F));
  }
}

static inline void set_pixel_nibble_screen(uint16_t x, uint16_t y, uint8_t nibble)
{
  set_pixel_nibble_physical(y, (uint16_t)((EPD_HEIGHT - 1) - x), nibble);
}

void Screen::draw_bitmap(const unsigned char * bitmap_data, Dim dim, Pos pos)
{
  if (!s_epd_initialized || (bitmap_data == nullptr)) return;
  uint16_t x_max = pos.x + dim.width;
  uint16_t y_max = pos.y + dim.height;
  if (x_max > width)  x_max = width;
  if (y_max > height) y_max = height;

  for (uint16_t x = pos.x; x < x_max; ++x) {
    for (uint16_t y = pos.y; y < y_max; ++y) {
      const uint32_t p = (uint32_t)(y - pos.y) * dim.width + (x - pos.x);
      set_pixel_nibble_screen(x, y, gray8_to_nibble(bitmap_data[p]));
    }
  }
}

void Screen::draw_glyph(const unsigned char * bitmap_data, Dim dim, Pos pos, uint16_t pitch)
{
  if (!s_epd_initialized || (bitmap_data == nullptr)) return;
  uint16_t x_max = pos.x + dim.width;
  uint16_t y_max = pos.y + dim.height;
  if (x_max > width)  x_max = width;
  if (y_max > height) y_max = height;

  for (uint16_t i = 0; i < dim.width && (pos.x + i) < x_max; ++i) {
    const uint16_t x = (uint16_t)(pos.x + i);
    for (uint16_t j = 0; j < dim.height && (pos.y + j) < y_max; ++j) {
      const uint16_t y = (uint16_t)(pos.y + j);
      const uint8_t a = bitmap_data[j * pitch + i];
      if (!a) continue;
      const uint8_t nib = alpha8_to_nibble(a);
      if (nib == 0x0F) continue;
      set_pixel_nibble_screen(x, y, nib);
    }
  }
}

void Screen::draw_rectangle(Dim dim, Pos pos, uint8_t color)
{
  if (!s_epd_initialized) return;
  uint16_t x_max = pos.x + dim.width;
  uint16_t y_max = pos.y + dim.height;
  if (x_max > width)  x_max = width;
  if (y_max > height) y_max = height;
  const uint8_t nib = gray3_to_nibble(color);
  for (uint16_t x = pos.x; x < x_max; ++x) {
    set_pixel_nibble_screen(x, pos.y, nib);
    set_pixel_nibble_screen(x, (uint16_t)(y_max - 1), nib);
  }
  for (uint16_t y = pos.y; y < y_max; ++y) {
    set_pixel_nibble_screen(pos.x, y, nib);
    set_pixel_nibble_screen((uint16_t)(x_max - 1), y, nib);
  }
}

void Screen::draw_round_rectangle(Dim dim, Pos pos, uint8_t color)
{
  draw_rectangle(dim, pos, color);
}

void Screen::colorize_region(Dim dim, Pos pos, uint8_t color)
{
  if (!s_epd_initialized) return;
  uint16_t x_max = pos.x + dim.width;
  uint16_t y_max = pos.y + dim.height;
  if (x_max > width)  x_max = width;
  if (y_max > height) y_max = height;
  const uint8_t nib = gray3_to_nibble(color);
  for (uint16_t x = pos.x; x < x_max; ++x) {
    for (uint16_t y = pos.y; y < y_max; ++y) {
      set_pixel_nibble_screen(x, y, nib);
    }
  }
}
#endif