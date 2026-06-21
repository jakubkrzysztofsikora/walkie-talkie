#pragma once
/**
 * Audio HAL for CoreS3 walkie-talkie.
 *
 * Uses the ESP-IDF I2S driver directly to create one full-duplex channel on
 * I2S0. Speaker (GPIO 13) and mic (GPIO 14) share BCK/WS (GPIO 34/33) but
 * have separate data lines. Bypassing M5Unified's Speaker/Mic classes avoids
 * the begin/end churn that caused crashes on the shared I2S bus.
 */

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

// Initialize speaker + mic once. Returns true on success.
bool audio_hal_init();

// Mic control (only gates recording; I2S channel stays alive).
bool audio_hal_start_mic();
void audio_hal_stop_mic();
bool audio_hal_is_mic_running();

// Read up to `samples` 16-bit mono samples from the mic.
// Returns the number of samples actually read.
size_t audio_hal_read_mic(int16_t* buf, size_t samples);

// Speaker control.
bool audio_hal_play_pcm(const int16_t* pcm, size_t samples);
void audio_hal_stop_speaker();
