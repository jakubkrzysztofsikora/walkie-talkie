#include "background.h"
#include <cmath>

namespace walkie_ui {

BackgroundConfig default_background_config(uint16_t bg_color, uint16_t accent) {
    return BackgroundConfig{
        .bg_color = bg_color,
        .star_color = static_cast<uint16_t>((accent & 0xF7DE) >> 2),
        .hill_color = static_cast<uint16_t>((bg_color & 0xF7DE) >> 1),
        .ground_color = static_cast<uint16_t>(((bg_color & 0xF7DE) >> 1) | 0x0841),
    };
}

void render_background(LGFX_Sprite* sprite, BackgroundState& state, const BackgroundConfig& cfg) {
    const int16_t w = sprite->width();
    const int16_t h = sprite->height();

    sprite->fillSprite(cfg.bg_color);

    // Far layer: stars.
    for (int i = 0; i < 24; ++i) {
        int16_t x = ((i * 53) + state.star_offset) % w;
        int16_t y = 10 + ((i * 17) % 90);
        uint8_t twinkle = (state.frame + i) % 16;
        uint16_t c = (twinkle < 8) ? cfg.star_color : static_cast<uint16_t>(cfg.star_color | 0x2104);
        sprite->fillRect(x, y, 2, 2, c);
    }

    // Mid layer: rolling hills.
    const int16_t hill_y = 150;
    sprite->fillRect(0, hill_y, w, h - hill_y, cfg.hill_color);
    for (int16_t x = 0; x < w; ++x) {
        int16_t dx = (x + state.hill_offset) % 120;
        int16_t hh = (dx < 60) ? dx / 4 : (120 - dx) / 4;
        sprite->drawFastVLine(x, hill_y - hh, hh, cfg.hill_color);
    }

    // Near layer: ground blocks.
    const int16_t ground_y = 190;
    sprite->fillRect(0, ground_y, w, h - ground_y, cfg.ground_color);
    for (int16_t x = 0; x < w; x += 32) {
        // Normalise to non-negative before the modulo: (x - ground_offset) can go
        // negative (x starts at 0, offset up to 31), and C++ % on a negative LHS
        // yields a negative remainder → a glitched gx. Add (w+32) so the dividend
        // is always >= 0.
        int16_t gx = ((x - state.ground_offset + (w + 32)) % (w + 32)) - 16;
        int16_t gh = 6 + ((x / 32) % 3) * 6;
        sprite->fillRect(gx, ground_y - gh, 24, gh + 4, cfg.ground_color);
        sprite->drawRect(gx, ground_y - gh, 24, gh + 4, cfg.star_color);
    }

    state.star_offset = (state.star_offset + 1) % w;
    state.hill_offset = (state.hill_offset + 2) % 120;
    state.ground_offset = (state.ground_offset + 4) % 32;
    state.frame++;
}

} // namespace walkie_ui
