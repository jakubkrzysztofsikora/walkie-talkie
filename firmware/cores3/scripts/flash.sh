#!/usr/bin/env bash
#
# flash.sh — compile, upload, and verify CoreS3 firmware
#
# Detects the CoreS3 USB port automatically. Run directly on the Mac Studio
# or via deploy_to_cores3.sh from your laptop.
#
# Usage:
#   ./flash.sh              # build + flash + verify
#   ./flash.sh --no-build   # flash last build + verify (skip compile)
#   ./flash.sh --monitor    # just open serial monitor (no flash)
#
# See: firmware/cores3/README.md

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PIO_PATH="/Users/jakubsikora/Library/Python/3.9/bin"
export PATH="$PIO_PATH:$PATH"

MODE="${1:-full}"

# ---- Detect CoreS3 port ------------------------------------------------------
detect_port() {
    # The CoreS3 shows as /dev/cu.usbmodem* (ESP32-S3 built-in USB-serial-JTAG)
    local port
    port=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
    if [ -z "$port" ]; then
        # Fallback: try pio device list
        port=$(pio device list --json-output 2>/dev/null | python3 -c "
import json,sys
try:
    data = json.load(sys.stdin)
    for d in data:
        if 'usbmodem' in d.get('port','') or 'm5stack' in d.get('description','').lower():
            print(d['port'])
            break
except: pass" 2>/dev/null)
    fi
    echo "$port"
}

# ---- Build -------------------------------------------------------------------
if [ "$MODE" != "--no-build" ] && [ "$MODE" != "--monitor" ]; then
    echo "==> Building firmware..."
    cd "$FIRMWARE_DIR"
    pio run
    echo -e "${GREEN}[OK]${NC} Build succeeded"
fi

# ---- Flash -------------------------------------------------------------------
if [ "$MODE" != "--monitor" ]; then
    PORT=$(detect_port)
    if [ -z "$PORT" ]; then
        echo -e "${RED}[FAIL]${NC} No CoreS3 detected. Plug in the USB-C cable and try again."
        echo "    Expected: /dev/cu.usbmodem*"
        exit 1
    fi
    echo "==> Flashing CoreS3 at $PORT"
    cd "$FIRMWARE_DIR"
    pio run -t upload --upload-port "$PORT"
    echo -e "${GREEN}[OK]${NC} Flash complete"
fi

# ---- Monitor -----------------------------------------------------------------
PORT=$(detect_port)
if [ -n "$PORT" ]; then
    echo "==> Monitoring serial for 10s (115200 baud)"
    cd "$FIRMWARE_DIR"
    python3 -c "
import serial, time
try:
    s = serial.Serial('$PORT', 115200, timeout=1)
    time.sleep(2)  # let ESP32 boot after flash
    deadline = time.time() + 10
    while time.time() < deadline:
        line = s.readline()
        if line:
            print(line.decode(errors='replace'), end='')
    s.close()
except Exception as e:
    print(f'Serial error: {e}')
" 2>&1
    echo ""
    echo -e "${GREEN}[OK]${NC} Serial output captured"
else
    echo -e "${YELLOW}[WARN]${NC} Could not open serial monitor (no port found)"
fi
