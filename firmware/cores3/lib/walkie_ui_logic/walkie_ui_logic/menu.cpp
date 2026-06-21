#include "menu.h"

namespace walkie_ui {

MenuLayout compute_menu_layout(int16_t screen_w, int16_t screen_h) {
    MenuLayout layout{};
    const int16_t cols = static_cast<int16_t>(MENU_COLS);
    const int16_t rows = static_cast<int16_t>(MENU_ROWS);
    const int16_t cell_w = screen_w / cols;
    const int16_t cell_h = (screen_h - 40) / rows;  // leave top banner
    const int16_t y0 = 60;   // was 80 -> bottom row clipped off-screen

    for (size_t i = 0; i < NCHARS; ++i) {
        int16_t col = static_cast<int16_t>(i % MENU_COLS);
        int16_t row = static_cast<int16_t>(i / MENU_COLS);
        layout.centers[i][0] = col * cell_w + cell_w / 2;
        layout.centers[i][1] = y0 + row * cell_h + cell_h / 2;
    }
    return layout;
}

size_t menu_hit_test(int16_t x, int16_t y, const MenuLayout& layout) {
    for (size_t i = 0; i < NCHARS; ++i) {
        int32_t dx = static_cast<int32_t>(x) - layout.centers[i][0];
        int32_t dy = static_cast<int32_t>(y) - layout.centers[i][1];
        if (dx * dx + dy * dy <= MENU_BADGE_RADIUS * MENU_BADGE_RADIUS) {
            return i;
        }
    }
    return NCHARS;
}

} // namespace walkie_ui
