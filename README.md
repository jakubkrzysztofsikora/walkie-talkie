<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://img.shields.io/badge/ESP32--S3-240MHz-blue?style=for-the-badge">
  <img alt="ESP32-S3" src="https://img.shields.io/badge/ESP32--S3-240MHz-blue?style=for-the-badge">
</picture>
<img alt="Python" src="https://img.shields.io/badge/Python-3.11+-green?style=for-the-badge">

# Walkie-Talkie — AI Voice Agent for Kids

An open-source, push-to-talk AI companion for children. Built on the M5Stack CoreS3 and
ElevenLabs Conversational AI. Kids press and hold to talk, release to listen — no reading required.

<p align="center">
  <i>Hej Gniewko! Hej Ziemko! Tu Radek!</i>
</p>

---

## What It Does

A child picks up the CoreS3 handheld, taps the screen, speaks into the built-in mic,
and an AI character responds aloud. The system handles audio encoding, WebSocket
streaming, LLM orchestration, tool calling, and conversation auditing — all in real time.

The device shows an animated pixel-art mascot that blinks, smiles, and flaps its mouth
while the agent speaks. Eleven characters are available — switch anytime via long-press menu.

| Character | Personality | Accent Color |
|-----------|------------|--------------|
| Radek | Cheerful Polish buddy | Electric Cyan |
| Steve | Block-building gamer | Green |
| Simba | Brave lion cub | Orange |
| Creeper | Minecraft mischief | Lime |
| Pikachu | Electric companion | Yellow |
| *...and 6 more* | | |

---

## Architecture

```
┌─────────────────────┐      Opus/WebSocket       ┌──────────────────────────┐
│   M5Stack CoreS3    │◄──────────────────────────►│   FastAPI Server         │
│   (ESP32-S3)        │    audio + control msgs    │   (Python 3.11)          │
│                     │   ws://host:8000/ws/cores3 │                          │
│  • Touch PTT        │                            │  • Opus encode/decode    │
│  • Opus codec       │                            │  • Session mgmt          │
│  • Pixel mascot UI  │                            │  • Tool orchestration    │
│  • 11 characters    │                            │  • Audit logging         │
└─────────────────────┘                            └───────────┬──────────────┘
                                                               │
                                                    ┌──────────▼──────────────┐
                                                    │  ElevenLabs Conversational│
                                                    │  AI (WebSocket)          │
                                                    │                          │
                                                    │  • Speech-to-Text        │
                                                    │  • LLM (GPT-4o)          │
                                                    │  • Text-to-Speech         │
                                                    └──────────────────────────┘
```

**Server responsibilities**: audio codec bridge, ElevenLabs SDK integration, tool calling
(web search, sandboxed Python, parent messaging), conversation audit trail, camera/vision
pipeline, and admin API.

**Firmware responsibilities**: touch-driven PTT state machine, Opus compression,
WebSocket client, pixel-art mascot UI with character picker, WiFi reconnection.

---

## Quick Start

### Prerequisites

- Python 3.11+ with Poetry
- PlatformIO CLI (`pip install platformio`)
- M5Stack CoreS3 connected via USB-C
- ElevenLabs API key with Conversational AI access

### Server (macOS / Linux)

```bash
git clone https://github.com/jakubkrzysztofsikora/walkie-talkie.git
cd walkie-talkie/project

# Install dependencies
poetry install

# Install system Opus (required by opuslib)
brew install opus        # macOS
# sudo apt install libopus-dev   # Linux

# Create .env
cp .env.example .env
# Edit .env: set WALKIE_ELEVENLABS_API_KEY, WALKIE_ELEVENLABS_AGENT_ID

# Run the server
DYLD_LIBRARY_PATH=/opt/homebrew/lib poetry run python -m uvicorn walkie_agent.api:app \
  --host 0.0.0.0 --port 8000
```

### Firmware (CoreS3)

```bash
cd firmware/cores3

# Create secrets from example
cp src/config/secrets.h.example src/config/secrets.h
# Edit: set your WiFi SSID/PASS and server IP

# Build & flash
pio run -t upload --upload-port /dev/cu.usbmodem*
```

### Issue a Device Token

```bash
cd project
poetry run walkie-agent cores3 token issue --device-name kids-room-1
```

Copy the token into `firmware/cores3/src/config/secrets.h` as `DEVICE_TOKEN`.
Rebuild and flash the firmware.

---

## Hardware

- **M5Stack CoreS3** — ESP32-S3 @ 240MHz, 16MB Flash, 8MB PSRAM
- **Audio** — ES7210 ADC (mic array), AW88298 DAC (1W speaker)
- **Display** — 2.0" 320×240 IPS LCD (ILI9342C), capacitive touch (FT6336)
- **Shared I2S bus** — GPIO34 (BCK), GPIO33 (WS), GPIO14 (DIN), GPIO13 (DOUT)

> **Known limitation**: ESP-IDF v4.4.7 (Arduino-ESP32 v2.x) only supports half-duplex
> I2S on CoreS3. Speaker and mic cannot be active simultaneously. This firmware runs
> speaker-only; mic input comes from the web UI at `http://<server>:8000/`.

---

## Repository Structure

```
walkie-talkie/
├── firmware/cores3/           # ESP32-S3 PlatformIO project
│   ├── src/
│   │   ├── main.cpp           # Firmware entry point + UI
│   │   ├── audio/opus_stub.*  # libopus Arduino wrapper
│   │   └── config/secrets.h   # WiFi, token (gitignored)
│   ├── scripts/               # Build, flash, deploy helpers
│   └── platformio.ini
├── project/                   # Python FastAPI backend
│   ├── walkie_agent/
│   │   ├── api.py             # REST + WebSocket endpoints
│   │   ├── cores3_session.py  # CoreS3 session handler
│   │   ├── cores3_audio.py    # ElevenLabs audio bridge
│   │   ├── opus_codec.py      # Server-side Opus encode/decode
│   │   ├── memory_store.py    # Conversation audit + RAG
│   │   └── tools/             # Agent tools (search, sandbox, etc.)
│   └── web/                   # Browser client (fallback mic)
├── thoughts/shared/           # Research, plans, diagrams
└── SPEC.md                    # Full system specification
```

---

## Development

### Server tests

```bash
cd project
poetry run pytest
```

### Firmware debug

```bash
cd firmware/cores3
pio device monitor --port /dev/cu.usbmodem*
```

Watch for `[idle]` heartbeat lines and `→ SESSION_ACTIVE` / `→ IDLE` state
transitions. Stack overflows appear as `***ERROR*** A stack overflow in task loopTask`.

### Stack sizing

The UI + WebSocket + Opus codec needs ~28KB of loop task stack. This is set via:

```cpp
size_t getArduinoLoopTaskStackSize(void) { return 28672; }
```

If you add features that increase stack usage (more sprites, deeper call stacks),
increase this value. The ESP32-S3 has 512KB total DRAM.

---

## Roadmap

- [ ] **IDF v5 migration** — Arduino-ESP32 v3.x for native full-duplex I2S (mic + speaker simultaneously)
- [ ] Per-character pixel sprites (11 unique 8×8 art assets)
- [ ] Sound effects on state transitions (bleeps, pops, buzzes)
- [ ] External PDM mic on Grove port (hardware fallback for IDF v4)
- [ ] OTA firmware updates
- [ ] Battery-optimized deep sleep

---

## Contributing

Issues and PRs welcome. The project is active but early-stage — expect things to
move fast. Check `thoughts/shared/` for research docs and implementation plans.

### Commit style

```
emoji type: brief description

feat: Gen Alpha pixel mascot UI
fix: stack overflow in loopTask
chore: update project submodule
```

---

## License

MIT © 2025-2026 Jakub Sikora

---

<p align="center">
  <sub>Built in Sosnowiec, Silesian Voivodeship, Poland</sub>
</p>
