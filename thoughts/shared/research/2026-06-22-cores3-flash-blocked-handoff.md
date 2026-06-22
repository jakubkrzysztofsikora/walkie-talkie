---
date: 2026-06-22
status: RESOLVED — flashed via chunked-flash workaround; device healthy on e3acd4c
branch: feature/enable-mic
---
# CoreS3 overnight handoff — USB-JTAG corruption SOLVED via chunked flash

## RESOLUTION (06:30–08:15)
The corrupting USB-JTAG link was beaten WITHOUT a cable replug. Key insight:
**small transfers stay clean, only bulk writes corrupt.** `flash_id` always
worked; full 1.6MB writes hit MD5 errors. Fix: split the app .bin into 128KB
chunks and flash each at its own offset — every chunk passed MD5 on the first
try.

The device is now flashed with `e3acd4c` (I2S clock fix; audio audible, clean at
sentence start) and verified booting cleanly: AW88298 PLLS=1, full-duplex I2S
init OK, WiFi+WS connected, healthy `[idle]` heartbeat, pcm_drop=0.

### The chunked-flash technique (reuse if USB corrupts again)
```bash
# On the Studio, with the device connected:
cd ~/walkie-talkie/firmware/cores3
split -b 131072 .pio/build/m5stack-cores3/firmware.bin /tmp/appchunk_
# wake into bootloader once:
esptool --before usb_reset --after no_reset flash_id
# then flash each piece with --before no_reset --after no_reset, retrying per chunk:
#   bootloader.bin @0x0, partitions.bin @0x8000,
#   appchunk_N @ (0x10000 + N*0x20000)
# finally: esptool --before usb_reset --after hard_reset run
```
Script used: `/tmp/chunk_flash_exec.sh` on the Studio.

## STILL OPEN: the audio distortion
The flash crisis is solved but the distortion bug is NOT. Device runs the
e3acd4c baseline: **audible, clear at the start of a sentence, "sped-up/garbled"
by the end.** Needs the user's ears to validate any fix (implementing blind
backfired once — see below). Leading hypothesis unchanged: DMA underrun on the
WebSocket TTS bursts. Next concrete step: a decoded-PCM pre-buffer that primes
~200ms before the first i2s_write and keeps the DMA continuously fed (the old
removed commit 260968a did exactly this). Do NOT flash an unvalidated guess —
the ring-buffer attempt (170bd31) made it worse and was reverted.

## Git state (all pushed to origin/feature/enable-mic)
- `e3acd4c` — I2S clock HW register fix. **This is the build to flash.** It made
  audio AUDIBLE (the big win). Speech is intelligible at the start of a sentence.
- `170bd31` — opus-ring decode-off-loopTask attempt. **REVERTED** — it made the
  distortion WORSE on device, and `opus_drop` stayed 0, which DISPROVES the
  "loopTask decode starvation" theory. (Kept in history for reference.)
- `ad16bc0`, `00f7146` — the reverts. HEAD is back at the e3acd4c behaviour.

The local `.pio` build on the Studio currently holds the e3acd4c (post-revert)
firmware — that's what the autoflash retries are pushing.

## What we KNOW about the audio (hard-won, on-device)
1. **Silence → fixed.** Root cause was the missing ESP32-S3 I2S TX clock HW
   register patch (use_apll is dead code on S3; MCLK/BCK ran at reset dividers).
   Mirrored M5Unified spk_task: tx_bck_div_num=7, exact calcClockDiv
   (n=58,x=1,y=6,z=13,yn1=1), tx_clk_sel=PLL_240M, tx_update commit. Audio became
   audible. THIS IS CORRECT — keep it.
2. **Distortion remains:** clean at sentence start, "sped up x100 / garbled" by
   the end. Progressive. Bigger I2S TX DMA buffer (4→8) made it "good for longer."
   tx_desc_auto_clear=false made it "freeze in a glitched loop."
3. **opus_drop=0** on the heartbeat with the ring build → the PCM/decode path was
   NOT dropping frames. So the loopTask-starvation theory (plan
   2026-06-21-cores3-audio-distortion-fix.md) is WRONG, or at least not the whole
   story. The ring made it worse, not better.

## Leading hypotheses still standing (for next session)
- **DMA underrun during WebSocket bursts** — the agent streams TTS in bursts with
  gaps; when the PCM queue drains between bursts the I2S TX underruns and the
  restart glitches. "Good for longer with bigger buffer" fits this BEST. Next try:
  a proper pre-buffer / ring that holds ~200ms of DECODED PCM and only starts
  i2s_write once primed, then keeps the DMA continuously fed (the old commit
  260968a "ring-buffer speaker output" did exactly this and was removed).
- **Backend-side**: confirm the agent actually streams clean 16kHz/20ms Opus to
  the end. The garble could partly be the ElevenLabs→Opus re-encode tail. Worth
  capturing the raw outbound Opus on the server and decoding it offline to hear
  whether the SERVER audio is already garbled (rules firmware in or out).

## Recovery automation in place (this session only)
- Cron job `3ecabede` runs `/tmp/cores3_autoflash.sh` at :07/:27/:47 each hour.
  It hammers the flash 12× per run; on a clean MD5 it touches
  `/tmp/cores3_flashed_ok`, then verifies the boot heartbeat and reports. If the
  USB link self-recovers overnight it will flash automatically. Dies when the
  Claude session ends. `CronDelete 3ecabede` to stop.
- `/tmp/cores3_autoflash.log` has the per-attempt history.

## Backend note
The Studio's `~/walkie-talkie/walkie_agent/cores3_session.py` still has 9
temporary `CORES3_DEBUG` print()s (harmless; they confirmed session_started
flows). Remove when convenient. The walkie server (PID was 19032) runs from
`/tmp/walkie-serve.log`; restart with the launchd plist or the nohup command if
it died.
