#include <Arduino.h>
#include "audio/opus_stub.h"
#include <opus.h>

static OpusEncoder* g_encoder = nullptr;
static OpusDecoder* g_decoder = nullptr;
static bool g_initialized = false;

bool opus_stub_init() {
    if (g_initialized) return true;
    int err = 0;

    g_encoder = opus_encoder_create(OPUS_SAMPLE_RATE, OPUS_CHANNELS, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !g_encoder) {
        Serial.printf("[opus] encoder create failed: %d\n", err);
        return false;
    }
    opus_encoder_ctl(g_encoder, OPUS_SET_BITRATE(OPUS_BITRATE));

    g_decoder = opus_decoder_create(OPUS_SAMPLE_RATE, OPUS_CHANNELS, &err);
    if (err != OPUS_OK || !g_decoder) {
        Serial.printf("[opus] decoder create failed: %d\n", err);
        return false;
    }
    g_initialized = true;
    Serial.printf("[opus] init OK: %dHz %dch %dbps VoIP\n", OPUS_SAMPLE_RATE, OPUS_CHANNELS, OPUS_BITRATE);
    return true;
}

int opus_encode_frame(const int16_t* pcm_320, uint8_t* out, size_t out_cap) {
    if (!g_encoder) return -1;
    int len = opus_encode(g_encoder, pcm_320, OPUS_FRAME_SAMPLES, out, out_cap);
    return len;
}

int opus_decode_frame(const uint8_t* opus_data, size_t opus_len, int16_t* pcm_out_320) {
    if (!g_decoder) return -1;
    int samples = opus_decode(g_decoder, opus_data, opus_len, pcm_out_320, OPUS_FRAME_SAMPLES, 0);
    return samples;
}
