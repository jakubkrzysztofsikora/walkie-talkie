---
date: 2026-06-20
commit: 3dc68c0f38afd4789ab40188ef18a37c2f0b4275
branch: feature/enable-mic
ticket: null
status: implemented
revision: 2
---
# Plan: Two-Way Half-Duplex PTT Audio on CoreS3 (Option C) — v2

## Revision note
v2 incorporates an adversarial review (Codex, 2026-06-20) of v1, with every
claim re-verified against the vendored M5Unified source. Confirmed defects in
v1 that this revision fixes:
- **AW88298 writes were byte-swapped wrong** — M5Unified does `__builtin_bswap16(value)` before the I2C write (`M5Unified.cpp:421`); v1's sketch sent native little-endian → amp config garbage → no sound. **Fixed in Phase 1.**
- **I2S-port facts were wrong** — M5Unified configures CoreS3 mic/speaker on **`I2S_NUM_1`** (`M5Unified.cpp:2031` mic, `:2220` spk), not I2S0. v1's "second `i2s_driver_install` on I2S0 → `ESP_ERR_INVALID_STATE`" collision rationale was false. **Corrected in Phase 2.**
- **MCLK omitted** — M5Unified drives an **MCLK on GPIO0** for CoreS3 (`mic_cfg.pin_mck = GPIO_NUM_0`, `:2027`); the raw HAL's pin config never sets it. The ES7210 needs MCLK (hence the `div_m≥8` accuracy note at `Mic_Class.cpp:442`). **Addressed in Phase 1.**
- **The "mic muted during playback" invariant was not actually enforced** — v1 only disabled the mic on PTT *release*, never when TTS arrived. **Fixed by an explicit talk/listen state machine in Phase 3.**
- **Session_started-after-release race promoted from out-of-scope to a Phase 3 fix** (it is load-bearing for "mic only while held").
- Queue-drop visibility, Opus packet cap, and I2C error handling added.


## Implementation status (2026-06-20)
All 5 phases implemented on `feature/enable-mic`, commits b03bec9..3a905cf. Each phase: built on the Mac Studio (pio), dual adversarial review (Codex + review subagent — Codex covered P1-P3; its P4/P5 runs hung and were killed, so P4/P5 had subagent review only), fixes folded, re-built, on-device boot-verified.

**Verified (software + on-device boot/idle):** firmware builds clean; native tests 19/19; on device — `[audio] full-duplex I2S init OK mclk=4096000`, codec I2C ACKed, audio task on core 1, stack_hwm=13264B free of 16384, reset_reason clean, 90s idle watch with 0 reboots / stable heap / pcm_drop=0 out_drop=0.

**NOT yet verified — requires hardware + live backend (genuine handoff):**
- Audible: TTS actually plays through the speaker (proves the AW88298 byte-swap + MCLK on the TX path end-to-end).
- Mic: ES7210 capture reaches the server; correct slot (build `-DMIC_DEBUG_SLOTS` to log per-slot RMS).
- Hard-gate: no echo across talk→listen on real audio.
- Soak: 100+ PTT cycles + 1-hour + a battery run. NOTE: the old `stress` serial command was removed in the P2/P3 main.cpp rewrite — `scripts/stress_ptt.py` needs that command re-added (or drive PTT by hand) before it will work.

## Summary
Finish the raw-ESP-IDF audio HAL so the CoreS3 mic captures while the speaker still plays TTS, without the reboots/freezes that forced the current speaker-only SAFE_MODE. The raw HAL becomes the **sole runtime owner** of an I2S controller **and** of the AW88298/ES7210 codec I2C bring-up; `main.cpp` cuts over to it; an explicit talk/listen state machine guarantees mic and speaker are never active together.

## Research References
- `thoughts/shared/research/2026-06-20-cores3-half-duplex-mic-speaker.md` (root cause, codec-ownership trap)
- `thoughts/shared/research/2026-06-17-cores3-emulation-tools.md` (validation strategy)
- Adversarial review: `codex exec` run 2026-06-20 (findings folded into this revision)

## Decisions (locked with user, 2026-06-20)
- **Half-duplex semantics: hard-gate.** Mic and speaker are mutually exclusive in software. Enforced by the Phase 3 state machine, not by hope.
- **Full cutover, no build flag.** SAFE_MODE is replaced. Revert = git checkout + reflash on `feature/enable-mic`.

## Verified hardware/source facts (re-checked in v2)
- Codec I2C addrs on `M5.In_I2C`: AW88298=`0x36` (16-bit regs, **byte-swapped**), AW9523=`0x58`, ES7210=`0x40` (8-bit regs). `M5Unified.cpp:416-427`.
- AW88298 enable seq: `M5Unified.cpp:467-479`. ES7210 enable seq: `:738-781`.
- M5Unified CoreS3 audio uses **I2S_NUM_1**, MCLK=GPIO0, BCK=GPIO34, WS=GPIO33, mic DIN=GPIO14 / spk DOUT=GPIO13, `input_stereo`, spk magnification=4, mic magnification=2 (`M5Unified.cpp:2025-2033`, `:2216-2221`).
- `M5.begin()` only *registers* the enable callbacks (`:2611/:2616`); codec regs are written only by `Speaker/Mic.begin()`. Keep `M5.begin()`; never call the audio `begin()`s.
- `audio_task.h` already exposes the full API. `ws.sendBIN(const uint8_t*, size_t)` exists (`WebSocketsClient.h:98`).

---

## Phase 1: Correct codec bring-up + clock/MCLK in the raw HAL

Make `audio_hal_init()` configure both codecs itself, **correctly** (byte order, MCLK, error handling). Pure addition; not yet wired into `main.cpp`, so device behavior is unchanged this phase.

### Changes

#### File: `firmware/cores3/src/audio/audio_hal.cpp`
- **Add `#include <M5Unified.h>`** (for `M5.In_I2C`).

- **(C-1) AW88298 writes MUST byte-swap.** Mirror `M5Unified.cpp:419-423` exactly:
  ```cpp
  static constexpr uint8_t AW88298_ADDR = 0x36;
  static constexpr uint8_t AW9523_ADDR  = 0x58;
  static constexpr uint8_t ES7210_ADDR  = 0x40;

  static bool aw88298_write(uint8_t reg, uint16_t val) {
      val = __builtin_bswap16(val);                       // <-- the v1 bug fix
      return M5.In_I2C.writeRegister(AW88298_ADDR, reg, (const uint8_t*)&val, 2, 400000);
  }
  static bool es7210_write(uint8_t reg, uint8_t val) {     // ES7210 = 8-bit, no swap
      return M5.In_I2C.writeRegister(ES7210_ADDR, reg, &val, 1, 400000);
  }
  ```

- **(C-2) Codec enable, with error accumulation.** Mirror the enable branches; return false on any failed write:
  ```cpp
  static bool aw88298_enable(uint32_t sample_rate) {
      bool ok = M5.In_I2C.bitOn(AW9523_ADDR, 0x02, 0b00000100, 400000);   // amp power
      static constexpr uint8_t rate_tbl[] = {4,5,6,8,10,11,15,20,22,44};
      size_t reg06 = 0, rate = (sample_rate + 1102) / 2205;
      while (rate > rate_tbl[reg06] && ++reg06 < sizeof(rate_tbl)) {}
      reg06 |= 0x14C0;
      ok &= aw88298_write(0x61, 0x0673);
      ok &= aw88298_write(0x04, 0x4040);   // I2SEN=1 AMPPD=0 PWDN=0
      ok &= aw88298_write(0x05, 0x0008);
      ok &= aw88298_write(0x06, (uint16_t)reg06);
      ok &= aw88298_write(0x0C, 0x0064);   // volume (tune down for kids/brown-out)
      return ok;
  }
  static bool es7210_enable() {
      bool ok = es7210_write(0x00, 0xFF);  // RESET_CTL
      static const uint8_t seq[][2] = {
          {0x00,0x41},{0x01,0x1f},{0x06,0x00},{0x07,0x20},{0x08,0x10},
          {0x09,0x30},{0x0A,0x30},{0x20,0x0a},{0x21,0x2a},{0x22,0x0a},
          {0x23,0x2a},{0x02,0xC1},{0x04,0x01},{0x05,0x00},{0x11,0x60},
          {0x40,0x42},{0x41,0x70},{0x42,0x70},{0x43,0x1B},{0x44,0x1B},
          {0x45,0x00},{0x46,0x00},{0x47,0x00},{0x48,0x00},{0x49,0x00},
          {0x4A,0x00},{0x4B,0x00},{0x4C,0xFF},{0x01,0x14},
      };
      for (auto& r : seq) ok &= es7210_write(r[0], r[1]);
      return ok;
  }
  ```
  In `audio_hal_init()`, after `i2s_set_clk` succeeds:
  ```cpp
  if (!aw88298_enable(OPUS_SAMPLE_RATE)) { Serial.println("[audio] AW88298 init FAIL"); return false; }
  if (!es7210_enable())                  { Serial.println("[audio] ES7210 init FAIL");  return false; }
  ```

- **(C-3) MCLK + clock correctness.** This is the hard part Codex flagged; v1 punted it.
  - Set the MCLK pin explicitly in `i2s_pin_config_t` (line 48):
    ```cpp
    i2s_pin_config_t pin_config = {
        .mck_io_num  = GPIO_NUM_0,      // <-- MCLK to ES7210/AW88298, matches M5Unified (verify field name in your driver/i2s.h)
        .bck_io_num  = CORES3_I2S_BCK,
        .ws_io_num   = CORES3_I2S_WS,
        .data_out_num= CORES3_I2S_DOUT,
        .data_in_num = CORES3_I2S_DIN,
    };
    ```
  - Force a standard MCLK ratio so the ES7210 gets a clean master clock. In `i2s_config_t` set `.fixed_mclk = OPUS_SAMPLE_RATE * 256` (and, if the field exists in this IDF, `.mclk_multiple = I2S_MCLK_MULTIPLE_256`). 256×16 kHz = 4.096 MHz.
  - **Route decision (do A first, fall back to B):**
    - **Route A (recommended, low effort):** legacy `driver/i2s.h` + explicit MCLK pin + `fixed_mclk = 256×SR`. Try this; verify mic on hardware.
    - **Route B (fallback if mic is pitch-shifted/noisy):** port M5Unified's manual divider programming (`Mic_Class.cpp:442-447` `calcClockDiv`, `div_m=8`) or migrate the mic path to the new `driver/i2s_std.h` with `i2s_std_clk_config_t.mclk_multiple`. Higher effort; only if A fails.
  - Replace the v1 `TODO(hw)` with this concrete A/B note in code.

### Success Criteria
#### Automated
- [ ] Builds: `cd firmware/cores3 && pio run -e m5stack-cores3`
- [ ] Host tests pass: `pio test -e native`
- [ ] `grep -n "bswap16" src/audio/audio_hal.cpp` is present (regression guard for C-1)
- [ ] `grep -n "i2s_driver_uninstall" src/audio/audio_hal.cpp` returns nothing
#### Manual
- [ ] Code review: AW88298 swap, `reg06` loop, and ES7210 `seq[]` match `M5Unified.cpp:419-479`/`:738-781` byte-for-byte
- [ ] `mck_io_num` field name matches the installed `driver/i2s.h`

### Dependencies — Requires: nothing · Blocks: Phase 2

---

## Phase 2: Single I2S owner — cut `main.cpp` over to the raw HAL (speaker first)

Make the raw HAL the only runtime I2S driver. Prove **playback** works via the raw path before touching the mic.

### Changes
#### File: `firmware/cores3/src/main.cpp`
- Add `#include "audio/audio_hal.h"` and `#include "audio/audio_task.h"`.
- In `setup()`: keep `M5.begin(cfg)` and the `cfg.internal_*` flags (board detection + callback registration only). **Delete** the `M5.Speaker.config()/Mic.config()` block and `M5.Speaker.begin()` (lines ~234-236). After `opus_stub_init()`:
  ```cpp
  if (!audio_hal_init())  Serial.println("[audio] HAL init FAILED");
  if (!audio_task_init()) Serial.println("[audio] task init FAILED");
  if (!audio_task_start())Serial.println("[audio] task start FAILED");
  ```
- In `ws_handler` `WStype_BIN`: replace `M5.Speaker.playRaw(...)` with `audio_task_play_pcm(buf, n);`. Drop the unused `tts_playing/tts_last` statics.

- **Correct ownership rationale (replaces v1's wrong claim):** In a full cutover M5Unified audio is never `begin()`-ed, so its **I2S_NUM_1** config is never installed; the raw HAL on **I2S_NUM_0** is the only driver routing the shared BCK/WS/DATA pins. The risk of a stray `M5.Speaker.begin()`/`Mic.begin()` is a **GPIO pin conflict** (it would install I2S1 onto the same physical pins), *not* an `INVALID_STATE` on a re-installed I2S0. Hence the whole-firmware grep gate below.
  - Decision: keep the raw HAL on `I2S_NUM_0` (no measured reason to move); document that M5Unified would use `I2S_NUM_1`.

### Success Criteria
#### Automated
- [ ] Builds: `pio run -e m5stack-cores3`
- [ ] **Whole-firmware** grep clean: `grep -rn "M5.Speaker\|M5.Mic\|Speaker.begin\|Mic.begin\|Speaker.end\|Mic.end" firmware/cores3/src/` returns nothing
- [ ] Host tests pass: `pio test -e native`
#### Manual (real hardware)
- [ ] Boots to IDLE, no panic
- [ ] **TTS plays through the speaker via the raw path** (this validates C-1 byte-swap + C-3 clock on the TX side before the mic is added)
- [ ] `heap`/`psram` stable over a 2-min session; `esp_reset_reason()` POWERON

### Dependencies — Requires: Phase 1 · Blocks: Phase 3

---

## Phase 3: Talk/Listen state machine — enforce the hard gate + fix the PTT race

Introduce an explicit audio mode so mic and speaker are provably never on together, and fix the session_started-after-release race. Mic capture is enabled here (it fills/drops the outbound queue harmlessly until Phase 4 wires the drain).

### Design
New `enum AudioMode { AUDIO_IDLE, AUDIO_TALK, AUDIO_LISTEN }` in `main.cpp`, driven by PTT + WS events. Invariant: `mic on ⇔ AUDIO_TALK`; playback only queued in `AUDIO_LISTEN`.

| Event | Guard | Action |
|---|---|---|
| `session_started` | `touch_pressed` (still held) | `AUDIO_TALK`; `audio_task_set_mic_enabled(true)` |
| `session_started` | **`!touch_pressed`** (already released — the race) | immediately `send_session_end()`; `AUDIO_IDLE` |
| `WStype_BIN` (TTS) | mode==`AUDIO_TALK` | `set_mic_enabled(false)` **first**, then `AUDIO_LISTEN`, then `audio_task_play_pcm()` |
| `WStype_BIN` (TTS) | mode==`AUDIO_LISTEN` | `audio_task_play_pcm()` |
| touch release | mode==`AUDIO_TALK` | `set_mic_enabled(false)`; `send_session_end()`; `AUDIO_IDLE` |
| touch release | mode==`AUDIO_LISTEN` | `send_session_end()`; let playback drain; `AUDIO_IDLE` |
| `session_ended` / WS disconnect | any | `set_mic_enabled(false)`; `AUDIO_IDLE` |

This closes Codex Critical #3 (mic muted whenever TTS plays) and Medium race (#fast-tap leaves backend active).

### Changes
- **`main.cpp`**: add `AudioMode mode`; implement the table above in `ws_handler` (`session_started` ~line 58, `WStype_BIN` ~line 62) and the touch handlers (~lines 266-276). Replace the bare `state=SESSION_ACTIVE` paths with mode transitions.
- **`audio_task.cpp`**: raise `audio_task` stack 8192 → 16384 (line 106) — Opus encode depth + `int32_t stereo_buf[320]` on-stack.
- **`audio_hal.cpp` mic channel**: the right-channel assumption (`stereo_buf[i] >> 16`, line 115) is unproven — M5Unified uses `input_stereo` and averages both slots (`Mic_Class.cpp:654`). Add a build-time `MIC_DEBUG_BOTH_SLOTS` path that logs per-slot RMS for the first N frames so the correct slot/average is chosen on hardware. Keep right-channel as default but make it one line to change.

### Success Criteria
#### Automated
- [ ] Builds; host tests pass
- [ ] Add a host-side test asserting the mode table: `session_started while !held → session_end`; `BIN while TALK → mic disabled before play`
#### Manual (real hardware)
- [ ] Hold PTT → mic high-water shows capture activity; release → activity stops
- [ ] TTS arriving **while finger still held** disables the mic (no echo recorded) — the core gate test
- [ ] Fast tap (press+release faster than RTT) does not leave the device stuck in active/playback
- [ ] Per-slot RMS log identifies the live mic slot

### Dependencies — Requires: Phase 2 · Blocks: Phase 4

---

## Phase 4: Outbound transport + queue/packet hardening

Send captured Opus to the server and make the queues observable/robust.

### Changes
- **`audio_task.h`**: raise `AUDIO_MAX_OPUS_PACKET` 128 → **256**. (Opus respects `out_cap`, so 128 was a *quality* cap, not an overflow — but 48 kbps 20 ms VBR peaks well past 128 bytes and was being silently truncated in quality.) Pass `out_cap = sizeof(pkt.data)`.
- **`audio_task.cpp`**: add `static uint32_t g_outbound_drops, g_event_drops;` incremented on each `xQueueSend(...,0)==errQUEUE_FULL` (lines 76/122/129/137). Expose a getter for the heartbeat. Bump inbound `EVENT_QUEUE_LEN` 16 → **32** for TTS burst headroom (keep `wait=0` — dropping beats blocking on a real-time path, but now it's counted).
- **`main.cpp`**: in `loop()` after `ws.loop()`, drain outbound (bounded) only in `AUDIO_TALK`:
  ```cpp
  if (mode == AUDIO_TALK) {
      uint8_t pkt[AUDIO_MAX_OPUS_PACKET]; size_t plen; int budget = 8;
      while (budget-- && audio_task_get_outbound_packet(pkt,&plen,0)) ws.sendBIN(pkt, plen);
  }
  ```
  Add `drops` to the heartbeat printf.

### Success Criteria
#### Automated
- [ ] Builds; host tests pass
#### Manual (real hardware)
- [ ] Hold-speak-release: **server receives Opus frames**; full round-trip reply plays back
- [ ] Idle: server receives zero audio frames
- [ ] Heartbeat shows `outbound_drops`/`event_drops` ≈ 0 in normal use; if non-zero, queue sizing is revisited

### Dependencies — Requires: Phase 3 · Blocks: Phase 5

---

## Phase 5: On-device hardening & soak

### Changes
- **`audio_task.cpp`**: log `esp_cpu_get_core_id()` once at task entry (expect 1); periodic `uxTaskGetStackHighWaterMark(NULL)` (~every 5 s).
- **`main.cpp`**: log `esp_reset_reason()` once in `setup()` (S3 brown-out reports as `rst:0x3` but reason is `ESP_RST_BROWNOUT`).

### Success Criteria (real hardware)
- [ ] **PTT stress** `scripts/stress_ptt.py` 100+ cycles — zero reboots
- [ ] **1-hour soak** — no reboot, reset reason stays POWERON, heap stable, drop counters bounded
- [ ] `audio_task` stack high-water ≥ 1 KB throughout (else raise to 20 KB)
- [ ] One run on **battery** (exclude USB brown-out)
- [ ] No audible echo across talk→listen cycles (gate confirmed end-to-end)

### Dependencies — Requires: Phase 4 · Blocks: nothing

---

## Risk Assessment (v2)

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| MCLK/clock-div wrong → mic pitch-shifted or noisy | **Med-High** | High | Phase 1 Route A (explicit MCLK + `fixed_mclk=256×SR`); Route B fallback (M5Unified `calcClockDiv` or `i2s_std`). Phase 3 RMS check. This is now the top technical risk. |
| AW88298 byte order regressed again | Low | High | Phase 1 `bswap16` grep gate; Phase 2 playback test catches it. |
| Mic channel/slot mapping wrong → silence/weak | Med | Med | Phase 3 per-slot RMS log; one-line switch to average/other slot. |
| Control latency: 100 ms `i2s_write` delays a STOP event | Low-Med | Med | Hard-gate means TX and RX never run together, so timeouts don't stack; reduce write timeout to ~40 ms (2 DMA buffers) and rely on `i2s_zero_dma_buffer` truncation. Revisit task split only if measured. |
| Stray `M5.Speaker/Mic.begin()` installs I2S1 on shared pins | Low | High | Phase 2 **whole-firmware** grep gate (not just main.cpp). |
| TTS burst > 32-deep queue → truncation | Low | Med | Phase 4 drop counters surface it; bump queue if seen. |
| Brown-out from amp inrush on USB | Low | High | Lower `0x0C` volume; Phase 5 battery soak; reset-reason log. |
| Mic/speaker quieter than M5Unified (no magnification: mic×2, spk×4) | Med | Low | Tuning note: optional digital gain in channel extract / PCM scale with clip guard. |
| `audio_task` stack tight at 16 KB | Low | High | Phase 5 high-water logging; raise to 20 KB. |

## Rollback Strategy
Confined to `feature/enable-mic`; `main` keeps SAFE_MODE. Each phase is a separate commit; `git revert` the Phase 2 cutover commit to restore speaker-only. Fast on-device safety: reflash the last `main` build via `scripts/flash.sh`. No server/protocol changes.

## Out of Scope
- **AEC / full-duplex barge-in** — excluded by the hard-gate decision (the Phase 3 state machine makes it unnecessary).
- `pschatzmann/arduino-audio-tools` in `platformio.ini` appears unused; not touched.

## File Ownership Summary
| File | Phase | Change |
|------|-------|--------|
| `src/audio/audio_hal.cpp` | 1, 3 | byte-swap, codec enable+error handling, MCLK/clock, mic-slot debug |
| `src/main.cpp` | 2, 3, 4, 5 | cutover, talk/listen state machine + race fix, drain, reset log |
| `src/audio/audio_task.cpp` | 3, 4, 5 | stack bump, drop counters, queue size, instrumentation |
| `src/audio/audio_task.h` | 4 | `AUDIO_MAX_OPUS_PACKET` 128→256 |
| `test/test_audio_state_machine/test_main.cpp` | 3 | mode-table assertions |
