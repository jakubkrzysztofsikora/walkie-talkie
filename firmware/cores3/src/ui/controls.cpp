#include "controls.h"

namespace walkie_ui {

void draw_status_bar(lgfx::LGFX_Sprite* sprite, int16_t x, int16_t y,
                     int battery_pct, WifiIconState wifi) {
    // Battery outline and fill.
    sprite->drawRect(x, y, 18, 10, 0xFFFF);
    sprite->fillRect(x + 18, y + 2, 3, 6, 0xFFFF);
    int fill = (battery_pct * 14 + 50) / 100;
    if (fill > 14) fill = 14;
    uint16_t bc = (battery_pct < 20) ? 0xF800 : (battery_pct < 50) ? 0xFFE0 : 0x07E0;
    sprite->fillRect(x + 2, y + 2, fill, 6, bc);

    // WiFi icon to the right.
    int16_t wx = x + 26;
    if (wifi == WifiIconState::ON) {
        sprite->fillCircle(wx + 6, y + 8, 2, 0x07E0);
        sprite->drawArc(wx + 6, y + 8, 5, 7, 225, 315, 0x07E0);
        sprite->drawArc(wx + 6, y + 8, 9, 11, 225, 315, 0x07E0);
    } else if (wifi == WifiIconState::CONNECTING) {
        uint8_t f = (millis() / 250) % 3;
        for (uint8_t i = 0; i <= f; ++i) {
            sprite->drawArc(wx + 6, y + 8, 3 + i * 4, 5 + i * 4, 225, 315, 0xFFE0);
        }
    } else {
        sprite->drawLine(wx, y, wx + 12, y + 10, 0xF800);
        sprite->drawLine(wx + 12, y, wx, y + 10, 0xF800);
    }
}

void draw_ptt_control(lgfx::LGFX_Sprite* sprite, int16_t cx, int16_t cy,
                      bool pressed, uint8_t speaking_level,
                      uint16_t accent, uint16_t bg) {
    int16_t r = pressed ? 28 : 24;
    uint16_t ring_color = pressed ? 0xF800 : accent;
    uint16_t fill_color = pressed ? 0x5000 : bg;

    sprite->fillCircle(cx, cy, r + 4, ring_color);
    sprite->fillCircle(cx, cy, r, fill_color);

    // Mic body.
    int16_t mx = cx - 3;
    int16_t my = cy - 8;
    sprite->fillRoundRect(mx, my, 6, 10, 2, pressed ? 0xFFFF : ring_color);
    sprite->fillRect(mx + 1, my + 10, 4, 3, pressed ? 0xFFFF : ring_color);
    sprite->fillRect(mx + 2, my + 13, 2, 2, pressed ? 0xFFFF : ring_color);

    // Animated ring segments when speaking.
    if (speaking_level > 0 && !pressed) {
        for (uint8_t i = 0; i < speaking_level; ++i) {
            int16_t rr = r + 6 + i * 4;
            sprite->drawArc(cx, cy, rr, rr + 2, 200 + i * 40, 250 + i * 40, accent);
        }
    }
}

void draw_waveform(lgfx::LGFX_Sprite* sprite, int16_t cx, int16_t y, uint8_t frame,
                   uint16_t color) {
    for (int i = 0; i < 9; ++i) {
        int8_t h = 2 + ((frame + i * 3) % 7) * 2;
        int16_t x = cx - 40 + i * 10;
        sprite->fillRect(x, y - h, 6, h * 2, color);
    }
}

} // namespace walkie_ui
