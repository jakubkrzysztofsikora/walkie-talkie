#include "ui_engine.h"
#include "controls.h"
#include "pixel_anim.h"
#include <cstring>

// Extern: real-time mic peak from the audio task (0-255).
// Used to drive the VU meter during PTT so the user sees their voice is heard.
extern uint8_t audio_task_mic_peak();

namespace walkie_ui {

static AnimatorConfig s_anim_cfg = default_animator_config();

bool ui_engine_init(UIEngine& ui, M5GFX* display) {
    ui.display = display;
    if (!ui.back_buffer) {
        ui.back_buffer = new LGFX_Sprite(display);
    }
    ui.back_buffer->setColorDepth(16);
    // The 320x240x16bpp back-buffer is ~150 KB. Force it into PSRAM so it does
    // NOT eat scarce internal SRAM (which would risk OOM / a black screen on a
    // connected device). createSprite returns the buffer pointer (nullptr on
    // failure); capture + null-check it and set a fault flag instead of crashing.
    ui.back_buffer->setPsram(true);
    // LGFX silently falls back to INTERNAL SRAM if PSRAM can't honour the alloc —
    // createSprite still returns non-null, so a null-check alone is not enough.
    // A 320x240x16bpp sprite is ~150 KB; measure the PSRAM delta and reject the
    // sprite if it did NOT actually land in PSRAM (delta < ~140 KB), otherwise it
    // would consume the internal SRAM that WiFi/Opus need.
    const size_t psram_before = ESP.getFreePsram();
    void* buf = ui.back_buffer->createSprite(320, 240);
    const size_t psram_after = ESP.getFreePsram();
    const size_t psram_delta = (psram_before > psram_after) ? (psram_before - psram_after) : 0;
    if (buf == nullptr) {
        ui.back_buffer_ok = false;
        Serial.println("[ui] FATAL back-buffer createSprite(320,240) FAILED — "
                       "UI engine disabled, falling back");
        ui.menu_layout = compute_menu_layout(320, 240);
        return false;
    }
    if (psram_delta < 140000) {
        // Sprite landed in internal SRAM (or PSRAM accounting says it didn't take
        // the expected ~150 KB) — refuse it so we don't starve WiFi/Opus.
        Serial.printf("[ui] FATAL back-buffer not in PSRAM (delta=%u, expected >=140000) "
                      "— UI engine disabled, falling back\n", (unsigned)psram_delta);
        ui.back_buffer->deleteSprite();
        ui.back_buffer_ok = false;
        ui.menu_layout = compute_menu_layout(320, 240);
        return false;
    }
    Serial.printf("[ui] back-buffer in PSRAM ok (delta=%u)\n", (unsigned)psram_delta);
    ui.back_buffer_ok = true;
    // The generated sprite arrays (sprites.cpp) are CPU-native little-endian
    // RGB565. With the LGFX default _swapBytes=false, pushImage<uint16_t> would
    // treat the buffer as byte-swapped and transpose every pixel's bytes,
    // garbling all sprite colours. setSwapBytes(true) makes pushImage treat the
    // arrays as nonswapped (CPU-native) RGB565 so they blit correctly. This only
    // affects raw uint16 pushImage/pushPixels blits; chrome draws (fillRect,
    // drawCircle, drawPixel, text) pass a uint16_t colour through
    // _write_conv.convert() and are unaffected by this flag.
    ui.back_buffer->setSwapBytes(true);
    ui.back_buffer->fillSprite(0x0000);
    ui.menu_layout = compute_menu_layout(320, 240);
    return true;
}

void ui_engine_set_state(UIEngine& ui, ScreenState state) {
    ui.screen = state;
}

void ui_engine_set_character(UIEngine& ui, size_t idx) {
    ui.character_idx = (idx < NCHARS) ? idx : 0;
}

void ui_engine_set_menu_open(UIEngine& ui, bool open) {
    ui.menu_open = open;
}

void ui_engine_set_ptt(UIEngine& ui, bool pressed) {
    ui.ptt_pressed = pressed;
}

void ui_engine_set_speaking_level(UIEngine& ui, uint8_t level) {
    ui.speaking_level = (level > 3) ? 3 : level;
}

void ui_engine_set_overlay_suppressed(UIEngine& ui, bool suppressed) {
    ui.overlay_suppressed = suppressed;
}

void ui_engine_set_expression(UIEngine& ui, Expression expr) {
    ui.forced_expression = expr;
    ui.use_forced_expression = true;
}

// Draw a 64x64 RGB565 sprite at native scale, centred on (cx,cy), honouring the
// 1bpp alpha mask. Pushes contiguous opaque horizontal runs through the real
// LovyanGFX pushImage(x,y,w,h,const T*) blit (LGFXBase.hpp:407) — far cheaper
// than 4096 per-pixel fillRects, and the mask gives clean edges (no colour-key
// fringing, no palette lookup).
static void draw_sprite_native(lgfx::LGFX_Sprite* sprite, int16_t cx, int16_t cy,
                               const uint16_t* data, const uint8_t* mask,
                               uint8_t scale = 1) {
    if (scale < 1) scale = 1;
    const int16_t x0 = cx - (SPRITE_W * scale) / 2;
    const int16_t y0 = cy - (SPRITE_H * scale) / 2;
    // Expand each opaque run into a scaled row, then push it `scale` times tall.
    static uint16_t srow[SPRITE_W * 4];   // max scale 4 (256px) — bounds the buffer
    if (scale > 4) scale = 4;
    for (size_t y = 0; y < SPRITE_H; ++y) {
        const uint16_t* row = &data[y * SPRITE_W];
        size_t x = 0;
        while (x < SPRITE_W) {
            if (!sprite_mask_opaque(mask, x, y)) { ++x; continue; }
            const size_t run_start = x;
            while (x < SPRITE_W && sprite_mask_opaque(mask, x, y)) ++x;
            const size_t run_len = x - run_start;
            if (scale == 1) {
                sprite->pushImage(x0 + (int16_t)run_start, y0 + (int16_t)y,
                                  (int16_t)run_len, 1, &row[run_start]);
                continue;
            }
            size_t k = 0;
            for (size_t i = 0; i < run_len; ++i)
                for (uint8_t s = 0; s < scale; ++s) srow[k++] = row[run_start + i];
            const int16_t dx = x0 + (int16_t)(run_start * scale);
            const int16_t dy = y0 + (int16_t)(y * scale);
            for (uint8_t s = 0; s < scale; ++s)
                sprite->pushImage(dx, dy + s, (int16_t)k, 1, srow);
        }
    }
}

// Draw a 64x64 RGB565 sprite shrunk to a square `dst` px badge (nearest-
// neighbour), honouring the alpha mask. Used for the menu hero badges.
static void draw_sprite_badge(lgfx::LGFX_Sprite* sprite, int16_t cx, int16_t cy,
                              const uint16_t* data, const uint8_t* mask,
                              int16_t dst) {
    const int16_t x0 = cx - dst / 2;
    const int16_t y0 = cy - dst / 2;
    for (int16_t dy = 0; dy < dst; ++dy) {
        const size_t sy = (size_t)dy * SPRITE_H / dst;
        for (int16_t dx = 0; dx < dst; ++dx) {
            const size_t sx = (size_t)dx * SPRITE_W / dst;
            if (!sprite_mask_opaque(mask, sx, sy)) continue;
            sprite->drawPixel(x0 + dx, y0 + dy, data[sy * SPRITE_W + sx]);
        }
    }
}

static void render_main_screen(UIEngine& ui, int battery_pct, bool wifi_connected) {
    const CharacterTheme* theme = get_character_theme(ui.character_idx);
    if (!theme) return;

    BackgroundConfig bg_cfg = default_background_config(theme->background, theme->accent);
    render_background(ui.back_buffer, ui.background, bg_cfg);

    WifiIconState wifi = wifi_connected ? WifiIconState::ON : WifiIconState::OFF;
    if (ui.screen == ScreenState::WIFI_CONNECT || ui.screen == ScreenState::WSS_CONNECT) {
        wifi = WifiIconState::CONNECTING;
    }
    draw_status_bar(ui.back_buffer, 6, 6, battery_pct, wifi);

    // Mascot centre with bounce. Always tick the animator to advance blink /
    // bounce / mouth timing, but when main.cpp has supplied an expression via the
    // pure compute_screen() logic, render that directly (bypassing the
    // speaking_level gate in screen_to_mascot_state).
    MascotState desired = screen_to_mascot_state(ui.screen, ui.ptt_pressed, ui.speaking_level);
    Expression expr = animator_tick(ui.animator, desired, s_anim_cfg);
    if (ui.use_forced_expression) {
        expr = ui.forced_expression;
        // Bring the forced state to LIFE rather than freezing one frame:
        //  - SPEAK (agent talking): flap the mouth by alternating SPEAK<->LISTEN
        //    every few ticks so it looks like it's actually talking.
        //  - otherwise: every ~2.5s blink to a different expression briefly so
        //    idle/listen don't sit dead-still.
        const uint32_t tk = ui.animator.tick;
        if (expr == Expression::SPEAK) {
            expr = (tk % 4 < 2) ? Expression::SPEAK : Expression::LISTEN;
        } else if ((tk % 25) < 2) {
            expr = Expression::THINK;          // quick "alive" beat
        }
    }
    const CharacterSprites* sprites = get_character_sprites(ui.character_idx);
    if (!sprites) return;  // theme is checked above; sprites must be too (null-deref guard)
    const size_t expr_idx = static_cast<size_t>(expr);
    const uint16_t* frame_data = sprites->frames[expr_idx];
    const uint8_t* frame_mask = sprites->masks[expr_idx];

    int16_t mx = 160;
    int16_t my = 95 + bounce_offset(ui.animator);

    // Glow ring when active (sized for the 2x = 128px mascot).
    if (ui.screen == ScreenState::SESSION_ACTIVE) {
        ui.back_buffer->drawCircle(mx, my, 70, theme->glow);
        ui.back_buffer->drawCircle(mx, my, 69, theme->glow);
    } else if (ui.screen == ScreenState::IDLE && (ui.animator.tick % 12) < 6) {
        ui.back_buffer->drawCircle(mx, my, 70, theme->glow);
    }

    // 2x scale: 64x64 art -> 128x128 on screen (was tiny at native size).
    draw_sprite_native(ui.back_buffer, mx, my, frame_data, frame_mask, /*scale=*/2);

    // Character carousel dots.
    int16_t cy = 175;
    for (int i = -1; i <= 1; ++i) {
        size_t idx = (ui.character_idx + i + NCHARS) % NCHARS;
        int16_t r = (i == 0) ? 8 : 5;
        uint16_t c = (i == 0) ? theme->accent : 0x6B4D;
        ui.back_buffer->fillCircle(mx + i * 28, cy, r, c);
        if (i == 0) {
            ui.back_buffer->fillCircle(mx, cy, r - 2, theme->background);
        }
    }
    ui.back_buffer->fillTriangle(mx - 18, cy, mx - 10, cy - 4, mx - 10, cy + 4, 0x6B4D);
    ui.back_buffer->fillTriangle(mx + 18, cy, mx + 10, cy - 4, mx + 10, cy + 4, 0x6B4D);

    // PTT / VU meter at bottom. When the user is actively talking (PTT held),
    // show the real mic level so they can see their voice is heard. Otherwise
    // render the standard PTT control with the agent-speaking ring.
    int16_t by = 215;
    if (ui.screen == ScreenState::SESSION_ACTIVE) {
        uint8_t mic_peak = audio_task_mic_peak();
        // 5-bar VU meter to the left of the PTT button.
        int bars = (mic_peak * 5 + 127) / 255;
        if (bars > 5) bars = 5;
        for (int i = 0; i < 5; ++i) {
            uint16_t col = (i < bars) ? theme->accent : 0x6B4D;
            int16_t bx = 90 + i * 9;
            int16_t bh = 6 + i * 4;
            ui.back_buffer->fillRect(bx, by - bh - 2, 7, bh, col);
        }
    }
    draw_ptt_control(ui.back_buffer, mx, by, ui.ptt_pressed, ui.speaking_level,
                     theme->accent, theme->background);
}

static void render_menu(UIEngine& ui) {
    ui.back_buffer->fillSprite(0x0000);
    const CharacterTheme* cur_theme = get_character_theme(ui.character_idx);
    uint16_t banner = cur_theme ? cur_theme->accent : 0x07FF;
    ui.back_buffer->fillRect(0, 0, 320, 60, banner);
    ui.back_buffer->setTextSize(2);
    ui.back_buffer->setTextColor(0xFFFF, banner);
    ui.back_buffer->drawCenterString("PICK HERO", 160, 22);

    for (size_t i = 0; i < NCHARS; ++i) {
        const CharacterTheme* t = get_character_theme(i);
        if (!t) continue;
        int16_t x = ui.menu_layout.centers[i][0];
        int16_t y = ui.menu_layout.centers[i][1];
        bool selected = (i == ui.character_idx);

        if (selected) {
            ui.back_buffer->fillCircle(x, y, MENU_BADGE_RADIUS + 4, 0xFFFF);
        }
        ui.back_buffer->fillCircle(x, y, MENU_BADGE_RADIUS, t->accent);

        const CharacterSprites* spr = get_character_sprites(i);
        if (!spr) continue;  // null-deref guard (mirrors the theme guard above)
        const size_t idle = static_cast<size_t>(Expression::IDLE);
        draw_sprite_badge(ui.back_buffer, x, y,
                          spr->frames[idle], spr->masks[idle],
                          MENU_BADGE_RADIUS + 14);  // ~40px inside the 52px badge
    }
}

void ui_engine_render(UIEngine& ui, int battery_pct, bool wifi_connected) {
    if (!ui.back_buffer || !ui.back_buffer_ok) return;

    if (ui.menu_open) {
        render_menu(ui);
    } else {
        render_main_screen(ui, battery_pct, wifi_connected);
        // Composite the PXA1 pixel animation (if active) ON TOP of the mascot
        // scene, into the same back-buffer, BEFORE the push. Advances the frame
        // on its own frame_ms cadence and auto-dismisses when it expires. Gated:
        //  - only during an active conversation (SESSION_ACTIVE) — the overlay is
        //    a session feature, not chrome for IDLE/connect screens; this also
        //    bounds the extra blit cost to when it's intended.
        //  - NOT while overlay_suppressed (set by main.cpp during AUDIO_TALK), so
        //    the masked-run blit never steals time from the mic uplink during
        //    capture (protects out_drop).
        // Draw whenever an animation is loaded and we're NOT actively capturing
        // (overlay_suppressed == AUDIO_TALK). The animation usually arrives while
        // the user holds PTT and plays out AFTER they release (state -> IDLE),
        // exactly like the TTS reply — so gating on SESSION_ACTIVE hid it. The
        // pixel_anim_active() check inside bounds it to the play window.
        if (!ui.overlay_suppressed) {
            pixel_anim_tick_and_draw(ui);
        }
    }

    ui.back_buffer->pushSprite(0, 0);
}

} // namespace walkie_ui
