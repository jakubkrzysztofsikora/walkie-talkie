#!/usr/bin/env python3
"""Radio bridge for the Walkie-Talkie Voice Agent.

Runs on any machine physically connected to a radio (Baofeng, Quansheng, etc.)
via a USB audio adapter or 3.5mm TRRS cable.  Streams received audio to the
FastAPI server over WebSocket, plays back TTS responses through the radio.

Hardware setup
--------------
Option A — USB audio adapter (recommended):
  Radio 3.5mm earphone  →  USB adapter MIC-IN  (pink jack)
  USB adapter PHONES    →  Radio 2.5mm mic      (via 3.5mm→2.5mm TRS adapter)

Option B — Mac / PC built-in 3.5mm:
  Requires a TRRS cable that maps radio speaker → TRRS sleeve (mic contact).
  Standard TRS cables will NOT work — audio lands on the wrong contacts.

PTT
---
  VOX mode (default):  set radio VOX sensitivity to 4-6; the played TTS audio
    will trigger transmit automatically.  Tune VOX_OUTPUT_GAIN so TTS keys the
    radio but ambient room noise does not.
  GPIO PTT (Raspberry Pi): wire an optocoupler between GPIO pin and radio PTT
    line.  Set PTT_GPIO_PIN in .env.

Usage
-----
  pip install -r requirements.txt
  cp .env.example .env && nano .env
  python radio_client.py                  # start bridge
  python radio_client.py --list-devices   # show audio device indices
"""

from __future__ import annotations

import argparse
import asyncio
import audioop
import logging
import os
import struct
import sys
import time
from typing import Optional

try:
    from dotenv import load_dotenv
    load_dotenv()
except ImportError:
    pass

# ---------------------------------------------------------------------------
# Configuration (all overridable via env / CLI args)
# ---------------------------------------------------------------------------

SERVER_URL = os.getenv("SERVER_URL", "ws://localhost:8000/ws/walkie-talkie/radio-bridge-1")
AUDIO_INPUT_DEVICE: Optional[int] = int(os.getenv("AUDIO_INPUT_DEVICE", "0")) if os.getenv("AUDIO_INPUT_DEVICE") else None
AUDIO_OUTPUT_DEVICE: Optional[int] = int(os.getenv("AUDIO_OUTPUT_DEVICE", "0")) if os.getenv("AUDIO_OUTPUT_DEVICE") else None
VOX_THRESHOLD = int(os.getenv("VOX_THRESHOLD", "500"))
VOX_OUTPUT_GAIN = float(os.getenv("VOX_OUTPUT_GAIN", "0.08"))
PTT_GPIO_PIN: Optional[int] = int(os.getenv("PTT_GPIO_PIN")) if os.getenv("PTT_GPIO_PIN") else None
AUDIO_SAMPLE_RATE = int(os.getenv("AUDIO_SAMPLE_RATE", "8000"))
AUDIO_FORMAT = os.getenv("AUDIO_FORMAT", "ulaw")
LOG_LEVEL = os.getenv("LOG_LEVEL", "INFO").upper()

logging.basicConfig(
    level=getattr(logging, LOG_LEVEL, logging.INFO),
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("radio_bridge")


# ---------------------------------------------------------------------------
# G.711 µ-law helpers (uses audioop for correctness)
# ---------------------------------------------------------------------------

def pcm_to_ulaw(pcm_bytes: bytes) -> bytes:
    """Encode 16-bit signed little-endian PCM to G.711 µ-law."""
    return audioop.lin2ulaw(pcm_bytes, 2) if pcm_bytes else b""


def ulaw_to_pcm(ulaw_bytes: bytes) -> bytes:
    """Decode G.711 µ-law to 16-bit signed little-endian PCM."""
    return audioop.ulaw2lin(ulaw_bytes, 2) if ulaw_bytes else b""


def rms(pcm_bytes: bytes) -> float:
    """Return RMS amplitude of 16-bit PCM bytes (range 0–32767)."""
    if not pcm_bytes:
        return 0.0
    n = len(pcm_bytes) // 2
    samples = struct.unpack(f"<{n}h", pcm_bytes[:n * 2])
    return (sum(s * s for s in samples) / n) ** 0.5


# ---------------------------------------------------------------------------
# GPIO PTT helper
# ---------------------------------------------------------------------------

class PttController:
    """Asserts PTT via GPIO relay/optocoupler; falls back silently for VOX."""

    def __init__(self, gpio_pin: Optional[int]) -> None:
        self._gpio = None
        self._pin = gpio_pin
        if gpio_pin:
            try:
                import RPi.GPIO as GPIO
                GPIO.setmode(GPIO.BCM)
                GPIO.setup(gpio_pin, GPIO.OUT, initial=GPIO.LOW)
                self._gpio = GPIO
                logger.info("GPIO PTT enabled on BCM pin %d", gpio_pin)
            except ImportError:
                logger.info("RPi.GPIO not installed — using VOX PTT only")
            except Exception as exc:
                logger.warning("GPIO init failed: %s — using VOX PTT", exc)

    def key(self) -> None:
        if self._gpio:
            self._gpio.output(self._pin, self._gpio.HIGH)

    def unkey(self) -> None:
        if self._gpio:
            self._gpio.output(self._pin, self._gpio.LOW)

    def cleanup(self) -> None:
        if self._gpio:
            try:
                self._gpio.cleanup()
            except Exception:
                pass


# ---------------------------------------------------------------------------
# RadioBridge
# ---------------------------------------------------------------------------

class RadioBridge:
    """Bidirectional audio bridge: radio soundcard ↔ FastAPI WebSocket server."""

    def __init__(self) -> None:
        self._ptt = PttController(PTT_GPIO_PIN)
        self._running = False
        self._playback_active = False  # half-duplex guard

    async def run(self) -> None:
        """Connect and run until interrupted. Reconnects on disconnect."""
        import websockets
        from websockets.asyncio.client import connect

        logger.info("Connecting to %s", SERVER_URL)
        while True:
            try:
                async with connect(
                    SERVER_URL,
                    ping_interval=20,
                    ping_timeout=30,
                    close_timeout=10,
                ) as ws:
                    self._running = True
                    logger.info("Connected to server")
                    async with asyncio.TaskGroup() as tg:
                        tg.create_task(self._capture_and_send(ws), name="capture")
                        tg.create_task(self._receive_and_play(ws), name="playback")

            except* websockets.ConnectionClosed as eg:
                logger.warning("Connection closed: %s — reconnecting in 3s", eg.exceptions[0])
            except* Exception as eg:
                logger.error("Bridge error: %s — reconnecting in 3s", eg.exceptions[0])
            finally:
                self._running = False

            await asyncio.sleep(3)

    # ------------------------------------------------------------------ #
    # Capture: radio → server
    # ------------------------------------------------------------------ #

    async def _capture_and_send(self, ws) -> None:
        import numpy as np
        import sounddevice as sd

        loop = asyncio.get_event_loop()
        chunk_frames = AUDIO_SAMPLE_RATE // 10  # 100 ms

        def _callback(indata, frames, time_info, status):
            if status:
                logger.debug("sounddevice status: %s", status)
            if self._playback_active:
                return  # half-duplex: suppress mic during TTS playback

            pcm = (np.clip(indata[:, 0], -1.0, 1.0) * 32767).astype("int16").tobytes()
            if rms(pcm) > VOX_THRESHOLD:
                audio = pcm_to_ulaw(pcm) if AUDIO_FORMAT == "ulaw" else pcm
                future = asyncio.run_coroutine_threadsafe(ws.send(audio), loop)
                future.add_done_callback(
                    lambda f: logger.debug("send error: %s", f.exception())
                    if f.exception() else None
                )

        logger.info(
            "Capturing from device %s at %d Hz (VOX threshold=%d)",
            AUDIO_INPUT_DEVICE, AUDIO_SAMPLE_RATE, VOX_THRESHOLD,
        )
        with sd.InputStream(
            samplerate=AUDIO_SAMPLE_RATE,
            channels=1,
            dtype="float32",
            blocksize=chunk_frames,
            device=AUDIO_INPUT_DEVICE,
            callback=_callback,
        ):
            while self._running:
                await asyncio.sleep(0.05)

    # ------------------------------------------------------------------ #
    # Playback: server → radio
    # ------------------------------------------------------------------ #

    async def _receive_and_play(self, ws) -> None:
        import numpy as np
        import sounddevice as sd

        loop = asyncio.get_event_loop()
        logger.info("Waiting for TTS audio from server...")

        async for message in ws:
            if not isinstance(message, bytes) or not message:
                continue  # skip text frames (transcripts, pings)

            try:
                pcm = ulaw_to_pcm(message) if AUDIO_FORMAT == "ulaw" else message
                samples = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0
                samples = (samples * VOX_OUTPUT_GAIN).reshape(-1, 1)

                self._playback_active = True
                self._ptt.key()
                await asyncio.sleep(0.1)  # PTT ramp-up before audio

                await loop.run_in_executor(
                    None,
                    lambda s=samples: sd.play(
                        s,
                        samplerate=AUDIO_SAMPLE_RATE,
                        device=AUDIO_OUTPUT_DEVICE,
                        blocking=True,
                    ),
                )
                await asyncio.sleep(0.4)  # VOX tail hold after playback

            except Exception as exc:
                logger.warning("Playback error: %s", exc)
            finally:
                self._ptt.unkey()
                self._playback_active = False

    def cleanup(self) -> None:
        self._ptt.cleanup()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def list_devices() -> None:
    try:
        import sounddevice as sd
        print(sd.query_devices())
    except ImportError:
        print("sounddevice not installed — run: pip install sounddevice")


async def _main() -> None:
    bridge = RadioBridge()
    try:
        await bridge.run()
    except KeyboardInterrupt:
        logger.info("Interrupted — shutting down")
    finally:
        bridge.cleanup()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Radio bridge for the Walkie-Talkie Voice Agent"
    )
    parser.add_argument("--list-devices", action="store_true", help="List audio devices and exit")
    parser.add_argument("--server-url", help="Override SERVER_URL")
    parser.add_argument("--vox-threshold", type=int, help="Override VOX_THRESHOLD")
    parser.add_argument("--input-device", type=int, help="Override AUDIO_INPUT_DEVICE")
    parser.add_argument("--output-device", type=int, help="Override AUDIO_OUTPUT_DEVICE")
    args = parser.parse_args()

    if args.list_devices:
        list_devices()
        sys.exit(0)

    global SERVER_URL, VOX_THRESHOLD, AUDIO_INPUT_DEVICE, AUDIO_OUTPUT_DEVICE
    if args.server_url:
        SERVER_URL = args.server_url
    if args.vox_threshold:
        VOX_THRESHOLD = args.vox_threshold
    if args.input_device is not None:
        AUDIO_INPUT_DEVICE = args.input_device
    if args.output_device is not None:
        AUDIO_OUTPUT_DEVICE = args.output_device

    asyncio.run(_main())


if __name__ == "__main__":
    main()
