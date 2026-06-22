---
date: 2026-06-22
status: BLOCKED on physical USB — needs cable replug
branch: feature/enable-mic
---
# CoreS3 overnight handoff — flash blocked by corrupting USB-JTAG link

## TL;DR for the morning
The CoreS3's USB-JTAG link started corrupting bulk transfers around 05:00. I can
no longer flash it remotely — every full write reaches 100% but fails the final
MD5 (bit errors in the link), or disconnects at ~7-20%. **This is physical
(cable/port/connector), not firmware.** Fix: **unplug and replug the USB-C
cable on the Mac Studio** (or move it to a different port), then flash normally:

```
cd firmware/cores3 && bash scripts/deploy_to_cores3.sh --flash-only
```

The device is NOT bricked — it has a valid bootloader + partition table; only
the app partition is erased/partial, so it sits safely in the ROM bootloader.

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
