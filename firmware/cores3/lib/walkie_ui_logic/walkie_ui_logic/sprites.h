#pragma once
/**
 * Per-agent pixel-art sprites.
 *
 * Each character has 4 expression frames stored as indexed bitmaps against a
 * shared semantic palette (accent, secondary, skin, white, black, etc.).
 *
 * Base art is 16x16 px and rendered at 4x scale (64x64 on screen).
 */

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdint>
#include <cstddef>
#endif

namespace walkie_ui {

// Single source of truth for the character roster (main.cpp uses this list).
static constexpr size_t NCHARS = 11;
static constexpr size_t SPRITE_W = 16;
static constexpr size_t SPRITE_H = 16;
static constexpr size_t SPRITE_BYTES = SPRITE_W * SPRITE_H;

enum class Expression : uint8_t {
    IDLE = 0,
    LISTEN,
    THINK,
    SPEAK,
    EXPRESSION_COUNT
};

// Semantic palette indices shared by all characters.
enum class SpriteColor : uint8_t {
    TRANSPARENT = 0,
    BLACK,
    WHITE,
    SKIN,
    ACCENT,
    ACCENT_DARK,
    SECONDARY,
    SECONDARY_DARK,
    BACKGROUND,
    GLOW,
    PALETTE_SIZE
};

struct CharacterTheme {
    const char* id;
    uint16_t accent;
    uint16_t accent_dark;
    uint16_t secondary;
    uint16_t secondary_dark;
    uint16_t background;
    uint16_t glow;
};

struct CharacterSprites {
    const char* id;
    const uint8_t* frames[static_cast<size_t>(Expression::EXPRESSION_COUNT)]; // 16x16 indexed
};

// Public API
const CharacterTheme* get_character_theme(size_t idx);
const CharacterSprites* get_character_sprites(size_t idx);
uint16_t resolve_palette_color(SpriteColor idx, const CharacterTheme& theme);
size_t character_index_by_id(const char* id);

} // namespace walkie_ui
