#pragma once
// Opus codec wrapper for CoreS3 firmware
// Uses esphome/micro-opus (libopus port with PSRAM support)
// Spec: 16kHz mono, 20ms frames (320 samples), 16kbps VoIP
// Matches server-side opus_codec.py constants

#include <cstdint>
#include <cstddef>

#define OPUS_SAMPLE_RATE  16000
#define OPUS_CHANNELS     1
#define OPUS_FRAME_MS     20
#define OPUS_FRAME_SAMPLES 320
#define OPUS_FRAME_BYTES  640
#define OPUS_BITRATE      48000

// Init encoder + decoder. Allocate state from PSRAM.
// Returns true on success.
bool opus_stub_init();

// Encode one 20ms frame (320 int16 samples → Opus packet).
// Returns encoded packet length, or negative on error.
int opus_encode_frame(const int16_t* pcm_320, uint8_t* out, size_t out_cap);

// Decode one Opus packet → 320 int16 samples.
// Returns number of samples decoded (should be 320), or negative on error.
int opus_decode_frame(const uint8_t* opus_data, size_t opus_len, int16_t* pcm_out_320);
