#pragma once
/**
 * Main UI orchestrator.
 *
 * Initialise once, then call ui_engine_render() every frame.
 * All drawing goes through a single back-buffer sprite.
 */

#include <Arduino.h>
#include <M5GFX.h>
#include <cstdint>
#include <cstddef>
#include "walkie_ui_logic/sprites.h"
#include "walkie_ui_logic/animator.h"
#include "background.h"
#include "walkie_ui_logic/menu.h"

namespace walkie_ui {

struct UIEngine {
    M5GFX* display = nullptr;
    lgfx::LGFX_Sprite* back_buffer = nullptr;
    ScreenState screen = ScreenState::BOOT;
    size_t character_idx = 0;
    bool menu_open = false;
    bool ptt_pressed = false;
    uint8_t speaking_level = 0;  // 0-3, driven by audio amplitude

    // Expression chosen by the pure compute_screen() decision logic in the lib.
    // When set, render uses this directly (bypassing screen_to_mascot_state +
    // its speaking_level gate) so the agent-speaking face shows whenever TTS
    // plays — including the post-release reply that arrives in IDLE.
    Expression forced_expression = Expression::IDLE;
    bool use_forced_expression = false;

    bool back_buffer_ok = false;  // false if PSRAM createSprite failed (fault flag)

    AnimatorState animator;
    BackgroundState background;
    MenuLayout menu_layout;
};

bool ui_engine_init(UIEngine& ui, M5GFX* display);
void ui_engine_set_state(UIEngine& ui, ScreenState state);
void ui_engine_set_character(UIEngine& ui, size_t idx);
void ui_engine_set_menu_open(UIEngine& ui, bool open);
void ui_engine_set_ptt(UIEngine& ui, bool pressed);
void ui_engine_set_speaking_level(UIEngine& ui, uint8_t level);

// Drive the mascot expression directly from the pure compute_screen() result.
void ui_engine_set_expression(UIEngine& ui, Expression expr);

// Call at ~10 Hz. Renders the full frame and pushes it to the display.
void ui_engine_render(UIEngine& ui, int battery_pct, bool wifi_connected);

} // namespace walkie_ui
