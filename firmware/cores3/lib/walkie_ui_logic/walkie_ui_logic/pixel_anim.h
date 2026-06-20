#pragma once
/**
 * PXA1 pixel-art animation wire format — PURE parse/validate/timing logic.
 *
 * The backend pushes ONE binary WebSocket message per animation. This header
 * holds only the host-testable pure functions (magic check, header parse,
 * bounds/size validation, frame-advance timing math). The actual PSRAM copy +
 * masked RGB565 blit lives in src/ui/pixel_anim.cpp (device-only) and uses the
 * result of pxa_parse_header() to walk the payload.
 *
 * Wire format (little-endian RGB565, matches the sprite arrays + setSwapBytes(true)):
 *   Bytes 0-3:  magic "PXA1"
 *   Byte 4:     w        (uint8)
 *   Byte 5:     h        (uint8)
 *   Byte 6:     nframes  (uint8)
 *   Byte 7:     frame_ms (uint8, ms per frame)
 *   Then nframes x:
 *     rgb565: w*h*2 bytes (LE uint16, native RGB565)
 *     mask:   ceil(w*h/8) bytes, bit=1 OPAQUE, pixel index y*w+x, LSB-first
 */

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdint>
#include <cstddef>
#endif

namespace walkie_ui {

// 4-byte magic prefix that distinguishes a PXA1 animation from an Opus TTS
// frame on the shared WStype_BIN channel. Opus frames are small (<128B) and
// never start with these bytes.
static constexpr uint8_t PXA_MAGIC[4] = {0x50, 0x58, 0x41, 0x31};  // "PXA1"
static constexpr size_t  PXA_HEADER_BYTES = 8;

// Defensive caps. Reject anything larger so a malformed/hostile header can't
// drive an oversized PSRAM alloc. The device cap is 16KB total; at 48x48 a
// single frame is 48*48*2 + ceil(48*48/8) = 4608 + 288 = 4896 bytes, so 3
// frames (~14.7KB) fit and 4 is the defensive ceiling.
static constexpr uint8_t PXA_MAX_FRAMES   = 4;
static constexpr size_t  PXA_MAX_DIM      = 64;     // w,h each <= 64
static constexpr size_t  PXA_MAX_TOTAL    = 16384;  // total frame bytes cap

struct PxaHeader {
    uint8_t  w;
    uint8_t  h;
    uint8_t  nframes;
    uint8_t  frame_ms;
    size_t   rgb_bytes;     // per-frame RGB565 bytes  = w*h*2
    size_t   mask_bytes;    // per-frame mask bytes     = ceil(w*h/8)
    size_t   frame_stride;  // per-frame total          = rgb_bytes + mask_bytes
    size_t   total_bytes;   // nframes * frame_stride (payload bytes after header)
};

// True iff the first 4 bytes are the PXA1 magic. Cheap disambiguator to run in
// ws_handler BEFORE the Opus decode path. Safe on short payloads (checks len).
inline bool pxa_has_magic(const uint8_t* payload, size_t len) {
    if (payload == nullptr || len < 4) return false;
    return payload[0] == PXA_MAGIC[0] && payload[1] == PXA_MAGIC[1] &&
           payload[2] == PXA_MAGIC[2] && payload[3] == PXA_MAGIC[3];
}

// Parse + fully validate a PXA1 payload header. Returns true and fills `out`
// only when the payload is a well-formed PXA1 message whose declared frames
// exactly fit in `len`. Rejects: null/short payload, bad magic, zero dims,
// dims > PXA_MAX_DIM, zero/oversized nframes, total > PXA_MAX_TOTAL, and any
// payload whose length does not cover header + all declared frame data.
//
// NOTE: requires the payload length to be AT LEAST header + total_bytes; a
// longer payload (trailing bytes) is tolerated, a shorter one is rejected.
bool pxa_parse_header(const uint8_t* payload, size_t len, PxaHeader& out);

// Frame-advance timing math (pure). Given the elapsed milliseconds since the
// animation started, the per-frame duration, and the frame count, return the
// frame index to display. Loops modulo nframes. Guards frame_ms==0 (treats as
// 1) and nframes==0 (returns 0).
size_t pxa_frame_at(uint32_t elapsed_ms, uint8_t frame_ms, uint8_t nframes);

// True once the animation has played for at least `duration_ms` (auto-dismiss).
inline bool pxa_expired(uint32_t elapsed_ms, uint32_t duration_ms) {
    return elapsed_ms >= duration_ms;
}

} // namespace walkie_ui
