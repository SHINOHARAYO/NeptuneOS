#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build}"
ISO="${BUILD_DIR}/kernel.iso"
LOG="${BUILD_DIR}/qemu-regression.log"
QEMU="${QEMU:-qemu-system-x86_64}"
TIMEOUT_SECS="${TIMEOUT_SECS:-12}"

if [[ ! -f "${ISO}" ]]; then
  echo "regression: missing ISO at ${ISO}" >&2
  exit 1
fi

rm -f "${LOG}"
echo "regression: log ${LOG}"

timeout "${TIMEOUT_SECS}" "${QEMU}" \
  -cdrom "${ISO}" \
  -display none \
  -serial file:"${LOG}" \
  -no-reboot \
  -no-shutdown || true

if [[ ! -f "${LOG}" ]]; then
  echo "regression: log not captured" >&2
  exit 1
fi

if rg -q "KERNEL PANIC" "${LOG}"; then
  echo "regression: kernel panic" >&2
  exit 1
fi

if rg -q "Exception:" "${LOG}"; then
  echo "regression: exception reported" >&2
  exit 1
fi

if ! rg -q "Neptune user shell" "${LOG}"; then
  echo "regression: user shell not reached" >&2
  exit 1
fi

if ! rg -q "FAT16 volume mounted" "${LOG}"; then
  echo "regression: filesystem not mounted" >&2
  exit 1
fi

echo "regression: ok"
