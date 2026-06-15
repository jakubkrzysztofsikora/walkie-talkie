#!/usr/bin/env bash
#
# deploy_to_cores3.sh — push firmware changes to Mac Studio, build, flash, verify
#
# Mirrors deploy_to_studio.sh pattern: rsync → remote build → flash → verify.
# The Mac Studio hosts both the production server AND the CoreS3 build station.
#
# Usage:
#   ./deploy_to_cores3.sh                  # sync + build + flash + verify
#   ./deploy_to_cores3.sh --no-flash       # sync + build only (CI)
#   ./deploy_to_cores3.sh --build-only     # build only (no sync)
#   ./deploy_to_cores3.sh --flash-only     # flash last build (no sync)
#
# Auth: reads .cores3.deploy.env (gitignored) for SSHPASS + host.
# If SSHPASS is unset, uses your SSH key.

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

LOCAL_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$LOCAL_ROOT/.."  # repo root (walkie-talkie/)

# ---- Config ------------------------------------------------------------------
[ -f "$LOCAL_ROOT/.cores3.deploy.env" ] && set -a && . "$LOCAL_ROOT/.cores3.deploy.env" && set +a
STUDIO_HOST="${STUDIO_HOST:-jakubsikora@mac-studio-jakub.tail5d39b4.ts.net}"
REMOTE_ROOT="${REMOTE_ROOT:-/Users/jakubsikora/walkie-talkie/firmware/cores3}"
SSHCP="/tmp/wt-cores3-deploy-%r@%h:%p"
SSHOPTS="-o StrictHostKeyChecking=accept-new -o ConnectTimeout=15 -o ControlMaster=auto -o ControlPath=$SSHCP -o ControlPersist=45"

if [ -n "${SSHPASS:-}" ] && command -v sshpass >/dev/null 2>&1; then
    export SSHPASS
    # Force password auth — key auth hangs on the Studio unless explicitly configured
    PASS_OPTS="$SSHOPTS -o PreferredAuthentications=password -o PubkeyAuthentication=no"
    SSH() { sshpass -e ssh $PASS_OPTS "$STUDIO_HOST" "$@"; }
    RSYNC_RSH="sshpass -e ssh $PASS_OPTS"
else
    SSH() { ssh $SSHOPTS "$STUDIO_HOST" "$@"; }
    RSYNC_RSH="ssh $SSHOPTS"
fi

MODE="${1:-full}"

case "$MODE" in
    full|--no-flash|--build-only|--flash-only) ;;
    -h|--help)
        echo "Usage: $0 [full|--no-flash|--build-only|--flash-only]"
        echo "  full          Sync + build + flash (default)"
        echo "  --no-flash    Sync + build only"
        echo "  --build-only  Build only (no sync)"
        echo "  --flash-only  Flash last build (no sync)"
        exit 0
        ;;
    *)
        echo -e "${RED}Error: unknown flag '$MODE'${NC}"
        echo "Usage: $0 [full|--no-flash|--build-only|--flash-only]"
        exit 2
        ;;
esac

# ---- 1. Sync code ------------------------------------------------------------
if [ "$MODE" != "--build-only" ] && [ "$MODE" != "--flash-only" ]; then
    echo "==> Syncing firmware/cores3/ → $STUDIO_HOST:$REMOTE_ROOT/"
    rsync -az \
        --exclude '.pio/' --exclude '.vscode/' \
        --exclude 'src/config/secrets.h' \
        --exclude '*.deploy.env' \
        --exclude '__pycache__/' --exclude '*.pyc' \
        -e "$RSYNC_RSH" \
        "$LOCAL_ROOT/" "$STUDIO_HOST:$REMOTE_ROOT/"
    echo "    code synced"
fi

# ---- 2. Build on Studio ------------------------------------------------------
if [ "$MODE" != "--flash-only" ]; then
    echo "==> Building firmware on Studio"
    SSH "export PATH=\"/Users/jakubsikora/Library/Python/3.9/bin:\$PATH\"; cd '$REMOTE_ROOT' && pio run"
    echo "    build complete"
fi

# ---- 3. Flash on Studio ------------------------------------------------------
if [ "$MODE" != "--no-flash" ]; then
    echo "==> Flashing CoreS3"
    SSH "export PATH=\"/Users/jakubsikora/Library/Python/3.9/bin:\$PATH\"; cd '$REMOTE_ROOT' && bash scripts/flash.sh --no-build"
    echo "    flash complete"
fi

# ---- 4. Verify ---------------------------------------------------------------
echo "==> Done."
echo "    Serial monitor:  ssh $STUDIO_HOST 'cd $REMOTE_ROOT && bash scripts/flash.sh --monitor'"
