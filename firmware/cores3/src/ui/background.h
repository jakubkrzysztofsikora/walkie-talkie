#pragma once
/**
 * Parallax background layers.
 *
 * Drawn into the back-buffer before the mascot and controls.
 * Stars, hills and ground move at different speeds for depth.
 */

#include <Arduino.h>
#include <M5GFX.h>
#include <cstdint>

namespace walkie_ui {

struct BackgroundState {
    uint16_t star_offset = 0;
    uint16_t hill_offset = 0;
    uint16_t ground_offset = 0;
    uint32_t frame = 0;
};

struct BackgroundConfig {
    uint16_t bg_color;
    uint16_t star_color;
    uint16_t hill_color;
    uint16_t ground_color;
};

BackgroundConfig default_background_config(uint16_t bg_color, uint16_t accent);

// Render the full background into the sprite. Advances internal offsets.
void render_background(LGFX_Sprite* sprite, BackgroundState& state, const BackgroundConfig& cfg);

} // namespace walkie_ui
