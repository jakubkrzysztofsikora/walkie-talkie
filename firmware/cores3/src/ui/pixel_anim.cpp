#include "pixel_anim.h"

#include <esp_heap_caps.h>
#include <cstring>

#include "walkie_ui_logic/pixel_anim.h"

using walkie_ui::PxaHeader;

// How long an animation plays before auto-dismissing back to the normal face.
// The frame loop runs as long as it's active; pxa_frame_at() loops the frames,
// so this just bounds the total on-screen time (~6s).
static constexpr uint32_t PXA_PLAY_MS = 6000;

// PXA_SCALE is defined in pixel_anim.h (shared with PXA_MAX_RUN_PX + its
// static_assert) so the run-buffer sizing and the blit can't drift apart.

// --- Module state (single in-flight animation) ---
static uint8_t* g_buf = nullptr;     // PSRAM: copied frame data (payload after header)
static PxaHeader g_hdr{};            // parsed/validated header (valid iff g_buf != nullptr)
static bool      g_active = false;
static uint32_t  g_start_ms = 0;     // millis() when the animation was loaded

void pixel_anim_clear() {
    if (g_buf) {
        heap_caps_free(g_buf);
        g_buf = nullptr;
    }
    g_active = false;
    // Zero the parsed header too: g_buf is freed, so stale frame geometry
    // (stride/dims) must not survive to be read by a future caller before the
    // next pixel_anim_set() re-parses. (g_hdr is only valid iff g_buf != nullptr.)
    g_hdr = PxaHeader{};
}

bool pixel_anim_set(const uint8_t* payload, size_t len) {
    PxaHeader hdr{};
    if (!walkie_ui::pxa_parse_header(payload, len, hdr)) {
        return false;  // malformed — leave any current animation untouched
    }

    // Allocate the new buffer BEFORE freeing the old one, so a failed alloc
    // can't strand us with no animation when one was already playing.
    uint8_t* buf = static_cast<uint8_t*>(
        heap_caps_malloc(hdr.total_bytes, MALLOC_CAP_SPIRAM));
    if (!buf) {
        // PSRAM exhausted — keep whatever was playing.
        Serial.printf("[pxa] PSRAM alloc %u failed — dropping animation\n",
                      (unsigned)hdr.total_bytes);
        return false;
    }

    // Copy only the frame data (skip the 8-byte header); the WS payload is
    // freed once ws_handler returns, so we must not hold a pointer into it.
    memcpy(buf, payload + walkie_ui::PXA_HEADER_BYTES, hdr.total_bytes);

    // Swap in the new animation.
    pixel_anim_clear();
    g_buf      = buf;
    g_hdr      = hdr;
    g_active   = true;
    g_start_ms = millis();
    Serial.printf("[pxa] anim loaded w=%u h=%u nframes=%u frame_ms=%u bytes=%u\n",
                  g_hdr.w, g_hdr.h, g_hdr.nframes, g_hdr.frame_ms,
                  (unsigned)g_hdr.total_bytes);
    return true;
}

bool pixel_anim_active() {
    if (!g_active || !g_buf) return false;
    const uint32_t elapsed = millis() - g_start_ms;
    if (walkie_ui::pxa_expired(elapsed, PXA_PLAY_MS)) {
        pixel_anim_clear();   // duration done — free PSRAM, dismiss
        return false;
    }
    return true;
}

#ifdef UI_ENGINE

// Opacity test for a per-payload-width 1bpp mask (LSB-first, bit index y*w+x).
// The lib's sprite_mask_opaque() is hardcoded to SPRITE_W=64; the PXA1 width is
// dynamic (== 48), so index with the payload's own width.
static inline bool pxa_mask_opaque(const uint8_t* mask, size_t x, size_t y, size_t w) {
    const size_t i = y * w + x;
    return (mask[i >> 3] >> (i & 7)) & 0x1;
}

void pixel_anim_tick_and_draw(walkie_ui::UIEngine& ui) {
    if (!pixel_anim_active()) return;
    if (!ui.back_buffer || !ui.back_buffer_ok) return;

    const uint32_t elapsed = millis() - g_start_ms;
    const size_t fi = walkie_ui::pxa_frame_at(elapsed, g_hdr.frame_ms, g_hdr.nframes);

    // Locate this frame's RGB565 data + mask inside the PSRAM buffer.
    const uint8_t* frame = g_buf + fi * g_hdr.frame_stride;
    const uint16_t* data = reinterpret_cast<const uint16_t*>(frame);
    const uint8_t*  mask = frame + g_hdr.rgb_bytes;

    const int16_t w = (int16_t)g_hdr.w;
    const int16_t h = (int16_t)g_hdr.h;
    const int16_t dw = w * PXA_SCALE;
    const int16_t dh = h * PXA_SCALE;
    // Centre on the screen (back-buffer is 320x240).
    const int16_t x0 = (int16_t)((320 - dw) / 2);
    const int16_t y0 = (int16_t)((240 - dh) / 2);

    // Blit contiguous opaque horizontal runs via pushImage, scaled up. Same
    // masked-run idea as draw_sprite_native, but each source pixel maps to a
    // PXA_SCALE x PXA_SCALE block. Build a small per-run scaled row buffer so we
    // still use the cheap pushImage(x,y,w,h,T*) blit rather than per-pixel draws.
    // The static run[] buffer is reused across calls; this is safe ONLY because
    // the blit is single-threaded on loopTask (the UI render path). No other task
    // calls pixel_anim_tick_and_draw, so there is no concurrent writer.
    static uint16_t run[PXA_MAX_RUN_PX];
    for (int16_t y = 0; y < h; ++y) {
        const uint16_t* srow = &data[(size_t)y * (size_t)w];
        int16_t x = 0;
        while (x < w) {
            if (!pxa_mask_opaque(mask, (size_t)x, (size_t)y, (size_t)w)) { ++x; continue; }
            const int16_t run_start = x;
            while (x < w && pxa_mask_opaque(mask, (size_t)x, (size_t)y, (size_t)w)) ++x;
            const int16_t run_len = (int16_t)(x - run_start);
            const int16_t scaled_len = run_len * PXA_SCALE;
            // Expand the run horizontally into `run` (each src px -> PXA_SCALE px).
            int16_t k = 0;
            for (int16_t rx = 0; rx < run_len; ++rx) {
                const uint16_t c = srow[run_start + rx];
                for (int16_t s = 0; s < PXA_SCALE; ++s) run[k++] = c;
            }
            const int16_t dx = (int16_t)(x0 + run_start * PXA_SCALE);
            // Replicate the scaled row PXA_SCALE times vertically.
            for (int16_t sy = 0; sy < PXA_SCALE; ++sy) {
                ui.back_buffer->pushImage((int16_t)dx, (int16_t)(y0 + y * PXA_SCALE + sy),
                                          scaled_len, 1, run);
            }
        }
    }
}

#endif  // UI_ENGINE
