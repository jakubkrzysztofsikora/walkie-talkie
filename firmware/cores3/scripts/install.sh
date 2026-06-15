#!/usr/bin/env bash
#
# install.sh — one-shot CoreS3 build-station bootstrap for the Mac Studio
#
# Idempotent: safe to run multiple times. Each step checks whether work is
# already done before acting.
#
# Usage:
#   ssh jakubsikora@mac-studio-jakub.tail5d39b4.ts.net 'bash -s' < install.sh
#   # or run directly on the Studio:
#   ./install.sh
#
# See: firmware/cores3/README.md, docs/cores3-deploy.md

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
ok()  { echo -e "${GREEN}[OK]${NC} $*"; }
warn(){ echo -e "${YELLOW}[WARN]${NC} $*"; }
fail(){ echo -e "${RED}[FAIL]${NC} $*"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PIO_PATH="/Users/jakubsikora/Library/Python/3.9/bin"
VENV_PY="${HOME}/walkie-talkie/.venv/bin/python"

# ---- 1. Preflight -----------------------------------------------------------
echo "==> CoreS3 build-station bootstrap"
echo "    firmware dir: $FIRMWARE_DIR"

[[ "$(uname)" == "Darwin" ]] || fail "macOS only"
[[ "$(uname -m)" == "arm64" ]] || fail "ARM64 only"
ok "macOS ARM64 confirmed"

# ---- 2. Homebrew: libopus ---------------------------------------------------
# brew is not on PATH in non-interactive SSH; use absolute path.
BREW=/opt/homebrew/bin/brew
if [ ! -x "$BREW" ]; then
    warn "Homebrew not found at $BREW — skipping brew deps"
else
    echo "==> Checking libopus (brew)"
    if $BREW list opus &>/dev/null; then
        ok "libopus already installed"
    else
        echo "    Installing libopus via Homebrew..."
        $BREW install opus && ok "libopus installed" || warn "brew install opus failed (non-fatal for firmware builds)"
    fi
fi

# ---- 3. PlatformIO Core (system Python 3.9, NOT the .venv) ------------------
echo "==> Checking PlatformIO Core"
export PATH="$PIO_PATH:$PATH"
if command -v pio &>/dev/null; then
    ok "pio $(pio --version 2>&1 | head -1) already installed"
else
    echo "    Installing PlatformIO..."
    pip3 install platformio && ok "pio installed" || fail "pip3 install platformio failed"
fi

# ---- 4. ESP32-S3 platform + toolchain ---------------------------------------
echo "==> Checking ESP32-S3 platform"
if pio platform list 2>/dev/null | grep -q espressif32; then
    ok "espressif32 platform already installed"
else
    echo "    Installing espressif32@6.10.0 (~800 MB download)..."
    pio platform install espressif32@6.10.0 && ok "espressif32 installed" || fail "platform install failed"
fi

# ---- 5. Firmware library dependencies ----------------------------------------
echo "==> Checking firmware library dependencies"
if [ -d "$FIRMWARE_DIR/.pio/libdeps/m5stack-cores3" ]; then
    COUNT=$(find "$FIRMWARE_DIR/.pio/libdeps/m5stack-cores3" -maxdepth 1 -mindepth 1 -type d | wc -l | tr -d ' ')
    ok "$COUNT library dirs already cached"
else
    echo "    Installing lib_deps (M5Unified, WebSockets, micro-opus, audio-tools, M5GFX, ArduinoJson)..."
    cd "$FIRMWARE_DIR"
    pio pkg install && ok "lib_deps cached" || warn "pkg install had issues (build will retry resolution)"
fi

# ---- 6. Python opuslib (for server-side P2 opus_codec.py) -------------------
echo "==> Checking opuslib (server venv)"
if "$VENV_PY" -c "import opuslib" 2>/dev/null; then
    ok "opuslib already in server venv"
else
    echo "    Installing opuslib..."
    "$VENV_PY" -m pip install opuslib && ok "opuslib installed" || warn "opuslib install failed (P2 tests need it)"
fi

# ---- 7. Smoke test: compile -------------------------------------------------
echo "==> Smoke test: pio run"
cd "$FIRMWARE_DIR"
if pio run 2>&1 | tail -5 | grep -q "SUCCESS"; then
    ELF_SIZE=$(stat -f%z .pio/build/m5stack-cores3/firmware.elf 2>/dev/null || echo 0)
    ok "Firmware compiles (firmware.elf = $(numfmt --to=iec $ELF_SIZE 2>/dev/null || echo ${ELF_SIZE} bytes))"
else
    warn "Build had issues — check output above"
fi

# ---- Done -------------------------------------------------------------------
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  CoreS3 build station ready.${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "  Flash the device:  cd firmware/cores3 && ./scripts/flash.sh"
echo "  Firmware README:   firmware/cores3/README.md"
echo ""
