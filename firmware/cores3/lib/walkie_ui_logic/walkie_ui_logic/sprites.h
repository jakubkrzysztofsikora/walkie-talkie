#pragma once
/**
 * Per-agent character sprites.
 *
 * Each character has 4 expression frames stored as full-colour 64x64 RGB565
 * bitmaps with a packed 1-bit-per-pixel alpha mask (bit = 1 means opaque).
 * The art is authored as 64x64 RGBA PNGs in assets/sprites/ and compiled to C
 * by scripts/generate_sprites_png.py (sprites.cpp is generated — do not edit it
 * or these arrays by hand).
 *
 * Frames are drawn at native 64x64 via a masked RGB565 blit (no palette lookup,
 * no per-pixel colour indirection). The earlier 16x16 indexed-palette model and
 * its resolve_palette_color() have been removed.
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
static constexpr size_t SPRITE_W = 64;
static constexpr size_t SPRITE_H = 64;
// Pixel count (uint16_t each) and packed 1bpp alpha-mask size in bytes.
static constexpr size_t SPRITE_PIXELS = SPRITE_W * SPRITE_H;          // 4096
static constexpr size_t SPRITE_MASK_BYTES = (SPRITE_PIXELS + 7) / 8;  // 512

enum class Expression : uint8_t {
    IDLE = 0,
    LISTEN,
    THINK,
    SPEAK,
    EXPRESSION_COUNT
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
    // 64x64 RGB565, row-major. masks[e] is a packed 1bpp opacity mask: bit
    // (y*64+x) set => pixel opaque. Index both with Expression.
    const uint16_t* frames[static_cast<size_t>(Expression::EXPRESSION_COUNT)];
    const uint8_t* masks[static_cast<size_t>(Expression::EXPRESSION_COUNT)];
};

// Returns true if pixel (x,y) is opaque in the given 1bpp mask.
inline bool sprite_mask_opaque(const uint8_t* mask, size_t x, size_t y) {
    const size_t i = y * SPRITE_W + x;
    return (mask[i >> 3] >> (i & 7)) & 0x1;
}

// Public API
const CharacterTheme* get_character_theme(size_t idx);
const CharacterSprites* get_character_sprites(size_t idx);
size_t character_index_by_id(const char* id);

} // namespace walkie_ui
