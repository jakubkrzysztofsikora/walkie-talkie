---
date: 2026-06-21
commit: 170bd31
branch: feature/enable-mic
ticket: null
status: IMPLEMENTED — on-device flash pending (USB-JTAG wedged, needs BOOT-button reflash)
---
# Plan: Fix Agent Speech Distortion (LoopTask Decode Contention)

## STATUS UPDATE (2026-06-22, implemented autonomously overnight)
Implemented exactly as planned, with TDD:
- `opus_ring.h` — lock-free SPSC raw-Opus ring (12 packets ≈ 240ms), evict-oldest.
- `test_opus_ring` — 7 host tests written FIRST (watched fail), then implemented.
- `audio_task.cpp` — decode moved INTO the audio task; `audio_task_push_opus`,
  `audio_task_opus_drops`; ring init in `audio_task_init`.
- `main.cpp` ws_handler BIN — pushes the raw packet (no inline decode); removed
  throwaway `[tts]` debug; `opus_drop` added to the heartbeat.
- `platformio.ini` native env — `-I src` so the ring is host-testable.

Verification done: 52/52 native tests pass; `pio run -e m5stack-cores3`
SUCCESS; `audio_task_push_opus` / `audio_task_opus_drops` symbols linked in
firmware.elf. Committed `170bd31`, pushed to origin/feature/enable-mic.

**NOT YET verified on hardware** — the CoreS3 USB-JTAG wedged into a boot-loop
during repeated flashing and would not accept a new image over the auto-
bootloader (DTR/RTS) sequence. It needs a manual **hold-BOOT + replug** then
`bash firmware/cores3/scripts/deploy_to_cores3.sh --flash-only` (or the
115200-baud esptool command in scripts). The device is currently running the
PRIOR build (commit e3acd4c, "audio audible but garbled") — not bricked.

### Morning checklist
1. Hold BOOT on the CoreS3, replug USB-C, release after 2s.
2. Flash `170bd31`: from laptop run the esptool 115200 command (bootloader.bin
   @0x0, partitions.bin @0x8000, firmware.bin @0x10000) on the Studio, OR
   `deploy_to_cores3.sh --flash-only`.
3. Hold PTT, ask a question, release. Expect: **clear, intelligible speech end
   to end**, and `opus_drop=0` (or ≤2) in the `[idle]` heartbeat.
4. If still garbled: the ring is 12 frames — bump `OPUS_RING_CAPACITY` and check
   `opus_drop`. If `opus_drop` stays 0 but audio still bad, the issue is NOT
   queue starvation and the I2S path needs re-examination.
5. Backend (Studio `~/walkie-talkie`) still has 9 temporary `CORES3_DEBUG`
   print()s in `walkie_agent/cores3_session.py` — harmless, remove when done.

## Summary
Agent speech sounds like "hey Gniewko, Zi... a e ae u ya prprprprppr" — the
first few words are partially clear, then it degrades into garbled noise. Root
cause diagnosed live on-device: the PCM queue drops frames (59 drops in one
session) because the loopTask (WebSocket receive + Opus decode + UI rendering)
can't feed the queue faster than the audio_task drains it. Skipping the LCD
render during sessions didn't help — the bottleneck is the decode+enqueue path
living on a contended, multi-purpose priority-1 task. Fix: split the decode and
playback onto a single dedicated audio I/O task with a ring buffer, removing
loopTask from the audio hot path entirely.

## Research References
- Live on-device diagnostics (2026-06-21 session): all 19 `i2s_write` calls
  returned `ok`, zero timeouts — ruling out the i2s_write-blocking-cascade
  theory. But `pcm_drop=59` over one TTS reply — the loopTask can't keep up.
  Skipping the LCD `pushSprite` during the session had *no effect* on the
  distortion (the bottleneck is not the LCD SPI transfer).
- Four-agent research investigation (2026-06-21): eliminated bitrate mismatch,
  PXA1 magic overhead, DMA underrun looping, and `tx_desc_auto_clear` as causes.
  Converged on PCM queue starvation from loopTask contention.
- Git history: commit `260968a` ("ring-buffer speaker output") had the same
  approach — pre-buffered 200ms of TTS before playing, explicitly to avoid DMA
  underrun. It was removed in `99d6494` when the codebase switched to the
  raw-HAL architecture. Same root cause, same fix direction.
- `thoughts/shared/research/2026-06-20-cores3-half-duplex-mic-speaker.md` (audio
  architecture research)

## Verified hardware facts
- I2S DMA ring: 4 buffers × 320 stereo samples = 80ms headroom at 16kHz.
  `tx_desc_auto_clear=true` → underrun outputs silence, not repeated data.
- PCM queue: 32 slots × ~644 bytes = ~20KB in SRAM/PSRAM, linked as
  `g_pcm_queue`. 640ms of audio at 20ms/frame.
- `i2s_write` with 40ms timeout (reduced from 100ms) — never timed out in
  testing, confirming the write path is not the bottleneck.
- Opus decode (~30ms/frame fixed-point) + WebSocket frame assembly + JSON
  deserialization all compete on priority-1 loopTask with UI and keepalive.

## Root cause (confirmed on device)
```
loopTask (prio 1, Core 0):
  ws.loop() → ws_handler (WStype_BIN) → opus_decode → xQueueSend(g_pcm_queue, ..., 0)
  + ui_engine_frame() (skip pushSprite didn't help)
  + keepalive JSON + touch poll + outbound drain

audio_task (prio 2, Core 1):
  while(xQueueReceive(g_pcm_queue, ...)) { audio_hal_play_pcm(); }

audio_task drains faster than loopTask feeds → g_pcm_drops → Opus decoder gets
non-consecutive frames without PLC signaling → state corruption → garbled noise
```

---

## Phase 1: Dedicated audio I/O task with ring buffer

Replace the split (loopTask decodes + audio_task plays) with ONE task that does
both: receive the Opus frame over a small queue/flag, decode in-place, and push
into a ring buffer that the I2S DMA reads directly. Remove loopTask from the
audio playback hot path entirely. LoopTask only has to `xQueueSend` a raw WS
payload pointer — no decode, no queue flooding.

### Architecture
```
loopTask:
  ws_handler WStype_BIN → xQueueSend(g_ws_audio_queue, &payload_ref, 0)

audio_io_task (Core 1, prio 2, 32KB stack):
  while(1):
    drain g_ws_audio_queue (non-blocking) → opus_decode → write to RING BUFFER
    if ring buffer has space: fill next DMA buffer directly (avoid xQueueSend)
    if ring buffer low watermark: decode more aggressively
    vTaskDelay(1ms)
```

### Ring buffer design
Same concept as the old `260968a` ring buffer (200ms), but simpler: a single
`int16_t ring[OPUS_FRAME_SAMPLES * BUF_COUNT]` in PSRAM, with a write head
(audio_io_task fills) and a read-into-DMA path (audio_io_task also feeds). Since
the same task does both decode and DMA feed, there's no cross-task ring buffer
synchronization — it's just a FIFO the task drains into `i2s_write` one frame
at a time, yielding between writes.

This is effectively the existing audio_task loop but with decode moved INTO the
task instead of cross-queue.

### Changes

#### File: `firmware/cores3/src/audio/audio_task.cpp` (restructured)
- **What**: Remove the `g_pcm_queue` between loopTask and audio_task. Add
  `g_ws_audio_queue` — a SMALL queue (depth 8) that carries the raw WS payload
  pointer + length (no decode, no copy of 640B PCM). The audio_io_task decodes
  these in its own context.
- **Where**: Replace `g_pcm_queue` / `PcmEvent` with `struct WsAudioEvent { const uint8_t* data; size_t len; }` (the WS payload is valid only during `ws_handler` — the task must copy the bytes before the next ws.loop() frees it! Use a small static buffer ring for raw Opus packets instead of pointers into the WS frame).
- **Correction**: the WS payload is ephemeral. Safer: a small raw-Opus ring buffer (6 frames × ~128 bytes = ~768 bytes) that the loopTask copies into, and the audio_io_task decodes from.
- **Rationale**: loopTask only does memcpy + queue-send; the heavy decode + I2S feed runs on the dedicated task at higher priority. No cross-task PCM queue to overflow.

#### File: `firmware/cores3/src/main.cpp`
- **What**: In `ws_handler` `WStype_BIN`, replace `opus_decode_frame + audio_task_play_pcm` with `audio_task_push_opus(payload, len)` — a small copy into the raw-Opus ring buffer.
- **Where**: `main.cpp:139-146` (the current decode+enqueue block, which survives after the PXA1 check).
- **Code sketch**:
  ```cpp
  if (amode != AUDIO_TALK && (state == IDLE || state == SESSION_ACTIVE)) {
      if (amode != AUDIO_LISTEN) audio_set_mode(AUDIO_LISTEN);
      if (amode == AUDIO_LISTEN) {
          audio_task_push_opus(payload, len);  // just copy + signal, no decode
      }
  }
  ```

#### File: `firmware/cores3/src/audio/audio_task.h`
- **What**: Replace `audio_task_play_pcm` with `audio_task_push_opus`. Remove deprecated API surfaces.

#### File: `firmware/cores3/src/audio/opus_stub.cpp`
- **What**: No changes — the decoder instance is now used exclusively from the
  audio_io_task (single-threaded, no locking needed). Ensure `opus_decode_frame`
  is callable from there (it is — the decoder state is file-scope `g_decoder`).

### Success Criteria
#### Automated
- [ ] `pio run -e m5stack-cores3` SUCCESS
- [ ] `pio test -e native` all pass (existing tests — audio_task is device-only)
- [ ] `nm firmware.elf | grep audio_task_push_opus` — symbol linked
#### Manual (hardware)
- [ ] Hold PTT, talk, release — **agent replies clearly**, fully intelligible
- [ ] `pcm_drop` in heartbeat stays ≤ 2 over a full reply (the raw-Opus ring may still drop if burst >6 frames, but a 20ms frame at 50fps = 120ms headroom is generous for the new path)
- [ ] No regression: mic uplink still works, loopback test produces clear playback

### Dependencies — Requires: nothing · Blocks: nothing

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Raw-Opus ring too small, drops persist | Med | Med | Size at 12 frames (240ms) push-queue; the heavy work (decode+play) is now on the dedicated task, so loopTask only copies 128-byte packets — should keep up |
| Opus decoder state corruption if raw-Opus ring drops (same PLC issue) | Med | Med | The audio_io_task decodes sequentially from its own ring; if a frame is dropped, feed NULL to `opus_decode` for PLC concealment (proper masking) rather than just skipping |
| I2S underrun if decode is CPU-bound on Core1 | Low | Med | The decode cost is ~30ms fixed-point per 20ms frame at 16kHz — headroom exists; the dedicated task with prio 2 ensures it preempts loopTask |
| Memory: raw-Opus ring + existing PCM queue double the audio buffer cost | Low | Low | The small raw-Opus ring (~1536 bytes) replaces the 20KB PCM queue; net memory win |
| Cross-task `g_decoder` access — needs a lock if loopTask ever calls it | Low | High | After this change, ONLY the audio_io_task calls opus_decode/opus_encode. No lock needed. Document the invariant. |

## Rollback Strategy
Confined to `audio_task.cpp/h` and `main.cpp` WStype_BIN handler. Reverting
these two files restores the split decode/playback architecture. Committed
incrementally — one commit for the raw-Opus ring + audio_io_task, one for the
main.cpp wiring.

## File Ownership Summary
| File | Phase | Change |
|------|-------|--------|
| `firmware/cores3/src/audio/audio_task.cpp` | 1 | Restructure (raw-Opus ring, decode ownership) |
| `firmware/cores3/src/audio/audio_task.h` | 1 | API: `audio_task_push_opus` replaces `audio_task_play_pcm` |
| `firmware/cores3/src/main.cpp` | 1 | `ws_handler` BIN path: copy+enqueue, remove decode |
| `firmware/cores3/src/audio/audio_hal.cpp` | 1 | (revert i2s_write timeout 40→100ms — the YIELD+timeout change was diagnostic, not the fix) |
