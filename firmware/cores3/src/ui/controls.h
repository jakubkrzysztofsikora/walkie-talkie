#pragma once
/**
 * Status icons and PTT control visuals.
 *
 * Battery, WiFi, microphone/PTT ring.
 */

#include <Arduino.h>
#include <M5GFX.h>
#include <cstdint>

namespace walkie_ui {

enum class WifiIconState : uint8_t {
    OFF,
    CONNECTING,
    ON
};

// Draw top-left status cluster: battery + wifi.
void draw_status_bar(lgfx::LGFX_Sprite* sprite, int16_t x, int16_t y,
                     int battery_pct, WifiIconState wifi);

// Draw PTT button at the bottom centre. `pressed` and `speaking_level` (0-3).
void draw_ptt_control(lgfx::LGFX_Sprite* sprite, int16_t cx, int16_t cy,
                      bool pressed, uint8_t speaking_level,
                      uint16_t accent, uint16_t bg);

// Draw audio waveform while speaking/listening.
void draw_waveform(lgfx::LGFX_Sprite* sprite, int16_t cx, int16_t y, uint8_t frame,
                   uint16_t color);

} // namespace walkie_ui
