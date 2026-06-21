#include "audio_task.h"
#include "audio_hal.h"
#include "opus_stub.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// ---------------------------------------------------------------------------
// Queues (loopTask → audio_task)
//
// TWO separate queues by design. The half-duplex hard-gate depends on a mic
// DISABLE never being lost; if control rode the same best-effort queue as the
// PCM flood, a TTS burst could fill the queue and drop the mic-off, leaving the
// mic hot during playback (echo). So control (SET_MIC_ENABLED / STOP_OUTPUT)
// gets its own small queue, is drained BEFORE PCM each iteration, and its sends
// use a real timeout and report success so the caller can refuse to advance the
// logical mode if the command was not accepted.
// ---------------------------------------------------------------------------

// Control: mic gate + output stop. Small but never starved by PCM.
struct ControlEvent {
    bool set_mic;        // true = this is a SET_MIC_ENABLED
    bool mic_enabled;    // payload for set_mic
    bool stop_output;    // true = flush speaker
};
static constexpr UBaseType_t CONTROL_QUEUE_LEN = 8;

// PCM playback frames (best-effort; dropping a TTS frame is a quality blip).
struct PcmEvent {
    int16_t pcm[OPUS_FRAME_SAMPLES];
    size_t samples;
};
static constexpr UBaseType_t PCM_QUEUE_LEN = 128;   // ~640ms TTS burst headroom

static QueueHandle_t g_control_queue = nullptr;
static QueueHandle_t g_pcm_queue = nullptr;
static QueueHandle_t g_outbound_queue = nullptr;
static TaskHandle_t g_audio_task_handle = nullptr;

// Drop counters (queue-full). volatile: written on audio_task/loopTask, read on
// loopTask for the heartbeat. 32-bit reads are atomic on ESP32-S3.
static volatile uint32_t g_pcm_drops = 0;
static volatile uint32_t g_outbound_drops = 0;

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------

struct OutboundPacket {
    uint8_t data[AUDIO_MAX_OPUS_PACKET];
    size_t len;
};

static void audio_task(void* /*pvParameters*/) {
    ControlEvent ctl;
    PcmEvent pevt;
    int16_t mic_accum[OPUS_FRAME_SAMPLES];
    size_t mic_accum_count = 0;
    OutboundPacket pkt;

    Serial.printf("[audio] task running on core %d\n", (int)xPortGetCoreID());
    TickType_t last_hw = 0;

    for (;;) {
        // Stack high-water watch (~every 5s): if this drops near 0 the 16KB stack
        // is too small. Logged so the soak can catch it before an overflow reboot.
        if (xTaskGetTickCount() - last_hw > pdMS_TO_TICKS(5000)) {
            last_hw = xTaskGetTickCount();
            Serial.printf("[audio] stack_hwm=%u bytes\n",
                          (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
        }
        // 1) Drain CONTROL first — a mic-off here must take effect before any
        //    queued PCM is played, so the mic is never hot during playback.
        while (xQueueReceive(g_control_queue, &ctl, 0) == pdTRUE) {
            if (ctl.stop_output) audio_hal_stop_speaker();
            if (ctl.set_mic) {
                if (ctl.mic_enabled) audio_hal_start_mic();
                else {
                    audio_hal_stop_mic();
                    mic_accum_count = 0;
                    // Flush any encoded frames stranded from this turn so they
                    // can't be prepended to the next utterance as stale audio.
                    xQueueReset(g_outbound_queue);
                }
            }
        }

        // 2) Then PCM playback (best-effort).
        while (xQueueReceive(g_pcm_queue, &pevt, 0) == pdTRUE) {
            audio_hal_play_pcm(pevt.pcm, pevt.samples);
        }

        // Capture mic if enabled. i2s_channel_read may return partial data,
        // so accumulate until we have a full 20 ms frame.
        if (audio_hal_is_mic_running()) {
            size_t need = OPUS_FRAME_SAMPLES - mic_accum_count;
            size_t got = audio_hal_read_mic(&mic_accum[mic_accum_count], need);
            mic_accum_count += got;
            if (mic_accum_count >= OPUS_FRAME_SAMPLES) {
                int len = opus_encode_frame(mic_accum, pkt.data, sizeof(pkt.data));
                if (len > 0) {
                    pkt.len = (size_t)len;
                    if (xQueueSend(g_outbound_queue, &pkt, 0) != pdTRUE) g_outbound_drops++;
                }
                mic_accum_count = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool audio_task_init() {
    if (g_control_queue) return true;

    g_control_queue  = xQueueCreate(CONTROL_QUEUE_LEN, sizeof(ControlEvent));
    g_pcm_queue      = xQueueCreate(PCM_QUEUE_LEN, sizeof(PcmEvent));
    g_outbound_queue = xQueueCreate(PCM_QUEUE_LEN, sizeof(OutboundPacket));

    return g_control_queue && g_pcm_queue && g_outbound_queue;
}

bool audio_task_start() {
    if (g_audio_task_handle) return true;
    if (!g_control_queue) return false;

    BaseType_t res = xTaskCreatePinnedToCore(
        audio_task,
        "audio_task",
        32768,  // stack — opus_encode (fixed-point CELT) is VERY deep; 16384 stack-
                // canary-panicked on the first encode (confirmed on-device). 32KB
                // gives headroom; watch [audio] stack_hwm to confirm.
        nullptr,
        2,     // priority higher than loopTask (1)
        &g_audio_task_handle,
        1      // Core 1
    );

    return res == pdPASS;
}

void audio_task_play_pcm(const int16_t* pcm, size_t samples) {
    if (!g_pcm_queue || !pcm || samples == 0) return;
    PcmEvent evt;
    evt.samples = (samples > OPUS_FRAME_SAMPLES) ? OPUS_FRAME_SAMPLES : samples;
    memcpy(evt.pcm, pcm, evt.samples * sizeof(int16_t));
    if (xQueueSend(g_pcm_queue, &evt, 0) != pdTRUE) g_pcm_drops++;  // best-effort: dropped TTS frame is a blip
}

uint32_t audio_task_pcm_drops()      { return g_pcm_drops; }
uint32_t audio_task_outbound_drops() { return g_outbound_drops; }

bool audio_task_stop_output() {
    if (!g_control_queue) return false;
    ControlEvent evt{false, false, true};
    return xQueueSend(g_control_queue, &evt, pdMS_TO_TICKS(20)) == pdTRUE;
}

// Returns true only if the command was ACCEPTED into the control queue. The
// caller (audio_set_mode) must NOT advance the logical mode on false, or the
// gate and the real mic state desync.
bool audio_task_set_mic_enabled(bool enabled) {
    if (!g_control_queue) return false;
    ControlEvent evt{true, enabled, false};
    // Real timeout: the control queue is small and never starved by PCM, so this
    // effectively always succeeds; the timeout is insurance, not a hot path.
    return xQueueSend(g_control_queue, &evt, pdMS_TO_TICKS(20)) == pdTRUE;
}

bool audio_task_get_outbound_packet(uint8_t* out, size_t* out_len, TickType_t wait) {
    if (!g_outbound_queue || !out || !out_len) return false;
    OutboundPacket pkt;
    if (xQueueReceive(g_outbound_queue, &pkt, wait) != pdTRUE) return false;
    if (pkt.len > AUDIO_MAX_OPUS_PACKET) pkt.len = AUDIO_MAX_OPUS_PACKET;  // never trust queued len (caller's out[] is this size)
    memcpy(out, pkt.data, pkt.len);
    *out_len = pkt.len;
    return true;
}
