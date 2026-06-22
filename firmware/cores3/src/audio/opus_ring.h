#pragma once
//
// opus_ring.h — fixed-capacity ring buffer of RAW Opus packets.
//
// Purpose: decouple the WebSocket receive path (loopTask, Core 0, prio 1) from
// the audio decode+playback path (audio task, Core 1, prio 2). loopTask only
// copies the small raw Opus packet (~80-256 B) into this ring; the audio task
// pops and does the heavy fixed-point Opus decode. This removes the ~30ms/frame
// decode cost from the contended loopTask, which was starving the PCM queue and
// feeding the decoder non-consecutive frames → the "sped-up / garbled" speech.
//
// Single-producer (loopTask push) / single-consumer (audio task pop). Indices
// are volatile so the producer and consumer see each other's updates; each side
// only mutates its own index, which is the classic lock-free SPSC ring on a
// platform with atomic word-sized loads/stores (ESP32-S3).
//
// Overflow policy: when full, push EVICTS THE OLDEST packet and stores the new
// one. For a real-time TTS stream, stale audio is worthless — we must never fall
// behind, so we always keep the freshest frames. (This also keeps push O(1) and
// never blocks loopTask.)
//
// Header-only and Arduino-free so it is unit-testable on the native host.

#include <cstddef>
#include <cstdint>
#include <cstring>

// Largest encoded Opus packet we ever accept. 20ms @ 48kbps VBR averages ~120 B;
// 256 has ample headroom (mirrors AUDIO_MAX_OPUS_PACKET in audio_task.h).
static constexpr size_t OPUS_RING_MAX_PACKET = 256;

// Number of packet slots. 12 frames × 20ms = 240ms of buffered TTS — generous
// headroom for WebSocket burst/gap jitter while staying tiny (~3 KB total).
static constexpr size_t OPUS_RING_CAPACITY = 12;

struct OpusRingSlot {
    uint8_t data[OPUS_RING_MAX_PACKET];
    size_t  len;
};

struct OpusRing {
    OpusRingSlot slots[OPUS_RING_CAPACITY + 1];  // +1 sentinel: full != empty
    volatile size_t head;   // producer writes here, then advances
    volatile size_t tail;   // consumer reads here, then advances
};

static inline void opus_ring_init(OpusRing* r) {
    r->head = 0;
    r->tail = 0;
}

static inline size_t opus_ring__next(size_t i) {
    return (i + 1) % (OPUS_RING_CAPACITY + 1);
}

static inline size_t opus_ring_count(const OpusRing* r) {
    size_t h = r->head, t = r->tail;
    return (h + (OPUS_RING_CAPACITY + 1) - t) % (OPUS_RING_CAPACITY + 1);
}

static inline bool opus_ring__full(const OpusRing* r) {
    return opus_ring__next(r->head) == r->tail;
}

// Producer side. Copies `len` bytes. Rejects zero-length and oversized packets.
// On full, evicts the oldest packet (advances tail) so the newest is kept.
// Returns true if the packet was stored.
static inline bool opus_ring_push(OpusRing* r, const uint8_t* data, size_t len) {
    if (!data || len == 0 || len > OPUS_RING_MAX_PACKET) return false;

    if (opus_ring__full(r)) {
        // Drop oldest to make room — keep the freshest TTS audio.
        r->tail = opus_ring__next(r->tail);
    }
    OpusRingSlot* s = &r->slots[r->head];
    memcpy(s->data, data, len);
    s->len = len;
    r->head = opus_ring__next(r->head);
    return true;
}

// Consumer side. Copies the oldest packet into `out` (must be >= OPUS_RING_MAX_PACKET)
// and sets *out_len. Returns false if the ring is empty.
static inline bool opus_ring_pop(OpusRing* r, uint8_t* out, size_t* out_len) {
    if (r->head == r->tail) return false;   // empty
    const OpusRingSlot* s = &r->slots[r->tail];
    memcpy(out, s->data, s->len);
    *out_len = s->len;
    r->tail = opus_ring__next(r->tail);
    return true;
}
