#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
LOG_FILE="${LOG_FILE:-$BUILD_DIR/qemu-serial.log}"
TIMEOUT_SECS="${TIMEOUT_SECS:-20}"
KILL_GRACE_SECS="${KILL_GRACE_SECS:-5}"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --target iso

mkdir -p "$(dirname "$LOG_FILE")"
rm -f "$LOG_FILE"
: > "$LOG_FILE"

set +e
qemu-system-x86_64 \
    -cdrom "$BUILD_DIR/kernel.iso" \
    -display none \
    -serial "file:${LOG_FILE}" \
    -no-reboot \
    -no-shutdown &
qemu_pid=$!
set -e

remaining="$TIMEOUT_SECS"
while [ "$remaining" -gt 0 ]; do
    if [ -s "$LOG_FILE" ] && grep -q "Text write-protect test passed" "$LOG_FILE"; then
        break
    fi
    sleep 1
    remaining=$((remaining - 1))
done
killed=0
if kill -0 "$qemu_pid" 2>/dev/null; then
    killed=1
    kill -TERM "$qemu_pid" 2>/dev/null || true
    grace="$KILL_GRACE_SECS"
    while [ "$grace" -gt 0 ]; do
        if ! kill -0 "$qemu_pid" 2>/dev/null; then
            break
        fi
        sleep 1
        grace=$((grace - 1))
    done
    if kill -0 "$qemu_pid" 2>/dev/null; then
        kill -KILL "$qemu_pid" 2>/dev/null || true
    fi
fi

set +e
wait "$qemu_pid"
status=$?
set -e
if [ "$killed" -eq 0 ] && [ "$status" -ne 0 ]; then
    echo "boot-smoke: QEMU failed to run" >&2
    exit 1
fi

sleep 1
sync || true

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
