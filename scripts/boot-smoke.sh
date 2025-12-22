#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
LOG_FILE="${LOG_FILE:-$BUILD_DIR/qemu-serial.log}"
TIMEOUT_SECS="${TIMEOUT_SECS:-20}"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --target iso

mkdir -p "$(dirname "$LOG_FILE")"
rm -f "$LOG_FILE"
: > "$LOG_FILE"

if ! command -v timeout >/dev/null 2>&1; then
    echo "boot-smoke: missing 'timeout' (coreutils). Install it or run QEMU manually." >&2
    exit 1
fi

set +e
timeout "${TIMEOUT_SECS}s" qemu-system-x86_64 \
    -cdrom "$BUILD_DIR/kernel.iso" \
    -display none \
    -serial stdio \
    -no-reboot \
    -no-shutdown >"$LOG_FILE" 2>&1
status=$?
set -e

if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
    echo "boot-smoke: QEMU failed (status $status)" >&2
    exit "$status"
fi

if [ ! -s "$LOG_FILE" ]; then
    echo "boot-smoke: no serial log captured at $LOG_FILE" >&2
    exit 1
fi

grep -q "Kernel section protections applied" "$LOG_FILE" || {
    echo "boot-smoke: missing kernel section protections log" >&2
    exit 1
}
grep -q "NX self-test passed" "$LOG_FILE" || {
    echo "boot-smoke: missing NX self-test pass" >&2
    exit 1
}
grep -q "Text write-protect test passed" "$LOG_FILE" || {
    echo "boot-smoke: missing text write-protect pass" >&2
    exit 1
}

echo "boot-smoke: passed"
