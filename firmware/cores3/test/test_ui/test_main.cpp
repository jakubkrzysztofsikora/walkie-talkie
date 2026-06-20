#include <unity.h>
#include <cstring>

#include "walkie_ui_logic/sprites.h"
#include "walkie_ui_logic/animator.h"
#include "walkie_ui_logic/menu.h"

using namespace walkie_ui;

void test_character_count_matches_nchars(void) {
    TEST_ASSERT_EQUAL_INT(11, static_cast<int>(NCHARS));
}

void test_sprite_dimensions(void) {
    // Migrated 16x16 indexed -> 64x64 RGB565 + 1bpp mask.
    TEST_ASSERT_EQUAL_UINT32(64, SPRITE_W);
    TEST_ASSERT_EQUAL_UINT32(64, SPRITE_H);
    TEST_ASSERT_EQUAL_UINT32(64 * 64, SPRITE_PIXELS);
    TEST_ASSERT_EQUAL_UINT32((64 * 64 + 7) / 8, SPRITE_MASK_BYTES);  // 512
}

void test_every_character_has_all_frames(void) {
    for (size_t i = 0; i < NCHARS; ++i) {
        const CharacterSprites* spr = get_character_sprites(i);
        const CharacterTheme* theme = get_character_theme(i);
        TEST_ASSERT_NOT_NULL(spr);
        TEST_ASSERT_NOT_NULL(theme);
        TEST_ASSERT_EQUAL_STRING(theme->id, spr->id);
        for (size_t e = 0; e < static_cast<size_t>(Expression::EXPRESSION_COUNT); ++e) {
            TEST_ASSERT_NOT_NULL(spr->frames[e]);
            TEST_ASSERT_NOT_NULL(spr->masks[e]);
            // Ensure the mask marks at least some pixels opaque (non-empty art).
            uint32_t opaque = 0;
            for (size_t p = 0; p < SPRITE_MASK_BYTES; ++p) {
                opaque += __builtin_popcount(spr->masks[e][p]);
            }
            TEST_ASSERT_GREATER_THAN_UINT32(0, opaque);
        }
    }
}

void test_frames_pairwise_distinct(void) {
    // Each character's 4 expression frames (and masks) must be DISTINCT arrays.
    // Catches a generator bug that emits the same frame 4x, or swaps/duplicates
    // an expression.
    const size_t n = static_cast<size_t>(Expression::EXPRESSION_COUNT);
    for (size_t i = 0; i < NCHARS; ++i) {
        const CharacterSprites* spr = get_character_sprites(i);
        TEST_ASSERT_NOT_NULL(spr);
        for (size_t a = 0; a < n; ++a) {
            for (size_t b = a + 1; b < n; ++b) {
                TEST_ASSERT_TRUE(spr->frames[a] != spr->frames[b]);
                TEST_ASSERT_TRUE(spr->masks[a] != spr->masks[b]);
            }
        }
    }
}

void test_roster_order_pins_ids(void) {
    // Pin the full 11-element roster order. Catches reordering or a swapped
    // character in the generated table.
    static const char* const expected[NCHARS] = {
        "radek", "steve", "simba", "ryder", "creeper", "pimpek",
        "crewmate", "sonic", "pikachu", "mario", "roblox_noob",
    };
    for (size_t i = 0; i < NCHARS; ++i) {
        const CharacterTheme* theme = get_character_theme(i);
        const CharacterSprites* spr = get_character_sprites(i);
        TEST_ASSERT_NOT_NULL(theme);
        TEST_ASSERT_NOT_NULL(spr);
        TEST_ASSERT_EQUAL_STRING(expected[i], theme->id);
        TEST_ASSERT_EQUAL_STRING(expected[i], spr->id);
    }
}

// The PNG->RGB565 conversion is pure data; assert known RGB triples pack to the
// expected 16-bit value (matches generate_sprites_png.rgb565_pack and the C++
// blit path). RGB565 = ((r>>3)<<11)|((g>>2)<<5)|(b>>3).
static uint16_t rgb565_pack(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void test_rgb565_roundtrip(void) {
    TEST_ASSERT_EQUAL_HEX16(0x0000, rgb565_pack(0x00, 0x00, 0x00));  // black
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565_pack(0xFF, 0xFF, 0xFF));  // white
    TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565_pack(0xFF, 0x00, 0x00));  // red
    TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565_pack(0x00, 0xFF, 0x00));  // green
    TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565_pack(0x00, 0x00, 0xFF));  // blue
    // Mid grey 0x80 -> r:0x10<<11, g:0x20<<5, b:0x10
    TEST_ASSERT_EQUAL_HEX16(0x8410, rgb565_pack(0x80, 0x80, 0x80));
}

void test_sprite_mask_opaque_indexing(void) {
    // bit (y*64+x). Craft a tiny mask buffer and verify the accessor.
    uint8_t mask[SPRITE_MASK_BYTES] = {0};
    // pixel (0,0) -> bit 0; pixel (1,0) -> bit 1; pixel (0,1) -> bit 64.
    mask[0] = 0x01;            // (0,0)
    mask[64 / 8] = 0x01;       // (0,1)
    TEST_ASSERT_TRUE(sprite_mask_opaque(mask, 0, 0));
    TEST_ASSERT_FALSE(sprite_mask_opaque(mask, 1, 0));
    TEST_ASSERT_TRUE(sprite_mask_opaque(mask, 0, 1));
}

void test_character_index_by_id(void) {
    TEST_ASSERT_EQUAL_UINT32(0, character_index_by_id("radek"));
    TEST_ASSERT_EQUAL_UINT32(1, character_index_by_id("steve"));
    TEST_ASSERT_EQUAL_UINT32(10, character_index_by_id("roblox_noob"));
    TEST_ASSERT_EQUAL_UINT32(NCHARS, character_index_by_id("dmitry"));
    TEST_ASSERT_EQUAL_UINT32(NCHARS, character_index_by_id("unknown"));
    TEST_ASSERT_EQUAL_UINT32(NCHARS, character_index_by_id(nullptr));
}

void test_animator_state_mapping(void) {
    AnimatorState state;
    AnimatorConfig cfg = default_animator_config();

    Expression e = animator_tick(state, MascotState::IDLE, cfg);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Expression::IDLE), static_cast<int>(e));

    e = animator_tick(state, MascotState::LISTENING, cfg);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Expression::LISTEN), static_cast<int>(e));

    e = animator_tick(state, MascotState::THINKING, cfg);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Expression::THINK), static_cast<int>(e));

    e = animator_tick(state, MascotState::SPEAKING, cfg);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Expression::SPEAK), static_cast<int>(e));
}

void test_animator_tick_advances(void) {
    AnimatorState state;
    AnimatorConfig cfg = default_animator_config();
    animator_tick(state, MascotState::IDLE, cfg);
    uint32_t first_tick = state.tick;
    animator_tick(state, MascotState::IDLE, cfg);
    TEST_ASSERT_EQUAL_UINT32(first_tick + 1, state.tick);
}

void test_bounce_offset_bounds(void) {
    AnimatorState state;
    AnimatorConfig cfg = default_animator_config();
    for (int i = 0; i < 64; ++i) {
        animator_tick(state, MascotState::IDLE, cfg);
        int8_t off = bounce_offset(state);
        TEST_ASSERT_GREATER_OR_EQUAL_INT8(-3, off);
        TEST_ASSERT_LESS_OR_EQUAL_INT8(3, off);
    }
}

void test_menu_layout_has_all_centers(void) {
    MenuLayout layout = compute_menu_layout(320, 240);
    for (size_t i = 0; i < NCHARS; ++i) {
        TEST_ASSERT_GREATER_THAN_INT16(0, layout.centers[i][0]);
        TEST_ASSERT_GREATER_THAN_INT16(0, layout.centers[i][1]);
        TEST_ASSERT_LESS_THAN_INT16(320, layout.centers[i][0]);
        TEST_ASSERT_LESS_THAN_INT16(240, layout.centers[i][1]);
    }
}

void test_menu_hit_test_finds_badge(void) {
    MenuLayout layout = compute_menu_layout(320, 240);
    size_t idx = menu_hit_test(layout.centers[0][0], layout.centers[0][1], layout);
    TEST_ASSERT_EQUAL_UINT32(0, idx);
}

void test_menu_hit_test_outside_returns_nchars(void) {
    MenuLayout layout = compute_menu_layout(320, 240);
    size_t idx = menu_hit_test(10, 10, layout);
    TEST_ASSERT_EQUAL_UINT32(NCHARS, idx);
}

void test_screen_to_mascot_state(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MascotState::IDLE),
                          static_cast<int>(screen_to_mascot_state(ScreenState::IDLE, false, 0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MascotState::LISTENING),
                          static_cast<int>(screen_to_mascot_state(ScreenState::SESSION_ACTIVE, true, 0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MascotState::SPEAKING),
                          static_cast<int>(screen_to_mascot_state(ScreenState::SESSION_ACTIVE, false, 2)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MascotState::THINKING),
                          static_cast<int>(screen_to_mascot_state(ScreenState::SESSION_ACTIVE, false, 0)));
}

static int expr_i(Expression e) { return static_cast<int>(e); }

void test_compute_screen_connect_states_think(void) {
    // WIFI_CONNECT / WSS_CONNECT -> THINK
    TEST_ASSERT_EQUAL_INT(expr_i(Expression::THINK),
        expr_i(compute_screen(AudioMode::AUDIO_IDLE, ScreenState::WIFI_CONNECT, false, false)));
    TEST_ASSERT_EQUAL_INT(expr_i(Expression::THINK),
        expr_i(compute_screen(AudioMode::AUDIO_IDLE, ScreenState::WSS_CONNECT, false, false)));
}

void test_compute_screen_session_talk_listen(void) {
    // SESSION_ACTIVE + AUDIO_TALK (user holding) -> LISTEN
    TEST_ASSERT_EQUAL_INT(expr_i(Expression::LISTEN),
        expr_i(compute_screen(AudioMode::AUDIO_TALK, ScreenState::SESSION_ACTIVE, true, false)));
}

void test_compute_screen_agent_reply_speak_any_state(void) {
    // amode==AUDIO_LISTEN forces SPEAK regardless of DeviceState, incl IDLE
    // (the post-release reply) and even during connect.
    TEST_ASSERT_EQUAL_INT(expr_i(Expression::SPEAK),
        expr_i(compute_screen(AudioMode::AUDIO_LISTEN, ScreenState::IDLE, false, false)));
    TEST_ASSERT_EQUAL_INT(expr_i(Expression::SPEAK),
        expr_i(compute_screen(AudioMode::AUDIO_LISTEN, ScreenState::SESSION_ACTIVE, false, false)));
    TEST_ASSERT_EQUAL_INT(expr_i(Expression::SPEAK),
        expr_i(compute_screen(AudioMode::AUDIO_LISTEN, ScreenState::WIFI_CONNECT, false, false)));
}

void test_compute_screen_idle(void) {
    // IDLE with idle audio -> IDLE
    TEST_ASSERT_EQUAL_INT(expr_i(Expression::IDLE),
        expr_i(compute_screen(AudioMode::AUDIO_IDLE, ScreenState::IDLE, false, false)));
    // SESSION_ACTIVE but no talk and no listen -> IDLE (no agent audio, not holding)
    TEST_ASSERT_EQUAL_INT(expr_i(Expression::IDLE),
        expr_i(compute_screen(AudioMode::AUDIO_IDLE, ScreenState::SESSION_ACTIVE, false, false)));
}

void test_compute_screen_menu_overrides(void) {
    // Menu open draws its own UI -> IDLE expression even if audio is playing.
    TEST_ASSERT_EQUAL_INT(expr_i(Expression::IDLE),
        expr_i(compute_screen(AudioMode::AUDIO_LISTEN, ScreenState::IDLE, false, true)));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_character_count_matches_nchars);
    RUN_TEST(test_sprite_dimensions);
    RUN_TEST(test_every_character_has_all_frames);
    RUN_TEST(test_frames_pairwise_distinct);
    RUN_TEST(test_roster_order_pins_ids);
    RUN_TEST(test_rgb565_roundtrip);
    RUN_TEST(test_sprite_mask_opaque_indexing);
    RUN_TEST(test_character_index_by_id);
    RUN_TEST(test_animator_state_mapping);
    RUN_TEST(test_animator_tick_advances);
    RUN_TEST(test_bounce_offset_bounds);
    RUN_TEST(test_menu_layout_has_all_centers);
    RUN_TEST(test_menu_hit_test_finds_badge);
    RUN_TEST(test_menu_hit_test_outside_returns_nchars);
    RUN_TEST(test_screen_to_mascot_state);
    RUN_TEST(test_compute_screen_connect_states_think);
    RUN_TEST(test_compute_screen_session_talk_listen);
    RUN_TEST(test_compute_screen_agent_reply_speak_any_state);
    RUN_TEST(test_compute_screen_idle);
    RUN_TEST(test_compute_screen_menu_overrides);
    return UNITY_END();
}
