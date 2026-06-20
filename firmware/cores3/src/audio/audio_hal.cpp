#include "audio_hal.h"
#include "opus_stub.h"

#include <M5Unified.h>          // M5.In_I2C for AW88298/ES7210 codec bring-up
#include <driver/i2s.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>

// CoreS3 internal audio uses a single I2S port for speaker + mic.
// Speaker and mic share MCLK/BCK/WS but have separate data pins.
// MCLK (master clock): GPIO 0 — required by ES7210/AW88298 (matches M5Unified
//   mic_cfg.pin_mck, M5Unified.cpp:2027). Omitting it leaves the ADC without a
//   master clock and mic samples are garbage.
// AW88298 amp:  DOUT = GPIO 13
// Internal mic: DIN  = GPIO 14
//
// NOTE on I2S port: this raw HAL owns I2S_NUM_0. M5Unified's own CoreS3 path
// uses I2S_NUM_1 (M5Unified.cpp:2031/2220) on the SAME physical pins, so the
// invariant is "exactly one runtime I2S driver on these pins" — never call
// M5.Speaker.begin()/M5.Mic.begin() anywhere while this HAL is active.
static constexpr gpio_num_t CORES3_I2S_MCK  = GPIO_NUM_0;
static constexpr gpio_num_t CORES3_I2S_BCK  = GPIO_NUM_34;
static constexpr gpio_num_t CORES3_I2S_WS   = GPIO_NUM_33;
static constexpr gpio_num_t CORES3_I2S_DOUT = GPIO_NUM_13;
static constexpr gpio_num_t CORES3_I2S_DIN  = GPIO_NUM_14;
static constexpr i2s_port_t CORES3_I2S_PORT = I2S_NUM_0;

// Codec I2C addresses on the internal bus (M5.In_I2C). Mirror M5Unified.cpp:416-417.
static constexpr uint8_t AW88298_ADDR = 0x36;  // speaker amp, 16-bit regs (byte-swapped!)
static constexpr uint8_t AW9523_ADDR  = 0x58;  // GPIO expander (amp power)
static constexpr uint8_t ES7210_ADDR  = 0x40;  // mic ADC, 8-bit regs

// MCLK = 256 * sample_rate (4.096 MHz @ 16 kHz) — standard ratio for these codecs.
static constexpr uint32_t CORES3_MCLK_HZ = OPUS_SAMPLE_RATE * 256;

// 4 buffers * 320 samples = 1280 mono samples = 2560 bytes (~80 ms @ 16 kHz mono).
static constexpr size_t I2S_DMA_BUF_COUNT = 4;
static constexpr size_t I2S_DMA_BUF_LEN   = OPUS_FRAME_SAMPLES;

static bool g_hal_initialized = false;
static bool g_mic_running = false;

// ---------------------------------------------------------------------------
// Codec bring-up (replaces M5Unified's begin()-gated enable callbacks).
// The AW88298 and ES7210 are configured ONLY inside M5Unified's
// _speaker_enabled_cb_cores3 / _microphone_enabled_cb_cores3, which fire from
// Speaker.begin()/Mic.begin() — calls this HAL deliberately bypasses. So the
// HAL must write these registers itself or the amp stays muted and the ADC is
// unconfigured (silence both directions).
// ---------------------------------------------------------------------------

// AW88298 registers are 16-bit and MUST be byte-swapped before the I2C write
// (M5Unified.cpp:419-423 does __builtin_bswap16 — sending native little-endian
// corrupts every register and the amp never un-mutes).
static bool aw88298_write(uint8_t reg, uint16_t val) {
    val = __builtin_bswap16(val);
    return M5.In_I2C.writeRegister(AW88298_ADDR, reg, (const uint8_t*)&val, 2, 400000);
}

// ES7210 registers are 8-bit; no byte swap (M5Unified.cpp:425-428).
static bool es7210_write(uint8_t reg, uint8_t val) {
    return M5.In_I2C.writeRegister(ES7210_ADDR, reg, &val, 1, 400000);
}

// Mirror _speaker_enabled_cb_cores3 enable branch (M5Unified.cpp:465-479).
static bool aw88298_enable(uint32_t sample_rate) {
    bool ok = M5.In_I2C.bitOn(AW9523_ADDR, 0x02, 0b00000100, 400000);  // amp power on
    static constexpr uint8_t rate_tbl[] = {4, 5, 6, 8, 10, 11, 15, 20, 22, 44};
    size_t reg0x06 = 0;
    size_t rate = (sample_rate + 1102) / 2205;
    while (rate > rate_tbl[reg0x06] && ++reg0x06 < sizeof(rate_tbl)) {}
    reg0x06 |= 0x14C0;                          // I2SBCK ratio (16*2)
    ok &= aw88298_write(0x61, 0x0673);          // boost mode disabled
    ok &= aw88298_write(0x04, 0x4040);          // I2SEN=1 AMPPD=0 PWDN=0
    ok &= aw88298_write(0x05, 0x0008);          // RMSE=0 HAGCE=0 HDCCE=0 HMUTE=0
    ok &= aw88298_write(0x06, (uint16_t)reg0x06);
    ok &= aw88298_write(0x0C, 0x0064);          // volume (full)
    return ok;
}

// Mirror _microphone_enabled_cb_cores3 enable branch (M5Unified.cpp:738-781).
static bool es7210_enable() {
    bool ok = es7210_write(0x00, 0xFF);         // RESET_CTL
    static const uint8_t seq[][2] = {
        {0x00, 0x41}, {0x01, 0x1f}, {0x06, 0x00}, {0x07, 0x20}, {0x08, 0x10},
        {0x09, 0x30}, {0x0A, 0x30}, {0x20, 0x0a}, {0x21, 0x2a}, {0x22, 0x0a},
        {0x23, 0x2a}, {0x02, 0xC1}, {0x04, 0x01}, {0x05, 0x00}, {0x11, 0x60},
        {0x40, 0x42}, {0x41, 0x70}, {0x42, 0x70}, {0x43, 0x1B}, {0x44, 0x1B},
        {0x45, 0x00}, {0x46, 0x00}, {0x47, 0x00}, {0x48, 0x00}, {0x49, 0x00},
        {0x4A, 0x00}, {0x4B, 0x00}, {0x4C, 0xFF}, {0x01, 0x14},
    };
    for (auto& r : seq) ok &= es7210_write(r[0], r[1]);
    return ok;
}

bool audio_hal_init() {
    if (g_hal_initialized) return true;

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = OPUS_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = I2S_DMA_BUF_COUNT,
        .dma_buf_len = I2S_DMA_BUF_LEN,
        .use_apll = true,                         // APLL required for fixed_mclk to take effect
                                                  // (and gives an accurate audio clock for the ES7210)
        .tx_desc_auto_clear = true,
        .fixed_mclk = (int)CORES3_MCLK_HZ,        // 256 * SR clean master clock for ES7210/AW88298
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan = I2S_BITS_PER_CHAN_16BIT,
    };

    esp_err_t err = i2s_driver_install(CORES3_I2S_PORT, &i2s_config, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[audio] i2s_driver_install failed: %d\n", err);
        return false;
    }

    i2s_pin_config_t pin_config = {
        .mck_io_num = CORES3_I2S_MCK,     // MCLK on GPIO0 — REQUIRED for the codecs
        .bck_io_num = CORES3_I2S_BCK,
        .ws_io_num = CORES3_I2S_WS,
        .data_out_num = CORES3_I2S_DOUT,
        .data_in_num = CORES3_I2S_DIN,
    };

    err = i2s_set_pin(CORES3_I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[audio] i2s_set_pin failed: %d\n", err);
        i2s_driver_uninstall(CORES3_I2S_PORT);
        return false;
    }

    // Clock — Route A (legacy driver): the i2s_config above is the single source
    // of truth for the clock. With use_apll=true + fixed_mclk=256*SR the APLL
    // drives a continuous, accurate MCLK on GPIO0 (set in pin_config) regardless
    // of TX activity, which the ES7210/AW88298 require. channel_format=
    // RIGHT_LEFT already establishes stereo (mic on one slot, speaker duplicated
    // to both), so a post-install i2s_set_clk() is NOT called — it is redundant
    // and, in the legacy driver, can recompute and disturb the fixed MCLK.
    // If on-device mic audio is pitch-shifted or noisy, escalate to Route B:
    // port M5Unified's calcClockDiv (Mic_Class.cpp:442-447, div_m>=8) or migrate
    // the mic path to the new driver/i2s_std.h with i2s_std_clk_config_t.

    // Codec bring-up — without these the amp stays muted and the ADC is silent.
    if (!aw88298_enable(OPUS_SAMPLE_RATE)) {
        Serial.println("[audio] AW88298 codec init FAILED (I2C)");
        i2s_driver_uninstall(CORES3_I2S_PORT);
        return false;
    }
    // ES7210 is configured ONCE here and left powered (configure-once design).
    // Deliberate trade-off: re-running the 29-register sequence on every PTT
    // press (to "power down" between turns) would add I2C churn and pop risk for
    // only a small battery saving. The mic does NOT leak into the audio stream
    // when idle — g_mic_running gates i2s_read, so no frames are captured during
    // playback (the hard-gate holds at the stream level). Revisit only if the
    // Phase 5 battery soak shows the always-on ADC bias draw matters.
    if (!es7210_enable()) {
        Serial.println("[audio] ES7210 codec init FAILED (I2C)");
        i2s_driver_uninstall(CORES3_I2S_PORT);
        return false;
    }

    g_hal_initialized = true;
    g_mic_running = false;
    Serial.printf("[audio] full-duplex I2S init OK: port=%d sr=%d mclk=%u\n",
                  CORES3_I2S_PORT, OPUS_SAMPLE_RATE, (unsigned)CORES3_MCLK_HZ);
    return true;
}

bool audio_hal_start_mic() {
    if (!g_hal_initialized) return false;
    g_mic_running = true;
    return true;
}

void audio_hal_stop_mic() {
    g_mic_running = false;
}

bool audio_hal_is_mic_running() {
    return g_mic_running;
}

size_t audio_hal_read_mic(int16_t* buf, size_t samples) {
    if (!g_mic_running || !buf || samples == 0) return 0;
    if (samples > OPUS_FRAME_SAMPLES) samples = OPUS_FRAME_SAMPLES;  // clamp: stereo_buf is fixed-size

    // Each stereo frame is 4 bytes (L+R 16-bit). The ES7210 is configured with
    // MIC1/MIC2 active (MIC3/4 powered down) and M5Unified treats CoreS3 mic as
    // input_stereo, downmixing by AVERAGING both slots (Mic_Class.cpp:658), not
    // by picking one. We mirror that: averaging is robust to which physical slot
    // the mic lands on and matches vendor level. (If on-device the two slots
    // differ wildly, the MIC_DEBUG path in Phase 3 logs per-slot RMS.)
    size_t bytes_to_read = samples * sizeof(int32_t);
    int32_t stereo_buf[OPUS_FRAME_SAMPLES];

    // Short timeout (~1 frame) so a quiet mic never stalls the audio task long
    // enough to starve the PLAY_PCM drain or delay a SET_MIC_ENABLED(false) event.
    size_t bytes_read = 0;
    esp_err_t err = i2s_read(CORES3_I2S_PORT, stereo_buf, bytes_to_read, &bytes_read, pdMS_TO_TICKS(5));
    if (err != ESP_OK) {
        Serial.printf("[audio] i2s_read failed: %d\n", err);
        return 0;
    }

    size_t frames_read = bytes_read / sizeof(int32_t);
#ifdef MIC_DEBUG_SLOTS
    // Build with -DMIC_DEBUG_SLOTS to confirm WHICH physical slot carries the mic.
    // Logs per-slot RMS for the first ~50 frames after capture starts. If one slot
    // is ~0 and the other has signal, switch from averaging to that slot.
    static uint32_t dbg_frames = 0;
    uint64_t sum_lo = 0, sum_hi = 0;
#endif
    for (size_t i = 0; i < frames_read; ++i) {
        int16_t lo = (int16_t)(stereo_buf[i] & 0xFFFF);   // first/left slot
        int16_t hi = (int16_t)(stereo_buf[i] >> 16);      // second/right slot
        buf[i] = (int16_t)(((int32_t)lo + (int32_t)hi + 1) >> 1);  // average (vendor-equivalent)
#ifdef MIC_DEBUG_SLOTS
        sum_lo += (uint32_t)((int32_t)lo * lo);
        sum_hi += (uint32_t)((int32_t)hi * hi);
#endif
    }
#ifdef MIC_DEBUG_SLOTS
    if (frames_read && dbg_frames < 50) {
        dbg_frames++;
        Serial.printf("[mic] rms_lo=%lu rms_hi=%lu\n",
                      (unsigned long)(sum_lo / frames_read),
                      (unsigned long)(sum_hi / frames_read));
    }
#endif
    return frames_read;
}

bool audio_hal_play_pcm(const int16_t* pcm, size_t samples) {
    if (!g_hal_initialized || !pcm || samples == 0) return false;
    if (samples > OPUS_FRAME_SAMPLES) return false;  // guard: stereo_buf is fixed-size, never overflow

    // Duplicate mono sample to both left and right slots for the AW88298.
    int32_t stereo_buf[OPUS_FRAME_SAMPLES];
    for (size_t i = 0; i < samples; ++i) {
        stereo_buf[i] = ((int32_t)pcm[i] << 16) | (uint16_t)pcm[i];
    }

    size_t bytes_written = 0;
    size_t bytes_to_write = samples * sizeof(int32_t);
    esp_err_t err = i2s_write(CORES3_I2S_PORT, stereo_buf, bytes_to_write, &bytes_written, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        Serial.printf("[audio] i2s_write failed: %d\n", err);
        return false;
    }
    return bytes_written == bytes_to_write;
}

void audio_hal_stop_speaker() {
    if (!g_hal_initialized) return;
    // Zero the TX DMA buffer to truncate pending TTS without stopping the
    // shared I2S clock, so mic RX keeps running.
    i2s_zero_dma_buffer(CORES3_I2S_PORT);
}
