#pragma once
/**
 * Character picker menu.
 *
 * Icon-only 2-row grid. Long-press from IDLE opens the menu;
 * tap a badge to select; tap outside to close.
 */

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdint>
#include <cstddef>
#endif
#include "sprites.h"

namespace walkie_ui {

static constexpr size_t MENU_COLS = 6;
static constexpr size_t MENU_ROWS = 2;
static constexpr size_t MENU_BADGE_RADIUS = 26;  // px, 4x target size

struct MenuLayout {
    int16_t centers[NCHARS][2];  // x, y for each badge
};

MenuLayout compute_menu_layout(int16_t screen_w, int16_t screen_h);

// Returns character index or NCHARS if coordinate is outside any badge.
size_t menu_hit_test(int16_t x, int16_t y, const MenuLayout& layout);

} // namespace walkie_ui
