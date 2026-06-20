#include <unity.h>
#include <cstring>
#include <vector>
#include <cstdint>

#include "walkie_ui_logic/pixel_anim.h"

using namespace walkie_ui;

// --- Helpers -------------------------------------------------------------

// Build a well-formed PXA1 payload: header + nframes*(w*h*2 rgb + ceil(w*h/8) mask).
// Frame/mask bytes are filled with a marker so we don't depend on real art.
static std::vector<uint8_t> make_pxa(uint8_t w, uint8_t h, uint8_t nframes,
                                     uint8_t frame_ms) {
    const size_t pixels = (size_t)w * h;
    const size_t rgb = pixels * 2;
    const size_t mask = (pixels + 7) / 8;
    const size_t stride = rgb + mask;
    std::vector<uint8_t> p;
    p.reserve(8 + stride * nframes);
    p.push_back('P'); p.push_back('X'); p.push_back('A'); p.push_back('1');
    p.push_back(w); p.push_back(h); p.push_back(nframes); p.push_back(frame_ms);
    p.resize(8 + stride * nframes, 0xAB);
    return p;
}

// --- Magic check ---------------------------------------------------------

void test_magic_accepts_pxa1(void) {
    const uint8_t good[8] = {'P','X','A','1', 48,48,1,220};
    TEST_ASSERT_TRUE(pxa_has_magic(good, sizeof(good)));
}

void test_magic_rejects_bad_and_short(void) {
    const uint8_t bad[8] = {'O','P','U','S', 0,0,0,0};
    TEST_ASSERT_FALSE(pxa_has_magic(bad, sizeof(bad)));
    const uint8_t shortp[3] = {'P','X','A'};
    TEST_ASSERT_FALSE(pxa_has_magic(shortp, sizeof(shortp)));
    TEST_ASSERT_FALSE(pxa_has_magic(nullptr, 0));
    // A typical tiny Opus frame must not be mistaken for PXA1.
    const uint8_t opus[4] = {0x78, 0x01, 0x02, 0x03};
    TEST_ASSERT_FALSE(pxa_has_magic(opus, sizeof(opus)));
}

// --- Header parse: valid -------------------------------------------------

void test_parse_valid_48x48x3(void) {
    auto p = make_pxa(48, 48, 3, 220);
    PxaHeader hdr{};
    TEST_ASSERT_TRUE(pxa_parse_header(p.data(), p.size(), hdr));
    TEST_ASSERT_EQUAL_UINT8(48, hdr.w);
    TEST_ASSERT_EQUAL_UINT8(48, hdr.h);
    TEST_ASSERT_EQUAL_UINT8(3, hdr.nframes);
    TEST_ASSERT_EQUAL_UINT8(220, hdr.frame_ms);
    TEST_ASSERT_EQUAL_UINT32(48u * 48u * 2u, hdr.rgb_bytes);          // 4608
    TEST_ASSERT_EQUAL_UINT32((48u * 48u + 7u) / 8u, hdr.mask_bytes);  // 288
    TEST_ASSERT_EQUAL_UINT32(4608u + 288u, hdr.frame_stride);         // 4896
    TEST_ASSERT_EQUAL_UINT32((4608u + 288u) * 3u, hdr.total_bytes);   // 14688
    // The whole payload (header + 3 frames) must be ~14.7KB, under the 16KB cap.
    TEST_ASSERT_EQUAL_UINT32(8u + 14688u, (uint32_t)p.size());
}

void test_parse_valid_single_frame(void) {
    auto p = make_pxa(48, 48, 1, 100);
    PxaHeader hdr{};
    TEST_ASSERT_TRUE(pxa_parse_header(p.data(), p.size(), hdr));
    TEST_ASSERT_EQUAL_UINT8(1, hdr.nframes);
    TEST_ASSERT_EQUAL_UINT32(4896u, hdr.total_bytes);
}

// A trailing-bytes payload (longer than declared) is tolerated.
void test_parse_tolerates_trailing_bytes(void) {
    auto p = make_pxa(48, 48, 1, 100);
    p.push_back(0x00); p.push_back(0x00);  // extra junk
    PxaHeader hdr{};
    TEST_ASSERT_TRUE(pxa_parse_header(p.data(), p.size(), hdr));
}

// --- Header parse: rejections -------------------------------------------

void test_parse_rejects_bad_magic(void) {
    auto p = make_pxa(48, 48, 1, 100);
    p[0] = 'X';  // corrupt magic
    PxaHeader hdr{};
    TEST_ASSERT_FALSE(pxa_parse_header(p.data(), p.size(), hdr));
}

void test_parse_rejects_short_header(void) {
    const uint8_t p[5] = {'P','X','A','1', 48};  // < 8 bytes
    PxaHeader hdr{};
    TEST_ASSERT_FALSE(pxa_parse_header(p, sizeof(p), hdr));
}

void test_parse_rejects_truncated_frame_data(void) {
    // Header declares 3 frames but payload only carries header + 1 frame.
    auto full = make_pxa(48, 48, 3, 220);
    const size_t stride = 48u * 48u * 2u + (48u * 48u + 7u) / 8u;
    std::vector<uint8_t> truncated(full.begin(), full.begin() + 8 + stride);  // 1 frame only
    PxaHeader hdr{};
    TEST_ASSERT_FALSE(pxa_parse_header(truncated.data(), truncated.size(), hdr));
}

void test_parse_rejects_zero_dims(void) {
    auto p = make_pxa(48, 48, 1, 100);
    p[4] = 0;  // w = 0
    PxaHeader hdr{};
    TEST_ASSERT_FALSE(pxa_parse_header(p.data(), p.size(), hdr));
    p[4] = 48; p[5] = 0;  // h = 0
    TEST_ASSERT_FALSE(pxa_parse_header(p.data(), p.size(), hdr));
}

void test_parse_rejects_zero_frames(void) {
    auto p = make_pxa(48, 48, 1, 100);
    p[6] = 0;  // nframes = 0
    PxaHeader hdr{};
    TEST_ASSERT_FALSE(pxa_parse_header(p.data(), p.size(), hdr));
}

void test_parse_rejects_oversized_nframes(void) {
    // Declare more frames than the cap. Build a payload big enough that ONLY the
    // frame-count cap (not a truncation check) triggers the rejection.
    const uint8_t over = PXA_MAX_FRAMES + 1;  // 5
    auto p = make_pxa(48, 48, over, 220);
    PxaHeader hdr{};
    TEST_ASSERT_FALSE(pxa_parse_header(p.data(), p.size(), hdr));
}

void test_parse_rejects_oversized_total(void) {
    // 64x64x4 = 4 * (64*64*2 + 512) = 4 * 8704 = 34816 > 16384 cap.
    auto p = make_pxa(64, 64, 4, 220);
    PxaHeader hdr{};
    TEST_ASSERT_FALSE(pxa_parse_header(p.data(), p.size(), hdr));
}

void test_parse_rejects_dim_over_cap(void) {
    // w within uint8 but > PXA_MAX_DIM (64). Use a small declared payload; the
    // dim cap must fire before any size math.
    std::vector<uint8_t> p = {'P','X','A','1', (uint8_t)(PXA_MAX_DIM + 1), 10, 1, 100};
    p.resize(8 + 4096, 0xAB);
    PxaHeader hdr{};
    TEST_ASSERT_FALSE(pxa_parse_header(p.data(), p.size(), hdr));
}

// --- Frame-advance timing math ------------------------------------------

void test_frame_at_advances(void) {
    // 3 frames, 220ms each. elapsed -> frame index (looping).
    TEST_ASSERT_EQUAL_UINT32(0, pxa_frame_at(0,   220, 3));
    TEST_ASSERT_EQUAL_UINT32(0, pxa_frame_at(219, 220, 3));
    TEST_ASSERT_EQUAL_UINT32(1, pxa_frame_at(220, 220, 3));
    TEST_ASSERT_EQUAL_UINT32(1, pxa_frame_at(439, 220, 3));
    TEST_ASSERT_EQUAL_UINT32(2, pxa_frame_at(440, 220, 3));
    // Loops back to 0 after the last frame.
    TEST_ASSERT_EQUAL_UINT32(0, pxa_frame_at(660, 220, 3));
    TEST_ASSERT_EQUAL_UINT32(1, pxa_frame_at(880, 220, 3));
}

void test_frame_at_single_frame_always_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, pxa_frame_at(0,    220, 1));
    TEST_ASSERT_EQUAL_UINT32(0, pxa_frame_at(5000, 220, 1));
}

void test_frame_at_guards_zero_inputs(void) {
    // frame_ms==0 treated as 1ms/frame (no div-by-zero).
    TEST_ASSERT_EQUAL_UINT32(0, pxa_frame_at(0, 0, 3));
    TEST_ASSERT_EQUAL_UINT32(2, pxa_frame_at(2, 0, 3));
    // nframes==0 returns 0 (no modulo-by-zero).
    TEST_ASSERT_EQUAL_UINT32(0, pxa_frame_at(1000, 220, 0));
}

void test_expired(void) {
    TEST_ASSERT_FALSE(pxa_expired(0, 6000));
    TEST_ASSERT_FALSE(pxa_expired(5999, 6000));
    TEST_ASSERT_TRUE(pxa_expired(6000, 6000));
    TEST_ASSERT_TRUE(pxa_expired(7000, 6000));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_magic_accepts_pxa1);
    RUN_TEST(test_magic_rejects_bad_and_short);
    RUN_TEST(test_parse_valid_48x48x3);
    RUN_TEST(test_parse_valid_single_frame);
    RUN_TEST(test_parse_tolerates_trailing_bytes);
    RUN_TEST(test_parse_rejects_bad_magic);
    RUN_TEST(test_parse_rejects_short_header);
    RUN_TEST(test_parse_rejects_truncated_frame_data);
    RUN_TEST(test_parse_rejects_zero_dims);
    RUN_TEST(test_parse_rejects_zero_frames);
    RUN_TEST(test_parse_rejects_oversized_nframes);
    RUN_TEST(test_parse_rejects_oversized_total);
    RUN_TEST(test_parse_rejects_dim_over_cap);
    RUN_TEST(test_frame_at_advances);
    RUN_TEST(test_frame_at_single_frame_always_zero);
    RUN_TEST(test_frame_at_guards_zero_inputs);
    RUN_TEST(test_expired);
    return UNITY_END();
}
