# Backend changes needed: stream agent TTS reply AFTER PTT release

**Date:** 2026-06-20
**Status:** firmware side DONE (commit 75edab8, verified on hardware); backend side TODO
**Source:** dev:codebase-analyzer investigation of `project/walkie_agent/`

## The problem

The CoreS3 firmware was fixed to play inbound TTS whenever the mic is idle (not
only during SESSION_ACTIVE), so the agent's reply can play after the user
releases PTT. But the **backend currently destroys the agent turn on release**,
so there is no reply to play. Both sides must change.

## Current backend turn lifecycle (verified, with file:line)

1. Connect `/ws/cores3?device_token=` → `api.py:1701`, token check `api.py:1717` / `cores3_token.py:36`
2. `cores3_session_handler()` `api.py:1728` → `cores3_session.py:120`; sends `{"type":"hello"}` `cores3_session.py:125-130`
3. PTT press → device sends `session_start` → backend `cores3_session.py:175`; creates OpusDecoder `:179`, `_start_conv()` `:181`→`:65`
4. `_start_conv()` builds `CoreS3AudioInterface` + OpusEncoder `:77`, constructs ElevenLabs `Conversation` `:85-90`, `conv.start_session()` (background thread → ElevenLabs WS) `:93`, waits ready `:95`, sends `session_started` `:182-186`
5. User talks → device sends Opus bin → `cores3_session.py:144` → decode `:147` → `input_callback(pcm)` `:148` → base64 → ElevenLabs `user_audio_chunk`
6. **PTT release → device sends `session_end` → backend `cores3_session.py:193` → IMMEDIATELY `_end_conv()` `:195`**
7. `_end_conv()` `:103`: `conv.end_session()` `:106` (SDK sets `_should_stop`, calls `audio_interface.stop()`), explicit `stop()` `:112` (sets `_running=False`, cancels pending futures `cores3_audio.py:127-136`), `encoder.close()` `:113`, nulls session `:115-117`
8. Sends `session_ended` immediately `:199`

**TTS path:** SDK bg thread receives ElevenLabs `audio` → `audio_interface.output(pcm)` → `cores3_audio.py:91` checks `_running`; if true, encodes Opus → `_schedule_send(ws.send_bytes(pkt))` `:109` via `run_coroutine_threadsafe` `:152`.

## Root cause

On release, `_end_conv()` (step 7) **kills the ElevenLabs conversation while it
is still running STT → LLM → TTS for the user's last utterance.** Any
`output()` after `stop()` hits the `if not self._running: dropped++; return`
guard (`cores3_audio.py:91`) and is discarded. `session_ended` is sent before
any reply audio exists.

## Required backend changes

### Primary fix — `cores3_session.py` (the `session_end` handler, ~:193-199)
Do **not** call `_end_conv()` on `session_end`. Instead:
1. Stop feeding mic audio to ElevenLabs (stop decoding device frames / stop
   calling `input_callback`) — signals end-of-user-utterance.
2. Keep the `Conversation` alive and `CoreS3AudioInterface._running = True` so
   `output()` keeps streaming the TTS reply as binary frames to the device.
3. Only after TTS finishes: call `_end_conv()` and send `session_ended`.

### Detecting "TTS done"
The ElevenLabs SDK has no explicit "TTS finished" callback. Options:
- **Preferred:** let the SDK `Conversation` run to natural completion. After the
  user audio stops, ElevenLabs completes the turn and closes its WS; the SDK
  thread exits `_run()` on `ConnectionClosedOK` and calls `end_session()`
  itself. Hook that to send `session_ended` to the device.
- Add an `on_tts_done` callback to `CoreS3AudioInterface` that fires after the
  last `output()` chunk (idle timer since last output), signaling the session
  handler to tear down + send `session_ended`.

### New intermediate state
Introduce a `RESPONDING` / `agent_speaking` state between "session_end received"
and "session_ended sent". Mic input is stopped; TTS output flows; teardown
deferred.

## Contract notes / mismatches
- **`session_ended` timing:** must be delayed until after all TTS frames are
  sent. If it arrives first, fine for the firmware now (it plays TTS in IDLE
  too), but cleaner to send it last so the device's session lifecycle matches.
- **No "TTS start" signal:** device infers agent-speaking from receiving the
  first binary frame. If UI wants an explicit "agent talking" cue, add a text
  frame (e.g. `{"type":"agent_speaking"}`) — optional.
- **`agent_interrupted`:** already exists (`cores3_audio.py:119`) for barge-in;
  firmware should stop playback on it (separate concern).
- **No welcome/greeting today:** only text `hello`. To add a spoken greeting,
  inject a prompt into the ElevenLabs session after `_start_conv()` so it emits
  TTS as binary frames; the firmware (now playing in IDLE) will play it.
- **No half-duplex gating server-side:** `output()` streams regardless of device
  mic state — fine, since the firmware enforces the gate (mutes mic before
  playing).

## No changes needed
`cores3_token.py` (auth), `opus_codec.py` (codec fine), `api.py` (dispatch
fine), `audio/elevenlabs_bridge.py` (browser path, unused by CoreS3).
