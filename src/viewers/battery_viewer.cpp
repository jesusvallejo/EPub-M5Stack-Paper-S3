#define __BATTERY_VIEWER__ 1
#include "viewers/battery_viewer.hpp"
#include "viewers/page.hpp"
#include "viewers/screen_bottom.hpp"
#include "models/config.hpp"
#include "battery.hpp"
#include "screen.hpp"
#include "logging.hpp"
#include <cstring>

extern Battery battery; 

void BatteryViewer::show() {
    int8_t view_mode;
    config.get(Config::Ident::BATTERY, &view_mode);
    if (view_mode == 0) return;

    float voltage = battery.read_level();
    bool usb = battery.is_usb_connected();
    bool charging = battery.is_charging();

    Page::Format fmt = {
        .line_height_factor = 1.0, .font_index = 0, .font_size = 9,
        .indent = 0, .margin_left = 0, .margin_right = 0, .margin_top = 0, .margin_bottom = 0,
        .screen_left = 10, .screen_right = 10, .screen_top = 10, .screen_bottom = 10,
        .width = 0, .height = 0, .vertical_align = 0, .trim = true, .pre = false,
        .font_style = Fonts::FaceStyle::NORMAL, .align = CSS::Align::LEFT,
        .text_transform = CSS::TextTransform::NONE, .display = CSS::Display::INLINE
    };

    Font * font = fonts.get(0);
    if (!font) return;

    float level = ((voltage - 3.3) * 4.0) / 0.85; 
    int16_t icon_index = (int16_t)level;
    if (icon_index > 4) icon_index = 4;
    if (icon_index < 0) icon_index = 0;

    if (usb) icon_index = 5; // 'R' icon

    static constexpr char icons[6] = { '0', '1', '2', '3', '4', 'R' };
    Font::Glyph * glyph = font->get_glyph(icons[icon_index], 9);

    Pos pos;
    pos.x = 5;
    pos.y = Screen::get_height() + font->get_descender_height(9) - 2;

    fmt.font_index = 0;  
    page.put_char_at(icons[icon_index], pos, fmt);

    if (view_mode == 1 || view_mode == 2) {
        char str[32];
        if (view_mode == 1) {
            int percentage = (int)((voltage - 3.3) * 100.0 / 0.85);
            if (percentage > 100) percentage = 100;
            if (percentage < 0) percentage = 0;
            snprintf(str, sizeof(str), "%d%%%s", percentage, usb ? (charging ? " (CHG)" : " (FULL)") : "");
        } else {
            snprintf(str, sizeof(str), "%5.2fv", voltage);
        }

        fmt.font_index = 1;  
        pos.x = 5 + (glyph != nullptr ? glyph->advance : 10) + 5;
        page.put_str_at(str, pos, fmt);
    }
}

void BatteryViewer::update() {
    Font * font = fonts.get(ScreenBottom::FONT);
    if (!font) return;

    Page::Format fmt = {
        .line_height_factor = 1.0, .font_index = ScreenBottom::FONT,
        .font_size = ScreenBottom::FONT_SIZE,
        .indent = 0, .margin_left = 0, .margin_right = 0, .margin_top = 0, .margin_bottom = 0,
        .screen_left = 10, .screen_right = 10, .screen_top = 10, .screen_bottom = 10,
        .width = 0, .height = 0, .vertical_align = 0, .trim = true, .pre = false,
        .font_style = Fonts::FaceStyle::NORMAL, .align = CSS::Align::LEFT,
        .text_transform = CSS::TextTransform::NONE, .display = CSS::Display::INLINE
    };

    page.start(fmt);

    uint16_t strip_h = font->get_chars_height(ScreenBottom::FONT_SIZE) + 10;
    page.clear_region(Dim(180, strip_h), Pos(0, Screen::get_height() - strip_h));

    show();

    page.paint(false, true, true);
}