#include "ui_engine.h"
#include "controls.h"
#include <cstring>

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

void ui_engine_set_expression(UIEngine& ui, Expression expr) {
    ui.forced_expression = expr;
    ui.use_forced_expression = true;
}

static void draw_sprite_scaled(lgfx::LGFX_Sprite* sprite, int16_t cx, int16_t cy,
                               const uint8_t* data, const CharacterTheme& theme,
                               uint8_t scale) {
    const int16_t w = 16 * scale;
    const int16_t h = 16 * scale;
    const int16_t x0 = cx - w / 2;
    const int16_t y0 = cy - h / 2;

    for (uint8_t y = 0; y < 16; ++y) {
        for (uint8_t x = 0; x < 16; ++x) {
            uint8_t idx = data[y * 16 + x];
            if (idx == 0) continue;  // transparent
            uint16_t c = resolve_palette_color(static_cast<SpriteColor>(idx), theme);
            sprite->fillRect(x0 + x * scale, y0 + y * scale, scale, scale, c);
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
    }
    const CharacterSprites* sprites = get_character_sprites(ui.character_idx);
    if (!sprites) return;  // theme is checked above; sprites must be too (null-deref guard)
    const uint8_t* frame_data = sprites->frames[static_cast<size_t>(expr)];

    int16_t mx = 160;
    int16_t my = 95 + bounce_offset(ui.animator);

    // Glow ring when active.
    if (ui.screen == ScreenState::SESSION_ACTIVE) {
        ui.back_buffer->drawCircle(mx, my, 58, theme->glow);
        ui.back_buffer->drawCircle(mx, my, 57, theme->glow);
    } else if (ui.screen == ScreenState::IDLE && (ui.animator.tick % 12) < 6) {
        ui.back_buffer->drawCircle(mx, my, 58, theme->glow);
    }

    draw_sprite_scaled(ui.back_buffer, mx, my, frame_data, *theme, 4);

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

    // PTT / waveform at bottom.
    int16_t by = 215;
    if (ui.screen == ScreenState::SESSION_ACTIVE) {
        draw_waveform(ui.back_buffer, mx, by - 12, ui.animator.mouth_frame, theme->accent);
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
        draw_sprite_scaled(ui.back_buffer, x, y,
                           spr->frames[static_cast<size_t>(Expression::IDLE)],
                           *t, 2);
    }
}

void ui_engine_render(UIEngine& ui, int battery_pct, bool wifi_connected) {
    if (!ui.back_buffer || !ui.back_buffer_ok) return;

    if (ui.menu_open) {
        render_menu(ui);
    } else {
        render_main_screen(ui, battery_pct, wifi_connected);
    }

    ui.back_buffer->pushSprite(0, 0);
}

} // namespace walkie_ui
