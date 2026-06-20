#pragma once
/**
 * FreeRTOS audio task for CoreS3 walkie-talkie.
 *
 * Runs on Core 1 and owns all real-time audio work:
 * - mic capture → Opus encode → outbound queue
 * - inbound PCM playback via audio_hal
 * - speaker stop on interrupt/session_end
 *
 * The main loopTask is only responsible for draining the outbound queue
 * and calling WebSocketsClient.sendBIN(), keeping loopTask stack lean.
 */

#include <Arduino.h>

// Maximum encoded Opus packet size we ever produce. 20 ms @ 48 kbps averages
// ~120 B but VBR bursts exceed 128, so 128 silently truncated quality. 256 has
// ample headroom for any 20 ms VBR frame at this bitrate.
static constexpr size_t AUDIO_MAX_OPUS_PACKET = 256;

// Drop counters (queue-full events). Surfaced in the heartbeat so silent loss is
// visible. Read-only for callers.
uint32_t audio_task_pcm_drops();
uint32_t audio_task_outbound_drops();

// Initialize queues. Call once from setup().
bool audio_task_init();

// Start the audio task pinned to Core 1.
bool audio_task_start();

// Queue a decoded PCM frame for playback.
void audio_task_play_pcm(const int16_t* pcm, size_t samples);

// Truncate any queued/pending TTS audio. Returns true if the command was queued.
bool audio_task_stop_output();

// Enable/disable mic capture. Returns true only if the command was accepted into
// the (dedicated, non-starvable) control queue. The caller MUST NOT treat the mic
// as (dis)enabled on a false return — the gate would desync from real mic state.
bool audio_task_set_mic_enabled(bool enabled);

// Drain one encoded outbound packet. Called from loopTask only.
// Returns true if a packet was copied into `out`/`out_len`.
bool audio_task_get_outbound_packet(uint8_t* out, size_t* out_len, TickType_t wait = 0);
