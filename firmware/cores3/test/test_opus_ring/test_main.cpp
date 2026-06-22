#include <unity.h>
#include <cstring>
#include <cstdint>

// Test the raw-Opus ring buffer in isolation (host-buildable, no Arduino deps).
// This ring decouples loopTask (producer: copies raw Opus packets in) from the
// audio task (consumer: decodes + plays). The whole point is that the producer
// only does a small memcpy, never the heavy Opus decode — so loopTask can keep
// up and frames are not dropped (the root cause of the speech distortion).

#include "audio/opus_ring.h"

void setUp(void) {}
void tearDown(void) {}

// --- Empty / basic ---------------------------------------------------------

void test_new_ring_is_empty(void) {
    OpusRing r;
    opus_ring_init(&r);
    uint8_t out[OPUS_RING_MAX_PACKET];
    size_t len = 0;
    TEST_ASSERT_FALSE(opus_ring_pop(&r, out, &len));   // nothing to pop
    TEST_ASSERT_EQUAL_UINT(0, opus_ring_count(&r));
}

void test_push_then_pop_roundtrips_bytes(void) {
    OpusRing r;
    opus_ring_init(&r);
    const uint8_t pkt[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    TEST_ASSERT_TRUE(opus_ring_push(&r, pkt, sizeof(pkt)));
    TEST_ASSERT_EQUAL_UINT(1, opus_ring_count(&r));

    uint8_t out[OPUS_RING_MAX_PACKET];
    size_t len = 0;
    TEST_ASSERT_TRUE(opus_ring_pop(&r, out, &len));
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pkt, out, sizeof(pkt));
    TEST_ASSERT_EQUAL_UINT(0, opus_ring_count(&r));
}

// --- FIFO order ------------------------------------------------------------

void test_fifo_order_preserved(void) {
    OpusRing r;
    opus_ring_init(&r);
    const uint8_t a[] = {1, 1, 1};
    const uint8_t b[] = {2, 2};
    const uint8_t c[] = {3, 3, 3, 3};
    opus_ring_push(&r, a, sizeof(a));
    opus_ring_push(&r, b, sizeof(b));
    opus_ring_push(&r, c, sizeof(c));

    uint8_t out[OPUS_RING_MAX_PACKET];
    size_t len = 0;
    opus_ring_pop(&r, out, &len);
    TEST_ASSERT_EQUAL_UINT(3, len);
    TEST_ASSERT_EQUAL_UINT8(1, out[0]);
    opus_ring_pop(&r, out, &len);
    TEST_ASSERT_EQUAL_UINT(2, len);
    TEST_ASSERT_EQUAL_UINT8(2, out[0]);
    opus_ring_pop(&r, out, &len);
    TEST_ASSERT_EQUAL_UINT(4, len);
    TEST_ASSERT_EQUAL_UINT8(3, out[0]);
}

// --- Capacity / overflow ---------------------------------------------------

void test_fills_to_capacity(void) {
    OpusRing r;
    opus_ring_init(&r);
    const uint8_t pkt[] = {0x55};
    for (size_t i = 0; i < OPUS_RING_CAPACITY; ++i) {
        TEST_ASSERT_TRUE(opus_ring_push(&r, pkt, sizeof(pkt)));
    }
    TEST_ASSERT_EQUAL_UINT(OPUS_RING_CAPACITY, opus_ring_count(&r));
}

void test_overflow_drops_oldest_keeps_newest(void) {
    OpusRing r;
    opus_ring_init(&r);
    // Fill to capacity with marker = slot index.
    for (size_t i = 0; i < OPUS_RING_CAPACITY; ++i) {
        uint8_t pkt[1] = { (uint8_t)i };
        TEST_ASSERT_TRUE(opus_ring_push(&r, pkt, 1));
    }
    // One more push past capacity: must succeed by evicting the OLDEST, so the
    // newest TTS audio is always preserved (dropping stale audio is correct for
    // a real-time stream — we never want to fall behind).
    uint8_t newest[1] = { 0xAB };
    TEST_ASSERT_TRUE(opus_ring_push(&r, newest, 1));
    TEST_ASSERT_EQUAL_UINT(OPUS_RING_CAPACITY, opus_ring_count(&r));

    // The first pop must NOT be slot 0 (it was evicted); it must be slot 1.
    uint8_t out[OPUS_RING_MAX_PACKET];
    size_t len = 0;
    opus_ring_pop(&r, out, &len);
    TEST_ASSERT_EQUAL_UINT8(1, out[0]);   // oldest (0) evicted, 1 is now head

    // Drain to the end; the very last must be our newest 0xAB.
    uint8_t last = 0;
    while (opus_ring_pop(&r, out, &len)) { last = out[0]; }
    TEST_ASSERT_EQUAL_UINT8(0xAB, last);
}

// --- Guards ----------------------------------------------------------------

void test_oversized_packet_rejected(void) {
    OpusRing r;
    opus_ring_init(&r);
    uint8_t big[OPUS_RING_MAX_PACKET + 1] = {0};
    TEST_ASSERT_FALSE(opus_ring_push(&r, big, sizeof(big)));
    TEST_ASSERT_EQUAL_UINT(0, opus_ring_count(&r));
}

void test_zero_length_packet_rejected(void) {
    OpusRing r;
    opus_ring_init(&r);
    uint8_t x[1] = {0};
    TEST_ASSERT_FALSE(opus_ring_push(&r, x, 0));
    TEST_ASSERT_EQUAL_UINT(0, opus_ring_count(&r));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_new_ring_is_empty);
    RUN_TEST(test_push_then_pop_roundtrips_bytes);
    RUN_TEST(test_fifo_order_preserved);
    RUN_TEST(test_fills_to_capacity);
    RUN_TEST(test_overflow_drops_oldest_keeps_newest);
    RUN_TEST(test_oversized_packet_rejected);
    RUN_TEST(test_zero_length_packet_rejected);
    return UNITY_END();
}
