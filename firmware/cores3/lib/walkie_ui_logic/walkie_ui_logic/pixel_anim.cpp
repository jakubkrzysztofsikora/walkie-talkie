#include "pixel_anim.h"

namespace walkie_ui {

bool pxa_parse_header(const uint8_t* payload, size_t len, PxaHeader& out) {
    if (!pxa_has_magic(payload, len)) return false;
    if (len < PXA_HEADER_BYTES) return false;

    const uint8_t w        = payload[4];
    const uint8_t h        = payload[5];
    const uint8_t nframes  = payload[6];
    const uint8_t frame_ms = payload[7];

    // Bounds: non-zero dims within cap, non-zero frame count within cap.
    if (w == 0 || h == 0) return false;
    if (w > PXA_MAX_DIM || h > PXA_MAX_DIM) return false;
    if (nframes == 0 || nframes > PXA_MAX_FRAMES) return false;

    const size_t pixels      = (size_t)w * (size_t)h;
    const size_t rgb_bytes   = pixels * 2;
    const size_t mask_bytes  = (pixels + 7) / 8;
    const size_t frame_stride = rgb_bytes + mask_bytes;
    const size_t total_bytes = frame_stride * (size_t)nframes;

    // Defensive total-size cap (independent of the per-field caps above).
    if (total_bytes > PXA_MAX_TOTAL) return false;

    // The payload must actually contain header + all declared frame data.
    if (len < PXA_HEADER_BYTES + total_bytes) return false;

    out.w            = w;
    out.h            = h;
    out.nframes      = nframes;
    out.frame_ms     = frame_ms;
    out.rgb_bytes    = rgb_bytes;
    out.mask_bytes   = mask_bytes;
    out.frame_stride = frame_stride;
    out.total_bytes  = total_bytes;
    return true;
}

size_t pxa_frame_at(uint32_t elapsed_ms, uint8_t frame_ms, uint8_t nframes) {
    if (nframes == 0) return 0;
    const uint32_t step = (frame_ms == 0) ? 1u : (uint32_t)frame_ms;
    const uint32_t advanced = elapsed_ms / step;
    return (size_t)(advanced % (uint32_t)nframes);
}

} // namespace walkie_ui
