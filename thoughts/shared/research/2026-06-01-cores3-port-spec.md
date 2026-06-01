# CoreS3 Port Spec — Walkie-Talkie Voice Agent on M5Stack CoreS3

**Status:** Pre-device design document. No implementation until hardware is in hand.  
**Date:** 2026-06-01  
**Author:** Research synthesis + design  
**Scope:** Replace the iPad-in-Safari client with an M5Stack CoreS3 embedded device talking to the existing Python FastAPI backend.

---

## Table of Contents

1. [Purpose & Non-Goals](#1-purpose--non-goals)
2. [Hardware & Wiring](#2-hardware--wiring)
3. [Architecture Diagram](#3-architecture-diagram)
4. [Audio Chain Spec](#4-audio-chain-spec)
5. [Wire Protocol v1](#5-wire-protocol-v1)
6. [Server-Side Changes (Python)](#6-server-side-changes-python)
7. [Firmware Spec (CoreS3 Side)](#7-firmware-spec-cores3-side)
8. [Memory Budget](#8-memory-budget)
9. [Power & Battery (v1: Scoped Not Solved)](#9-power--battery-v1-scoped-not-solved)
10. [Wake-Word Options (v2 Only)](#10-wake-word-options-v2-only)
11. [Existing CoreS3 References](#11-existing-cores3-references-to-mine)
12. [Risks & Open Questions](#12-risks--open-questions)
13. [Test Plan (Pre-Device)](#13-test-plan-pre-device)
14. [Glossary](#14-glossary)
15. [Sources](#15-sources)

---

## 1. Purpose & Non-Goals

### Purpose

The current walkie-talkie system runs its client inside Safari on an iPad. The kid sees a green CRT-style screen, taps a large PTT button, and talks to one of eleven ElevenLabs Conversational AI characters (Radek, Steve, Simba, Ryder, Creeper, Pimpek, Crewmate, Sonic, Pikachu, Mario, Roblox Noob). The browser connects directly to ElevenLabs ConvAI via the ElevenLabs JS SDK, using server-side signed URLs to hide the API key. Client-side tools (`switch_character`, `show_animation`, `go_to_sleep`, `game_control`, `play_on_speaker`, `recall_memory`, `remember`, `create_animation`, `send_message`, `parent_location`) are implemented as JavaScript callbacks that run in the browser and call back to the FastAPI server's `/api/tools/*` endpoints.

This port replaces the iPad client with an M5Stack CoreS3 (ESP32-S3 microcontroller). The device is physically kid-friendly: rounded corners, integrated speaker and dual microphone, touch screen (unused in v1), and a USB-C power connector. Because the CoreS3 cannot run the ElevenLabs JavaScript SDK or the Python SDK, a new server-side bridge layer sits between the CoreS3 and the ElevenLabs ConvAI service. The FastAPI server already manages character selection and tool execution; the bridge extends it to handle raw Opus audio frames over WebSocket from the embedded device.

All eleven ElevenLabs agents, all system prompts, all server-side `/api/tools/*` endpoints, the parent inbox (Telegram/WhatsApp), the HomePod playback system, the memory vector store, and the `_CHARACTERS` roster in `api.py` remain untouched.

### Goals Summary

- CoreS3 device replaces iPad as the audio I/O endpoint.
- All 11 characters remain available; switching is done via a JSON control message (same session, no reconnect).
- Server-side bridge translates between the CoreS3 raw Opus protocol and the ElevenLabs Python SDK `Conversation` class.
- All tools that depend on server infrastructure (`send_message`, `parent_location`, `recall_memory`, `remember`, `play_on_speaker`) continue to work via the existing REST endpoints.
- Tools that are pure browser UI (`show_animation`, `game_control`, `create_animation`) degrade gracefully on CoreS3 v1 (spoken acknowledgement only).

### Non-Goals (v1)

- **Wake-word detection.** PTT-only in v1. See §10 for v2 options.
- **Battery optimisation.** Always-on Wi-Fi, no deep sleep. See §9.
- **LCD UI.** The 320×240 ILI9342C display is not used in v1. No graphics code.
- **Multiple simultaneous devices.** Single-device assumption.
- **OTA firmware updates.**
- **Local LLM or ASR.** All speech processing remains on ElevenLabs.

---

## 2. Hardware & Wiring

### CoreS3 SoC and Core Specs

| Component | Detail | Source |
|-----------|--------|--------|
| SoC | ESP32-S3 dual-core Xtensa LX7 @ 240 MHz | [#1] |
| PSRAM | 8 MB (Octal SPI) | [#1] |
| Flash | 16 MB (Quad SPI) | [#1] |
| Wi-Fi | 802.11 b/g/n 2.4 GHz (built into ESP32-S3) | [#1] |
| Bluetooth | BLE 5.0 (not used in v1) | [#1] |
| USB | USB-C OTG (flashing, power, serial debug) | [#1] |
| Display | 2.0" ILI9342C TFT 320×240 (not used in v1) | [#1] |
| Touch IC | FT6336U capacitive touch (not used in v1) | [#1] |
| Speaker amp | AW88298 1W class-D I2S amplifier | [#1] |
| Microphones | Dual MEMS mic array, ES7210 I2S ADC | [#1] |
| PMU | AXP2101 power management IC | [#6] |
| RTC battery | Onboard 500 mAh Li-ion | [#6] |
| Operating voltage | 5V via USB-C; 3.3V rail from AXP2101 | [#1] |

### I2S Pin Table

All audio data passes over a single shared I2S bus. The BCK (bit clock) and WCK/LRCK (word clock) lines are shared between the ES7210 ADC and the AW88298 DAC; only the data lines differ. [#1][#6]

| Signal | GPIO | Direction | Connected to |
|--------|------|-----------|-------------|
| MCLK | GPIO0 | Output | ES7210 MCLK |
| BCK | GPIO34 | Output | ES7210 + AW88298 BCLK |
| WCK/LRCK | GPIO33 | Output | ES7210 + AW88298 LRCK |
| DOUT (mic data) | GPIO14 | Input | ES7210 SDOUT |
| DIN (speaker data) | GPIO13 | Output | AW88298 SDIN |

Both codec chips are also controlled over I2C (separate bus, addresses 0x36 for ES7210 and 0x36/0x34 for AW88298 depending on ADDR pin).

The I2C for codec control is separate from the I2C for the AXP2101 PMU and the FT6336U touch controller.

**Key constraint:** Because BCK and WCK are shared, both TX (speaker) and RX (mic) I2S channels must be configured with identical sample rate and bit depth. See §12 for the duplex risk analysis.

### Power Connections

The CoreS3 is powered through USB-C. The AXP2101 PMU manages power rails. The internal 500 mAh Li-ion provides backup power when USB is absent. In v1 the device runs off USB-C only; the battery is an accidental bonus, not a design dependency.

The AW88298 speaker amplifier requires the AXP2101 ALDO1 (1.8V) and BLDO1 (2.8V) rails to be enabled; the ES7210 requires ALDO2 (3.3V). These are all enabled by default in M5Unified BSP initialization.

### Photo / Datasheet References

- M5Stack product page: https://shop.m5stack.com/products/m5stack-cores3-esp32s3-iotdevelopment-kit [#1]
- M5Stack docs page: https://docs.m5stack.com/en/core/CoreS3 [#1]
- ESP component registry (BSP): https://components.espressif.com/components/espressif/m5stack_core_s3 [#2]

---

## 3. Architecture Diagram

### Current Path (Browser Client)

```
  ┌──────────────────────────────────────────────────────────────────────┐
  │  iPad / Safari                                                       │
  │  ElevenLabs JS SDK  →  Conversation.startSession({ agentId, tools }) │
  │  client tools: switch_character, show_animation, go_to_sleep,        │
  │                game_control, play_on_speaker, recall_memory,         │
  │                remember, create_animation, send_message,             │
  │                parent_location, ask_expert                           │
  └───────────────────────────┬──────────────────────────────────────────┘
                              │  wss://convai.elevenlabs.io (signed URL)
                              │  PCM 16kHz in/out, JSON tool calls
                              ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  ElevenLabs ConvAI cloud                                             │
  │  (claude-sonnet-4-6, eleven_v3_conversational, pcm_16000)            │
  └──────────────────────────────────────┬───────────────────────────────┘
                                         │  tool webhook (HTTP POST)
                                         ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  FastAPI server (Mac Studio / Tailscale)                             │
  │  /api/tools/send-message, /api/tools/parent-location,               │
  │  /api/tools/recall-memory, /api/tools/remember,                     │
  │  /api/tools/homepod, /api/tools/ask-expert                          │
  └──────────────────────────────────────────────────────────────────────┘
```

### New Path (CoreS3 Client)

```
  ┌────────────────────────────────────────────────────────────────────┐
  │  M5Stack CoreS3                                                    │
  │  ┌─────────────────┐   ┌──────────────────────────────────────┐   │
  │  │  ES7210 mic ADC │──▶│  audio_in_task (Core 1)              │   │
  │  │  I2S RX GPIO14  │   │  I2S read → Opus encode 16kbps       │   │
  │  └─────────────────┘   │  → WSS binary frame                  │   │
  │                        └──────────────┬───────────────────────┘   │
  │                                       │                           │
  │                        ┌──────────────▼───────────────────────┐   │
  │                        │  control_task (Core 1)               │   │
  │                        │  JSON text frames in/out             │   │
  │                        │  switch_character, tool_event        │   │
  │                        │  PTT touch detection                 │   │
  │                        └──────────────┬───────────────────────┘   │
  │                                       │                           │
  │                        ┌──────────────▼───────────────────────┐   │
  │                        │  audio_out_task (Core 1)             │   │
  │  ┌─────────────────┐   │  WSS binary recv → Opus decode       │   │
  │  │  AW88298 amp    │◀──│  → I2S write GPIO13                  │   │
  │  │  I2S TX GPIO13  │   └──────────────────────────────────────┘   │
  │  └─────────────────┘                                              │
  └──────────────────────────────┬─────────────────────────────────────┘
                                 │  WSS /ws/cores3?device_token=…
                                 │  binary: Opus packets
                                 │  text: JSON control messages
                                 ▼
  ┌────────────────────────────────────────────────────────────────────┐
  │  FastAPI server — NEW endpoint /ws/cores3                          │
  │                                                                    │
  │  ┌──────────────────────────────────────────────────────────────┐  │
  │  │  cores3_bridge.py                                            │  │
  │  │  ┌─────────────────┐    ┌────────────────────────────────┐  │  │
  │  │  │ WS recv loop    │    │ ElevenLabs Python SDK          │  │  │
  │  │  │ binary → Opus   │───▶│ Conversation(                  │  │  │
  │  │  │ decode → PCM    │    │   audio_interface=             │  │  │
  │  │  │ → input_callback│    │     CoreS3AudioInterface,      │  │  │
  │  │  └─────────────────┘    │   client_tools=server_tools    │  │  │
  │  │  ┌─────────────────┐    │ )                              │  │  │
  │  │  │ output() recv   │◀───│                                │  │  │
  │  │  │ PCM → Opus enc  │    └────────────────────────────────┘  │  │
  │  │  │ → WS binary send│                                        │  │
  │  │  └─────────────────┘                                        │  │
  │  └──────────────────────────────────────────────────────────────┘  │
  │                                                                    │
  │  REUSED UNCHANGED:                                                 │
  │  /api/tools/send-message, /api/tools/parent-location,             │
  │  /api/tools/recall-memory, /api/tools/remember,                   │
  │  /api/tools/homepod, /api/tools/ask-expert                        │
  │  _CHARACTERS dict, _resolve_agent_id(), ConfigDatabase             │
  │  ParentInbox (Telegram/WhatsApp)                                   │
  └──────────────────────────────┬─────────────────────────────────────┘
                                 │  wss://convai.elevenlabs.io (Python SDK)
                                 │  PCM 16kHz in/out, JSON tool calls
                                 ▼
  ┌────────────────────────────────────────────────────────────────────┐
  │  ElevenLabs ConvAI cloud                                           │
  │  (claude-sonnet-4-6, eleven_v3_conversational, pcm_16000)          │
  └────────────────────────────────────────────────────────────────────┘
```

**What is reused:** The entire FastAPI server, all `/api/tools/*` REST endpoints, all ElevenLabs agents and their system prompts, the character registry in `api.py`, the `ConfigDatabase`, the `ParentInbox`, and the `HomePod` integration.

**What is new on the server:** One new WebSocket endpoint (`/ws/cores3`), one bridge module (`cores3_bridge.py`), one Opus codec wrapper (`opus_codec.py`), and one device token module (`cores3_token.py`).

**What is new on the device:** Entire PlatformIO firmware (C++/Arduino).

---

## 4. Audio Chain Spec

### Design Decision: Sample Rate

ElevenLabs ConvAI operates at **16 kHz PCM 16-bit mono** for both input (ASR) and output (TTS) when using the Python SDK's `AudioInterface`. This is confirmed by the SDK source code docstring: *"Capture audio in a format compatible with the ElevenLabs API (typically PCM 16-bit, 16kHz mono)"* and *"The audio parameter passed to output() is in the same PCM format."* [#5][#8]

ElatoAI uses **24 kHz / 12 kbps Opus** on the wire between device and edge server, then transcodes on the server side for the OpenAI Realtime API (which prefers 24 kHz). [#4]

For this project the target is ElevenLabs ConvAI via Python SDK, which expects 16 kHz. Running the I2S hardware at 24 kHz and downsampling on the server is unnecessary complexity. Instead:

- **Capture and playback on the CoreS3 run at 16 kHz.**
- **Opus is transmitted at 16 kHz mono, 16 kbps, 20 ms frames.**
- The Python bridge feeds the SDK directly at 16 kHz PCM with no resampling.

This eliminates one processing stage vs. ElatoAI's architecture and is architecturally cleaner for our target API.

### Capture Chain (Mic → Wire)

```
ES7210 dual-mic I2S ADC
  ↓  I2S RX at 16 kHz, 16-bit stereo (hardware generates stereo from dual mics)
  ↓  downmix to mono (average left+right or take left channel only)
16 kHz, 16-bit, mono PCM — in device PSRAM ring buffer
  ↓  Opus encoder (pschatzmann/arduino-libopus [#3])
     mode: VOIP / SILK, 16 kbps, 20 ms frames = 320 samples/frame
     output: ~40 bytes/frame
  ↓  WebSocket binary message (one Opus packet = one WS message)
FastAPI bridge WSS receive
  ↓  Opus decode (opuslib Python [#9])
     output: 320 samples × 2 bytes = 640 bytes PCM per 20 ms
  ↓  input_callback(pcm_bytes) → ElevenLabs Python SDK Conversation
ElevenLabs ConvAI cloud (STT → LLM → TTS)
```

### Playback Chain (Wire → Speaker)

```
ElevenLabs ConvAI cloud TTS output
  ↓  output(pcm_bytes) called on CoreS3AudioInterface
     PCM: 16 kHz, 16-bit, mono chunks (~250 ms = 4000 samples recommended [#8])
  ↓  Opus encode (opuslib Python)
     16 kbps, 20 ms frames
  ↓  WebSocket binary message → CoreS3
CoreS3 WSS receive
  ↓  Opus decode (pschatzmann/arduino-libopus)
     output: 320 samples, 16-bit mono
  ↓  output jitter buffer (200–400 ms; see §7)
  ↓  I2S TX to AW88298 speaker amp at 16 kHz, 16-bit mono
  ↓  1W speaker (0.5W typical conversation level)
```

### Codec Parameters (Locked)

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Sample rate | 16,000 Hz | ElevenLabs ConvAI SDK native; eliminates resampling [#5][#8] |
| Channels | 1 (mono) | ElevenLabs SDK expects mono; reduces bitrate |
| Opus mode | VOIP (SILK at this bitrate) | Best for speech; low CPU at 16 kHz [#3] |
| Bitrate | 16 kbps | Adequate for speech; ~40 bytes/frame; ≈3 KB/s wire bandwidth |
| Frame duration | 20 ms | Standard for VoIP; 320 samples/frame at 16 kHz |
| Opus application | `OPUS_APPLICATION_VOIP` | Optimised for voice, enables DTX, comfort noise |

ElatoAI uses 12 kbps at 24 kHz [#4]. Our choice of 16 kbps at 16 kHz yields slightly higher audio fidelity within ElevenLabs' native format with no extra server-side processing.

---

## 5. Wire Protocol v1

### Connection URL

```
wss://<tailscale-host>:8000/ws/cores3?device_token=<token>
```

The `device_token` is a per-device opaque token (32 hex characters, UUID v4) issued once and stored in the CoreS3's NVS flash. It maps to a device record in `cores3_token.py`. The server rejects connections with an unknown or revoked token with HTTP 401 before the WebSocket upgrade completes.

### Transport Rules

- All **audio data** is carried in WebSocket **binary** messages. Each binary message is exactly one Opus packet. No length prefix, no framing header — the WebSocket framing itself delimits packets. [#4]
- All **control messages** are WebSocket **text** messages containing JSON. Maximum text message size: 4096 bytes.
- The server sends audio binary frames and control text frames. The device sends audio binary frames and control text frames.
- A binary frame received while the device is not in `SESSION_ACTIVE` state is silently dropped by the server.

### Session Lifecycle

1. Device connects (HTTP Upgrade → WSS handshake, token validated).
2. Server sends `hello` text message with current character and capabilities.
3. Device is in `IDLE` state. No audio flows.
4. User presses PTT (touch): device sends `session_start` control message. Server starts an ElevenLabs `Conversation` session.
5. Device sends Opus binary frames. Server feeds PCM to the SDK `input_callback`.
6. ElevenLabs SDK calls `output(pcm)` on `CoreS3AudioInterface`. Server Opus-encodes and sends binary frames to device.
7. User releases PTT: device sends `session_end`. Server calls `conversation.end_session()`.
8. Server sends `session_ended` when ElevenLabs session closes cleanly.
9. Device returns to `IDLE`. WebSocket connection remains open.

Character switching does NOT require a reconnect. A `switch_character` control message from the device causes the server to end the current ElevenLabs session (if any) and start a new one with a different agent ID on the next `session_start`.

### Control Message Schemas

All JSON control messages share a `type` field as the discriminator. Timestamps are Unix seconds (integer).

#### Device → Server

**`session_start`** — User pressed PTT; device wants to start talking.
```json
{
  "type": "session_start",
  "ts": 1748779200
}
```

**`session_end`** — User released PTT (or latch tap-to-close in v1.5).
```json
{
  "type": "session_end",
  "ts": 1748779210
}
```

**`switch_character`** — Device requests character change. Takes effect on next `session_start`.
```json
{
  "type": "switch_character",
  "character": "steve",
  "ts": 1748779215
}
```
Valid `character` values: `radek | steve | simba | ryder | creeper | pimpek | crewmate | sonic | pikachu | mario | roblox_noob` (mirrors `_CHARACTERS` in `api.py`).

**`set_volume`** — Set speaker volume 0–100. Server echoes back a `volume_ack`.
```json
{
  "type": "set_volume",
  "level": 70,
  "ts": 1748779220
}
```

**`keepalive`** — Device heartbeat when no session active (every 30 s). Prevents NAT/load-balancer timeouts.
```json
{
  "type": "keepalive",
  "ts": 1748779230
}
```

#### Server → Device

**`hello`** — Sent immediately after WebSocket handshake succeeds.
```json
{
  "type": "hello",
  "server_version": "1",
  "current_character": "radek",
  "characters": ["radek","steve","simba","ryder","creeper","pimpek","crewmate","sonic","pikachu","mario","roblox_noob"],
  "ts": 1748779200
}
```

**`session_started`** — ElevenLabs `Conversation` session is open and ready.
```json
{
  "type": "session_started",
  "character": "radek",
  "conversation_id": "c_abc123",
  "ts": 1748779201
}
```

**`session_ended`** — ElevenLabs session has closed cleanly.
```json
{
  "type": "session_ended",
  "character": "radek",
  "ts": 1748779211
}
```

**`tool_event`** — Echo of an agent tool call. Device may use this to trigger a LED blink, status sound, or (future) LCD update.
```json
{
  "type": "tool_event",
  "tool": "switch_character",
  "args": {"character": "steve"},
  "ts": 1748779205
}
```

**`volume_ack`** — Confirmation after `set_volume`.
```json
{
  "type": "volume_ack",
  "level": 70,
  "ts": 1748779221
}
```

**`error`** — Server-side error. Device should display indicator and may retry.
```json
{
  "type": "error",
  "code": "auth_failed",
  "message": "device token not found",
  "ts": 1748779200
}
```

Error codes: `auth_failed`, `agent_unavailable`, `session_start_failed`, `session_limit_exceeded`.

**`keepalive_ack`** — Echo of device keepalive.
```json
{
  "type": "keepalive_ack",
  "ts": 1748779231
}
```

### PTT Model (v1)

**v1 (momentary):** Touch screen (anywhere) → `session_start`. Release touch → `session_end`. This maps directly to the existing browser PTT behaviour (the `#ptt-btn` in `index.html` uses `pointerdown`/`pointerup`). The CoreS3 FT6336U touch IC fires a finger-down event; the firmware uses a debounce of 50 ms.

**v1.5 (latching, optional):** First tap → `session_start`. Second tap → `session_end`. Useful for longer conversations without holding. Implemented as a state toggle in `control_task`. Can be selected at build time via a compile flag.

---

## 6. Server-Side Changes (Python)

### 6.1 New File: `walkie_agent/cores3_bridge.py`

This is the central new module. It contains `CoreS3AudioInterface`, a class that implements the ElevenLabs Python SDK `AudioInterface` abstract base class. [#5][#8]

The confirmed SDK abstract base class signature (from GitHub source as of 2026-06-01) is:

```python
# From elevenlabs/conversational_ai/conversation.py
from abc import ABC, abstractmethod
from typing import Callable

class AudioInterface(ABC):
    @abstractmethod
    def start(self, input_callback: Callable[[bytes], None]):
        """Called once before conversation begins.
        input_callback must be invoked with 16-bit PCM mono 16kHz chunks."""

    @abstractmethod
    def stop(self):
        """Called once after conversation ends. Release resources."""

    @abstractmethod
    def output(self, audio: bytes):
        """Receive agent audio in 16-bit PCM mono 16kHz. Must return promptly."""

    @abstractmethod
    def interrupt(self):
        """User interrupted agent. Stop buffered output immediately."""
```

There is also `AsyncAudioInterface` with the same four methods declared as `async def`, where `start` takes `Callable[[bytes], Awaitable[None]]`. [#5]

`CoreS3AudioInterface` will use the async variant because the bridge runs inside FastAPI's asyncio event loop. Design:

- `start(input_callback)`: stores callback; called when ElevenLabs session opens.
- `output(audio)`: Opus-encodes the PCM chunk and calls `await websocket.send_bytes(opus_packet)`. Must not block; if the WebSocket send queue is full, drop the packet and mark an interrupt (the device will hear silence, which is acceptable degradation).
- `interrupt()`: sends a `{"type":"agent_interrupted"}` text frame to the device so the device can flush its jitter buffer. Also sends a WebSocket binary frame consisting of a special sentinel (1-byte `0xFF`) to flush the device's output queue.
- `stop()`: sets an internal `_running` flag to False; no further audio calls after this.

The module also contains the WebSocket endpoint handler (`cores3_session_handler`) that manages the lifetime of one CoreS3 connection: reads the `hello` handshake, dispatches binary frames to the input callback, and fires `CoreS3AudioInterface.output` for outgoing frames.

### 6.2 New Endpoint: `WebSocket /ws/cores3`

Added to `walkie_agent/api.py` alongside the existing `/ws/walkie-talkie/{client_id}`.

```python
@app.websocket("/ws/cores3")
async def cores3_ws(websocket: WebSocket, device_token: str = Query(...)):
    # 1. Validate device_token via cores3_token.lookup(device_token)
    # 2. Accept websocket
    # 3. Send hello
    # 4. Start cores3_session_handler(websocket, device_record)
    ...
```

The endpoint accepts `device_token` as a query parameter (consistent with the URL defined in §5) rather than a header, because the Arduino WebSocket library (`arduinoWebSockets`) does not support custom HTTP headers during the upgrade handshake without patching. [#4]

### 6.3 New File: `walkie_agent/opus_codec.py`

Wraps the `opuslib` Python library for raw Opus encode/decode. [#9]

**Recommendation: use `opuslib` (not PyOgg).** PyOgg is designed for OggOpus file I/O; opuslib is lightweight ctypes bindings to libopus, matches the VoIP use case, and is the right tool for raw packet-level encode/decode over a WebSocket transport. System dependency: `libopus` must be present in the Docker image and on the Mac Studio (`brew install opus` or `apt-get install libopus0`).

Key functions this module must expose:
- `OpusEncoder(sample_rate=16000, channels=1, application='voip')` — stateful; one per session.
- `OpusDecoder(sample_rate=16000, channels=1)` — stateful; one per session.
- `encode(pcm_bytes: bytes) -> bytes` — input: 320 samples (640 bytes) of 16-bit LE mono; output: Opus packet ~40 bytes.
- `decode(opus_bytes: bytes) -> bytes` — input: Opus packet; output: 640 bytes PCM.

Both encoder and decoder are stateful objects. One pair per CoreS3 session, created in `cores3_bridge.py` when a `session_start` control message arrives and destroyed on `session_end`.

### 6.4 New File: `walkie_agent/cores3_token.py`

Manages per-device authentication. Storage: SQLite via the existing `ConfigDatabase` (new table `cores3_devices`).

Schema addition to `ConfigDatabase`:
```sql
CREATE TABLE IF NOT EXISTS cores3_devices (
    token TEXT PRIMARY KEY,              -- 32-char hex UUID
    device_name TEXT NOT NULL,           -- e.g. "kids-room-1"
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_seen TIMESTAMP,
    revoked INTEGER DEFAULT 0
);
```

Key functions:
- `issue_token(device_name: str) -> str` — generates UUID4 hex, inserts record, returns token.
- `lookup(token: str) -> Optional[dict]` — returns device record or None if not found/revoked.
- `revoke(token: str) -> None`

Tokens are provisioned once via the admin CLI (`walkie-agent cores3 issue-token --name kids-room-1`). The token is stored in the CoreS3's NVS (non-volatile storage) using `Preferences` library during first setup.

### 6.5 Tool Routing: Per-Tool Behaviour on CoreS3

The browser client runs tool callbacks as JavaScript functions that can update the DOM (faces, animations, character rail). The CoreS3 has no DOM. Tool calls from ElevenLabs ConvAI arrive at the Python SDK's `client_tools` callbacks. The server-side bridge registers these callbacks and routes them appropriately.

| Tool | UI in Browser Today | CoreS3 v1 Behaviour | New Server Code Needed? |
|------|--------------------|--------------------|------------------------|
| `switch_character` | Swaps character face/name in DOM; changes EL session | End current session; start new session with new agent on next PTT; send `tool_event` to device | Yes — bridge must handle session swap |
| `show_animation` | Renders ASCII animation in `#face` div | No visual output; agent can say "I'm showing something now" — spoken only | No (tool degrades silently) |
| `go_to_sleep` | Fades UI, stops session | Send `session_ended` to device; device returns to IDLE | Minimal — map to existing session_end flow |
| `game_control` | Controls browser mini-game | No-op in v1; spoken acknowledgement from agent | No |
| `play_on_speaker` | Calls `/api/tools/homepod` via JS fetch | Bridge calls `/api/tools/homepod` via internal HTTP | Yes — internal call in bridge |
| `recall_memory` | JS fetch → `/api/tools/recall-memory` → injected into prompt | Bridge calls `/api/tools/recall-memory` via internal HTTP | Yes — internal call in bridge |
| `remember` | JS fetch → `/api/tools/remember` | Bridge calls `/api/tools/remember` | Yes — internal call in bridge |
| `create_animation` | JS fetch → `/api/tools/create-animation` → renders in DOM | No visual output; result spoken or discarded | No (discarded silently) |
| `send_message` | JS fetch → `/api/tools/send-message` | Bridge calls `/api/tools/send-message` | Yes — internal call in bridge |
| `parent_location` | JS fetch → `/api/tools/parent-location` → spoken | Bridge calls `/api/tools/parent-location` | Yes — internal call in bridge |
| `ask_expert` | JS fetch → `/api/tools/ask-expert` → spoken | Bridge calls `/api/tools/ask-expert` | Yes — internal call in bridge |

The five tools that call REST endpoints (`play_on_speaker`, `recall_memory`, `remember`, `send_message`, `parent_location`, `ask_expert`) should call them as internal Python function calls (importing and calling the endpoint handler functions directly) rather than HTTP loops, since the bridge runs in the same process. The `/api/tools/*` REST route wrappers are unchanged for external callers (ElevenLabs webhook tools) and browser.

**`switch_character` handling in the bridge** is the most complex: ElevenLabs ConvAI does not support mid-session agent switching. When the agent calls `switch_character(character="steve")`, the bridge must:
1. Call `conversation.end_session()` on the current ElevenLabs `Conversation` object.
2. Update the session's `current_character` to the new value.
3. Send a `tool_event{"tool":"switch_character","args":{"character":"steve"}}` to the device.
4. On the next `session_start` from the device, instantiate a new `Conversation` with the new agent ID.

The `current_character` is per-connection state stored in the `cores3_session_handler` coroutine's local variables.

---

## 7. Firmware Spec (CoreS3 Side)

### Build System

- **Platform:** PlatformIO
- **Framework:** `arduino` (arduino-espressif32)
- **Board ID:** `m5stack-cores3`
- **IDF version target:** ESP-IDF v5.x (bundled with arduino-espressif32 platform 6.x)

`platformio.ini` skeleton:
```ini
[env:m5stack-cores3]
platform = espressif32@6.10.0
board = m5stack-cores3
framework = arduino
monitor_speed = 115200
board_build.psram = enabled
board_build.flash_mode = qio
board_build.flash_freq = 80m
```

### Required Libraries

| Library | Version | Purpose | Source |
|---------|---------|---------|--------|
| `M5Unified` | >= 0.2.x | CoreS3 BSP: codec init, AXP2101, touch, display init | M5Stack [#2] |
| `arduinoWebSockets` (links2004) | >= 2.5.0 | WSS client with TLS via mbedTLS | PIO registry [#4] |
| `pschatzmann/arduino-libopus` | a1.1.0 | Opus codec (Xtensa DSP-optimised) | PIO registry [#3][#4] |
| `pschatzmann/arduino-audio-tools` | 1.0.1 | I2S abstraction, ring buffers, resampling | PIO registry [#4] |
| `M5GFX` | latest | Display driver (optional; not used in v1 for drawing, but M5Unified depends on it) | M5Stack |

`M5Unified` wraps both the ES7210 ADC and AW88298 DAC via its internal BSP layer, which in turn uses `esp_codec_dev`. Calling `M5.Speaker.begin()` and `M5.Mic.begin()` is the simplest initialisation path. For lower-level control (e.g., to set exact sample rate), use `M5.Speaker.config()` before calling `begin()`.

Note: `pschatzmann/arduino-libopus` is the Arduino port of `esphome/micro-opus` and `opuslib`. It delivers approximately 8% dual-core CPU for CELT decode at 48 kHz stereo; at 16 kHz SILK it is even lighter (~2%). [#3]

### FreeRTOS Task Architecture

Three tasks run after Wi-Fi connects:

| Task | Core | Stack | Priority | Role |
|------|------|-------|----------|------|
| `audio_in_task` | Core 1 | 8 KB | 5 | I2S read → Opus encode → WSS send |
| `audio_out_task` | Core 1 | 8 KB | 5 | WSS recv → Opus decode → I2S write |
| `control_task` | Core 1 | 4 KB | 3 | JSON parse, touch poll, state machine |

Wi-Fi and TCP/IP stack run on Core 0 (ESP-IDF default, pinned by FreeRTOS config). The `arduinoWebSockets` library calls are made from tasks pinned to Core 1 via the library's internal thread-safe queue; WSS send/recv itself is serviced on Core 0 by the TCP stack.

Core assignment follows the ElatoAI recommended pattern: Core 0 = Wi-Fi/network, Core 1 = application/audio. [#4] The ESPHome I2S-task core-pinning note applies here: if any I2S initialisation code internally uses floating-point (the ESP-IDF I2S driver does), the I2S task gets permanently pinned to whichever core it initialises on. Starting I2S inside `setup()` (which runs on Core 1) ensures mic and speaker both pin to Core 1 as intended. [#6]

Audio in and audio out tasks can run concurrently on Core 1; ESP32-S3's LX7 is capable of interleaved DMA operations on the same I2S bus (separate TX/RX DMA channels).

### Buffer Sizes

#### Input Ring Buffer (mic → encode → send)

The I2S DMA driver delivers audio in DMA buffers. A ring buffer between the DMA interrupt and the Opus encoder decouples DMA timing from encoding latency.

- DMA buffer count: 4 × DMA buffer size: 512 bytes = 4 × 256 samples
- Ring buffer size: 4 frames × 320 samples × 2 bytes = 2560 bytes
- Location: PSRAM (reduces SRAM pressure; access latency acceptable for audio)

At 16 kHz, 320 samples = 20 ms. If the encode task runs late by up to 3 frames, the ring buffer absorbs the jitter before dropping audio.

#### Output Jitter Buffer (receive → decode → play)

Network jitter from Wi-Fi and the Python server can cause Opus packets to arrive in bursts. The jitter buffer smooths playback.

- Target buffering depth: 200–400 ms (160–320 ms = 8–16 Opus frames at 20 ms each)
- Buffer at 300 ms: 15 frames × 40 bytes encoded = 600 bytes (trivial)
- After decode: 15 frames × 640 bytes PCM = 9600 bytes — fit in SRAM
- Trade-off: 200 ms buffer adds 200 ms to end-to-end latency but prevents audible dropouts on Wi-Fi packet clusters. Tune at integration time.
- Location: SRAM (faster I2S DMA feed; decoded PCM is read by DMA continuously)

### State Machine

```
         power on
              │
              ▼
           BOOT
         (setup(),
          NVS load,
          M5Unified init,
          AXP2101 init)
              │  NVS token found
              ▼
        WIFI_CONNECT
        (WiFi.begin,
         30 s timeout)
              │  IP acquired             timeout → BOOT (retry)
              ▼
        WSS_CONNECT
        (WebSocketsClient
         begin(host,port,path))
              │  hello received          connect fail → WSS_CONNECT (retry 5s)
              ▼
           IDLE
        (keepalive every 30s,
         listen for touch)
              │  touch down (PTT)
              │  session_start sent
              ▼
       SESSION_ACTIVE ──────────────────────────────────────────────────┐
       (audio_in_task: I2S read                                         │
        → Opus enc → WS send)                                           │
       (audio_out_task: WS recv                                         │
        → Opus dec → I2S write)                                         │
       (control_task: tool_events,                                      │
        switch_character buffered)                                       │
              │  touch up (PTT release)                                 │
              │  session_end sent                                        │
              ▼                                                          │
      SESSION_ENDING                                                     │
      (drain output jitter buf,                                         │
       wait session_ended from server,                                  │
       2 s timeout then force IDLE)                                     │
              │  session_ended received or timeout                      │
              └──────────────────────────────────────────────────────── ▼
                                                                      IDLE
```

**Error paths:**
- Wi-Fi disconnect during `SESSION_ACTIVE` → `SESSION_ENDING` (drain) → `WIFI_CONNECT`.
- WebSocket disconnect → attempt reconnect in `WSS_CONNECT`; audio tasks suspended.
- ElevenLabs session error (server sends `error` control frame) → `SESSION_ENDING` → `IDLE`.
- Watchdog: if `SESSION_ACTIVE` persists > 120 s without any binary frame exchange, force `session_end`.

### TLS

`arduinoWebSockets` uses mbedTLS (bundled with arduino-espressif32). The ESP32-S3 has hardware SHA-256 and AES acceleration, so TLS handshake is approximately 300–800 ms on first connection and ~100 ms on resumption (session tickets).

TLS RAM cost: ~40–50 KB per connection. This is allocated from PSRAM via `iram_safe_malloc`. The WebSocket library must be configured with `#define USE_EXTRA_CLIENTSOCKET_ARGS` and `USE_SPIFFS`/`USE_LittleFS` for root CA certificate storage, or the server certificate pinned at build time.

For v1 on a closed Tailscale network, certificate validation can be set to `WEBSOCKETS_NETWORK_TYPE_DEFAULT` with a pinned CA. Full CA bundle not required.

---

## 8. Memory Budget

All figures are peak estimates. SRAM = internal SRAM of the ESP32-S3 (512 KB). PSRAM = 8 MB Octal SPI external.

| Component | SRAM (KB) | PSRAM (KB) | Notes |
|-----------|-----------|------------|-------|
| ESP-IDF / FreeRTOS kernel | 60 | 0 | Scheduler, task stacks (system tasks) |
| Wi-Fi stack (lwIP + Wi-Fi) | 80 | 0 | lwIP on IRAM, buffers on DRAM [#12] |
| TLS session (mbedTLS) | 45 | 0 | Per-connection; ~40–50 KB [#7] |
| WebSocket client state | 5 | 0 | arduinoWebSockets overhead |
| Opus encoder state | 2 | 30 | ~30 KB pseudostack allocated in PSRAM [#3] |
| Opus decoder state | 2 | 30 | Same [#3] |
| Audio ring buffer (input) | 0 | 3 | DMA → encode ring; in PSRAM |
| Output jitter buffer (decoded) | 10 | 0 | 300 ms × 640 B/frame = 9.6 KB; in SRAM for DMA |
| DMA I2S buffers (TX+RX) | 4 | 0 | 4 × 512 B × 2 directions |
| M5Unified + codec init | 20 | 0 | BSP init footprint |
| FreeRTOS task stacks (×3 + main) | 24 | 0 | 3×8KB + 4KB |
| Application code + heap | 30 | 0 | Conservative estimate |
| **SRAM total (peak)** | **~282 KB** | — | Out of 512 KB; ~230 KB headroom |
| **PSRAM total (peak)** | — | **~65 KB** | Out of 8192 KB; vast headroom |

The 8 MB PSRAM is very comfortable. SRAM is the constraint, but 230 KB headroom is adequate. The display frame buffer (if used in v2) would consume 150 KB PSRAM (320×240×2 bytes), still leaving >7.9 MB PSRAM free.

Key: Opus codec pseudostacks (~30 KB each) in PSRAM keep SRAM well below its limit. [#3]

---

## 9. Power & Battery (v1: Scoped Not Solved)

### Open Question

Battery life under always-on Wi-Fi + active audio is an open question for v1. The device ships with a 500 mAh Li-ion battery managed by the AXP2101 PMU. [#1][#6]

### Estimates

No published lab measurements for CoreS3 specifically under Wi-Fi + audio active were found in search. Extrapolating from:
- Older M5Stack (ESP32 original): 80–100 mA active with Wi-Fi [#6]
- ESP32-S3 datasheet Wi-Fi TX peak: ~240 mA
- AW88298 speaker amp at 1W into 4Ω: up to 250 mA peak, ~60 mA at conversation volume
- ES7210 ADC: ~15 mA active
- IPS display (off in v1): ~80 mA at full brightness (not relevant)

Rough worst-case for Wi-Fi connected + audio streaming + display off: **200–300 mA**.

At 500 mAh ÷ 250 mA average = **~2 hours** before the battery is depleted. The device is expected to be used in short bursts (5–15 min conversations); between sessions it idles in `IDLE` state with Wi-Fi connected but no audio flowing, which reduces to approximately 80–120 mA.

In practice the device will be used on USB-C power (plugged into a wall socket or USB hub in the kids' room), making battery runtime a secondary concern for v1. The AXP2101 will charge the battery when USB is present.

### v2 Mitigation Options (Not Designed)

- Wi-Fi sleep mode between sessions (saves ~50 mA).
- Display off enforced in v1 (already planned).
- Deep sleep during long idle periods (> 5 min) with Wi-Fi reconnect on touch wakeup.
- Reduce Opus encoding to 8 kbps to lower CPU and therefore dynamic power.
- Measure precisely using AXP2101's built-in coulomb counter via M5Unified's `Axp.getBatCurrent()` API.

---

## 10. Wake-Word Options (v2 Only — Documented, Not Selected)

Wake word is explicitly a non-goal for v1. This section documents the two candidates for v2 evaluation.

### Option A: ESP-SR WakeNet (Espressif)

- **What it is:** Neural network wake-word engine built into the ESP-SR library. WakeNet9 runs on ESP32-S3 with the AI accelerator (vector instructions). [#10]
- **License:** Free for use on Espressif silicon. No runtime royalty.
- **Available wake words:** "Hi ESP", "Hi Lexin" (free). Custom words require either Espressif's training pipeline (samples from 500+ speakers required) or the new TTS-pipeline v3 (added 2026-04-23 per changelog) which may support Polish in future updates.
- **Integration:** Uses the AFE (Audio Front End) framework, which also includes AEC and noise suppression. Sample rate fixed at 16 kHz. CPU cost: ~22% of one core for full AFE pipeline. [#11]
- **Relevance:** Native integration, no external dependencies. The AFE framework is well-tested on ESP32-S3 Korvo and ESP-BOX hardware.
- **Limitation:** Polish wake words are not currently supported by the self-service training pipeline. Custom Polish word ("Hej Radek", "Hej walkie") would require Espressif professional support or waiting for the Polish TTS-pipeline expansion.

### Option B: Picovoice Porcupine

- **What it is:** Cross-platform wake word engine with pre-trained and custom models via the Picovoice Console web interface. Runs on ESP32 (MCU SDK available). [#10]
- **License:** Free tier for personal/non-commercial projects; commercial deployment requires a paid Picovoice subscription. The free tier has usage restrictions.
- **Polish support:** Picovoice Console supports creating custom wake words in any language including Polish, via a web interface with no large training dataset required.
- **Accuracy:** Generally considered more accurate than WakeNet on held-out benchmarks, though no direct ESP32-S3 head-to-head with WakeNet9 was found.
- **Limitation:** License terms are restrictive; not suitable for commercial products without paying. For a personal kids' device this may be acceptable. SDK adds ~200 KB flash overhead.

### Option C: microWakeWord (ESPHome / openWakeWord)

A third option not in the original brief but worth noting: `microWakeWord` (used in ESPHome for Home Assistant Voice) is an open-source MIT-licensed wake word engine designed for ESP32-S3. It uses synthetic-only training data and runs comfortably on the ESP32-S3. Polish models would need training. The ESPHome M5CoreS3 integration already uses this stack. [#6]

**Recommendation (not yet made):** Evaluate microWakeWord first due to MIT license and ESPHome integration proof. If accuracy is insufficient, evaluate Porcupine under its free personal tier. WakeNet is a last resort unless Polish language support matures.

---

## 11. Existing CoreS3 References to Mine

### ElatoAI (github.com/akdeb/ElatoAI) — Primary Template [#4]

**Relevance:** Closest architectural match. ESP32-S3 device → Deno edge server → ElevenLabs ConvAI via WSS + Opus. Proves the end-to-end latency target (<2 s round-trip) is achievable on this hardware class.

**What to copy:** WSS Opus binary frame transport design; `arduinoWebSockets` + `arduino-libopus` + `arduino-audio-tools` library stack; FreeRTOS task split (network / audio); `platformio.ini` structure (board=esp32-s3-devkitc-1, update to m5stack-cores3); no-PSRAM proof (device runs without PSRAM; we have 8 MB — even better).

**What to skip:** Deno edge server (replaced by our Python bridge); MAX98357A + generic I2S mic hardware init (replaced by M5Unified BSP for AW88298 + ES7210); 24 kHz / 12 kbps audio settings (we use 16 kHz / 16 kbps to match ElevenLabs ConvAI native); Supabase/Next.js dashboard (we have our own FastAPI admin).

### ESPHome M5CoreS3 (m5stack/M5CoreS3-Esphome, devices.esphome.io) — Audio HAL Reference [#6]

**Relevance:** Only fully open, CoreS3-specific reference for the audio HAL. Shows exact GPIO assignments (verified our §2 table), AXP2101 power rail config, AW88298 and ES7210 component integration, and the shared I2S bus arrangement.

**What to copy:** GPIO table (BCK=34, WCK=33, DIN=13, DOUT=14, MCLK=0). AXP2101 rail sequence (ALDO1 before AW88298 enable). The `external_components` block showing `aw88298` from the M5Stack ESPHome YAML repo.

**What to skip:** ESPHome YAML config format (we use Arduino C++); ESPHome's microWakeWord pipeline; the ESPHome voice assistant entity model.

### M5Stack OpenAI Voice (docs.m5stack.com/en/guide/realtime/openai/m5cores3) — Proof of Concept [#1]

**Relevance:** M5Stack's own demo that CoreS3 + audio + AI = working. Confirms the hardware is capable of real-time voice AI.

**What to skip:** Everything — it is a closed binary, targets OpenAI (not ElevenLabs), and is not open source. Use only as confidence evidence.

### XiaoZhi ESP32 (github.com/78/xiaozhi-esp32) — Architecture Reference [#13]

**Relevance:** MIT-licensed ESP32-S3 voice assistant that supports multiple AI backends and MCP tool protocol. Has a mature character-switching model (calling friends by name) that parallels our `switch_character` mechanism.

**What to copy:** Concept of stateful character roster on device; the name-based activation pattern.

**What to skip:** XiaoZhi's own cloud server; MCP protocol (we use our own JSON control messages); Mandarin-first voice profiles.

### FabrikappAgency/esp32-realtime-voice-assistant [#13]

**Relevance:** Detailed dev.to writeup on building real-time voice assistant with ESP32 + Node.js edge server + WebSocket. Good reference for buffer sizing and the WebSocket latency discussion.

**What to copy:** Buffer sizing observations; the decision to use a Node.js/server bridge (validates our Python bridge approach).

**What to skip:** Node.js server (we use Python FastAPI); generic ESP32 hardware (we have CoreS3 with integrated codecs).

### KALO-ESP32-Voice-ChatGPT (github.com/kaloprojects/KALO-ESP32-Voice-ChatGPT) [#13]

**Relevance:** ESP32 device with ElevenLabs STT + Groq LLM + TTS. Direct proof that ElevenLabs + ESP32 voice pipelines work end-to-end.

**What to copy:** ElevenLabs API call patterns from embedded C++; audio format handling snippets.

**What to skip:** The STT→LLM→TTS pipeline architecture (we use ConvAI which bundles all three); SD card audio storage (we stream over WSS).

---

## 12. Risks & Open Questions

### RQ-1: Shared I2S Bus Duplex on CoreS3

**Question:** Does `esp_codec_dev` (as wrapped by M5Unified) cleanly support simultaneous `read` (ES7210 mic) and `write` (AW88298 speaker) on the shared BCK/WCK bus?

**Evidence:** The ESP-IDF I2S driver v5.x supports full-duplex via `i2s_new_channel()` allocating both TX and RX handles together. The `esp_codec_dev` BSP pattern on ESP-BOX allocates both handles and creates separate IN and OUT `esp_codec_dev_handle_t` objects that share a single `audio_codec_i2s_data_if`. Both TX and RX DMA channels run independently. The ESPHome M5CoreS3 config confirms the same shared bus for both directions. [#6][#11]

**Constraint:** TX and RX must be configured at the same sample rate and bit depth because they share BCK/LRCK. This is satisfied by our choice of 16 kHz 16-bit for both directions.

**Risk level:** Low-Medium. The pattern is well-established on ESP-BOX; CoreS3 uses the same chip family. Initialisation order (allocate both channels before enabling either) is the known footgun.

**Mitigation:** Follow the `i2s_new_channel()` duplex allocation pattern. Test audio-in-while-audio-out early. If M5Unified's convenience wrapper does not support this, use `esp_codec_dev` directly.

### RQ-2: ElevenLabs Python SDK `AudioInterface` Contract Stability

**Question:** How stable is the `AudioInterface` ABC? Could an SDK update change the contract between device arrival and implementation?

**Evidence:** The four methods (`start`, `stop`, `output`, `interrupt`) have been stable since the interface was introduced. The SDK is actively maintained with frequent releases (checking PyPI: `elevenlabs` package has multiple 2025 releases). The abstract base is a published contract, not an internal API. [#5][#8]

**Risk level:** Low. Pin the SDK version in `pyproject.toml` (`elevenlabs>=1.0,<2.0`). The breaking change risk across minor versions is low given the interface is fundamental to the library's documented extensibility.

**Mitigation:** Pin version. Add an integration test that instantiates a minimal `CoreS3AudioInterface` and calls all four methods as a smoke test.

### RQ-3: Acoustic Echo Cancellation (AEC)

**Question:** The ES7210 microphone and AW88298 speaker are in the same physical enclosure (50–60 mm apart estimated). When the speaker plays the agent's voice, the microphone picks it up and sends it back to ElevenLabs as "user speech." This creates a feedback loop that can confuse ASR and the agent.

**Evidence:** The ESP-SR AFE framework provides AEC for ESP32-S3. It requires a "reference channel" (R) — a copy of the speaker playback signal — interleaved with microphone channels (e.g., channel layout "MR"). The reference is the digital signal being sent to the speaker, not a physical pickup. Sample rate is fixed at 16 kHz. CPU cost: ~22% of one core for full AFE (AEC + NS + VAD). [#11]

ElatoAI does not document AEC mitigation; its DevKitC reference hardware uses a MAX98357A analog amp with no reference signal path back to the ESP32, suggesting ElatoAI relies on ElevenLabs' server-side VAD and turn-taking to prevent feedback. In practice, with a push-to-talk model (PTT open only when user speaks), AEC is less critical because the speaker should be silent during PTT-active periods. However, if `output()` is still draining the jitter buffer when PTT opens (tail of agent response), echo will occur.

**Risk level:** Medium. PTT model reduces but does not eliminate echo. The agent's TTS response may still be trickling through the jitter buffer when the user immediately presses PTT after the agent finishes. ElevenLabs server-side VAD may suppress this, but it adds latency.

**Mitigation options (in order of preference):**
1. **PTT gate the I2S TX:** When `session_end` is received (user releases PTT), immediately mute the speaker output. When `session_start` fires, delay mic capture by 50 ms to let the speaker fully quiesce.
2. **Implement ESP-SR AEC:** Feed the I2S TX DMA output as the reference channel to `esp_afe_sr_v2_handle_t`. This is v2 work but the ESP-SR library is available on Arduino-ESP32.
3. **Reduce speaker volume** when mic is active (half-duplex style audio ducking).

### RQ-4: Latency Target

**Question:** ElatoAI claims <2 s round-trip (device → edge server → ElevenLabs → edge server → device). Our path adds one extra hop: CoreS3 → Python bridge → ElevenLabs Python SDK → ElevenLabs cloud → Python SDK → Python bridge → CoreS3. The Python SDK manages its own WebSocket to ElevenLabs, so the actual latency add is the Python bridge processing overhead.

**Analysis:**
- ElatoAI: CoreS3 → Deno edge (50 ms LAN/Tailscale) → ElevenLabs cloud (150–400 ms ASR+LLM+TTS) → Deno → CoreS3. Total: ~700 ms–1.5 s typical, <2 s target.
- Our path: CoreS3 → FastAPI bridge (50 ms) → ElevenLabs Python SDK (0 ms overhead; manages its own persistent WS) → ElevenLabs cloud (150–400 ms) → SDK → bridge → CoreS3. The extra hop is functionally the same as Deno.
- Additional latency sources: Opus encode/decode round-trip (~5 ms per direction), jitter buffer (200 ms at 300 ms target).

**Estimated total:** 600 ms–1.7 s, well within the <2 s target. The jitter buffer is the dominant added latency vs. ElatoAI.

**Risk level:** Low. The Python bridge adds negligible CPU overhead.

### RQ-5: Per-Device Auth & Multi-Tenancy

**Question:** The current Python backend is single-user (one family, shared agents, single Tailscale network). The CoreS3 adds a device concept but does not fundamentally change the tenancy model for v1.

**Current state:** The backend has no authentication on its WebSocket or REST endpoints except the `x-tool-secret` header on `/api/tools/send-message` and the lockdown middleware.

**v1 requirement:** The CoreS3 WebSocket endpoint authenticates via `device_token` (per-device token in NVS). This prevents any ESP32 on the same Tailscale network from connecting without a provisioned token. It does not provide multi-tenancy (multiple families on one server) — that is explicitly out of scope.

**Risk level:** Low for v1 (single family, private Tailscale). Document as a limitation.

---

## 13. Test Plan (Pre-Device)

The following tests can be run without the physical CoreS3. They verify the server-side code before the device arrives.

### T1: Python SDK AudioInterface Stub

Write a `StubCoreS3AudioInterface` that implements `AsyncAudioInterface` and records all `output()` calls and `interrupt()` calls. Use it to instantiate a real `Conversation` object against a real ElevenLabs agent. Verify the session opens, audio flows into `input_callback`, and the stub receives PCM chunks from `output()`.

**Tool:** `pytest-asyncio`, `elevenlabs` Python SDK, a valid ElevenLabs API key and agent ID from the dev environment.

### T2: Opus Codec Round-Trip

Generate a known PCM tone (440 Hz, 16 kHz, 1 s). Encode with `opus_codec.OpusEncoder`. Decode with `opus_codec.OpusDecoder`. Verify the decoded signal is perceptually identical (PESQ or SNR check, or simple amplitude check). This confirms `opuslib` is correctly installed and the codec parameters are consistent.

**Tool:** `pytest`, `opuslib`, `numpy`.

### T3: WebSocket Wire Protocol Simulation

Run the FastAPI server locally. Open a WebSocket client (Python `websockets` library) to `ws://localhost:8000/ws/cores3?device_token=<test_token>`. Verify:
- `hello` message is received with correct schema.
- Sending a `session_start` JSON frame causes `session_started` response.
- Sending valid Opus binary frames (from a pre-recorded PCM → Opus file) causes the ElevenLabs session to receive audio (verify via transcript callback).
- Sending `session_end` JSON frame causes `session_ended` response.
- Sending `switch_character` changes the active agent.

**Tool:** `pytest-asyncio`, `websockets`, `opuslib`.

### T4: Tool Routing

Unit test each tool callback in `cores3_bridge.py`:
- `send_message` → mock the internal HTTP call → verify the `/api/tools/send-message` handler is invoked with correct parameters.
- `recall_memory` → mock the memory store → verify correct agent+query is passed.
- `switch_character` → verify the bridge closes the current ElevenLabs session and queues the character swap.

**Tool:** `pytest`, `unittest.mock`.

### T5: PlatformIO Firmware Build (CI Smoke Test)

Run `pio run -e m5stack-cores3` on the laptop without a connected device. Verify the firmware compiles and links without errors. This catches dependency resolution issues, API mismatches with library versions, and missing include files.

**Tool:** PlatformIO CLI, `pio run`, runs on macOS without hardware.

### T6: ElatoAI Smoke Test (if a spare ESP32-S3 DevKit is available)

Flash the unmodified ElatoAI firmware to a bare ESP32-S3-DevKitC-1 (if available, not the CoreS3). Point it at a local Deno edge (ElatoAI provides docker-compose). Verify audio round-trip works. This de-risks the Opus/WSS/ESP32-S3 stack before CoreS3-specific hardware brings in the codec layer.

**Tool:** ElatoAI repo, Deno, generic ESP32-S3 devkit (optional).

---

## 14. Glossary

**AEC (Acoustic Echo Cancellation):** Signal processing technique that removes the speaker's playback audio from the microphone input, preventing the device from hearing itself. Required for full-duplex (simultaneous speak+listen) operation. ESP-SR provides an AEC module for ESP32-S3. [#11]

**ASR (Automatic Speech Recognition):** Speech-to-text. In this system, ASR is performed by ElevenLabs ConvAI on their cloud servers; no on-device ASR in v1.

**AW88298:** Texas Instruments / AWINIC class-D I2S amplifier IC. On CoreS3 it drives a 1W speaker. Configured over I2C; receives PCM data over I2S. [#1]

**BSP (Board Support Package):** A library that abstracts the hardware-specific initialization for a development board. The official M5Stack CoreS3 BSP v3.0.2 wraps the AW88298 and ES7210 drivers via `esp_codec_dev`. [#2]

**ConvAI:** ElevenLabs Conversational AI — the cloud service that handles ASR, LLM (claude-sonnet-4-6 in this project), and TTS in a single WebSocket session. All 11 characters in this project are ConvAI agents.

**ES7210:** EVEREST SEMI quad-channel I2S PDM ADC. On CoreS3 it converts the dual MEMS microphone analog signals to I2S digital audio. Controlled over I2C. [#1]

**esp_codec_dev:** Espressif open-source component that provides a unified `esp_codec_dev_handle_t` abstraction over different audio codec chips (ES7210, ES8311, AW88298, etc.). Used internally by the M5Stack CoreS3 BSP. [#2]

**Opus:** Open, royalty-free audio codec developed by Xiph.Org and standardised as RFC 6716. Designed for real-time voice and music over IP networks. Has two internal modes: SILK (optimised for speech at low bitrates, <12 kbps) and CELT (optimised for music, wider bandwidth). The ESP32-S3 port (`micro-opus`, `arduino-libopus`) uses Xtensa DSP optimisations. [#3]

**PSRAM:** Pseudo-Static RAM — external DRAM accessed via SPI. On CoreS3 it is 8 MB, connected via Octal SPI for higher bandwidth than QSPI. Slower than internal SRAM but orders of magnitude larger. Used for Opus codec pseudostacks and audio ring buffers. [#1]

**PTT (Push-to-Talk):** Interaction model where the user presses (and holds, or taps) a button to activate the microphone. The existing browser client uses PTT. CoreS3 v1 uses the touchscreen as the PTT trigger.

**PSRAM:** See above.

**WSS (WebSocket Secure):** WebSocket protocol over TLS (wss:// URI). Equivalent to HTTPS for WebSockets. Used for all CoreS3 ↔ Python bridge communication.

---

## 15. Sources

Sources accessed 2026-06-01. Quality tags: **primary** = official documentation or source code; **secondary** = community-maintained reference; **forum** = community discussion.

| # | URL | Description | Quality |
|---|-----|-------------|---------|
| 1 | https://docs.m5stack.com/en/core/CoreS3 | M5Stack official CoreS3 hardware documentation | primary |
| 2 | https://components.espressif.com/components/espressif/m5stack_core_s3/versions/3.0.2/readme | Espressif component registry — CoreS3 BSP v3.0.2 README | primary |
| 3 | https://components.espressif.com/components/esphome/micro-opus | Espressif component registry — esphome/micro-opus (Xtensa DSP Opus) | primary |
| 4 | https://github.com/akdeb/ElatoAI | ElatoAI GitHub repository — ESP32-S3 ConvAI template | primary |
| 4b | https://cookbook.openai.com/examples/voice_solutions/running_realtime_api_speech_on_esp32_arduino_edge_runtime_elatoai | OpenAI Cookbook writeup of ElatoAI | secondary |
| 5 | https://github.com/elevenlabs/elevenlabs-python | ElevenLabs official Python SDK — AudioInterface source | primary |
| 6 | https://devices.esphome.io/devices/m5stack-cores3 | ESPHome device page — M5Stack CoreS3 audio HAL reference | secondary |
| 7 | https://docs.m5stack.com/en/guide/realtime/openai/m5cores3 | M5Stack OpenAI Voice Assistant demo page | primary |
| 8 | https://deepwiki.com/elevenlabs/elevenlabs-python/4.4.4-audio-interface | DeepWiki — ElevenLabs AudioInterface documented analysis | secondary |
| 9 | https://pypi.org/project/opuslib/ | opuslib PyPI page | primary |
| 10 | https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/README.html | ESP-SR WakeNet documentation for ESP32-S3 | primary |
| 11 | https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/audio_front_end/README.html | ESP-SR Audio Front-end (AFE + AEC) for ESP32-S3 | primary |
| 12 | https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2s.html | ESP-IDF I2S driver documentation for ESP32-S3 | primary |
| 13 | https://github.com/FabrikappAgency/esp32-realtime-voice-assistant | FabrikappAgency ESP32 realtime voice assistant | secondary |
| 13b | https://github.com/kaloprojects/KALO-ESP32-Voice-ChatGPT | KALO ESP32 Voice ChatGPT project | secondary |
| 13c | https://github.com/78/xiaozhi-esp32 | XiaoZhi ESP32 AI voice assistant (MIT) | secondary |
| 14 | https://elevenlabs.io/docs/conversational-ai/libraries/python | ElevenLabs official Python SDK documentation | primary |
| 15 | https://community.m5stack.com/topic/163/power-consumption | M5Stack community — power consumption discussion | forum |
| 16 | https://github.com/esphome/esphome/pull/8879 | ESPHome PR #8879 — I2S FPU core-pinning gotcha | primary |
| 17 | https://shop.m5stack.com/products/m5stack-cores3-esp32s3-iotdevelopment-kit | M5Stack CoreS3 product page | primary |
| 18 | https://github.com/elevenlabs/elevenlabs-python/issues/495 | ElevenLabs SDK issue — custom WebSocket audio interface | forum |
| 19 | https://deepwiki.com/wireless-tag-com/ESP_BOX_CONNECT_AI/5.3-audio-codec-interface | DeepWiki — esp_codec_dev full-duplex pattern | secondary |
| 20 | https://deepwiki.com/espressif/esp-sr/3-wake-word-detection-(wakenet) | DeepWiki — WakeNet architecture | secondary |
| 21 | https://www.espressif.com/en/solutions/audio-solutions/esp-afe | Espressif AFE solutions page | primary |

---

*End of CoreS3 Port Spec v1.0 — 2026-06-01*
