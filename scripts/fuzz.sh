#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build}"
ISO="${BUILD_DIR}/kernel.iso"
LOG="${BUILD_DIR}/qemu-fuzz.log"
QEMU="${QEMU:-qemu-system-x86_64}"
TIMEOUT_SECS="${TIMEOUT_SECS:-20}"

if [[ ! -f "${ISO}" ]]; then
  echo "fuzz: missing ISO at ${ISO}" >&2
  exit 1
fi

rm -f "${LOG}"
echo "fuzz: log ${LOG}"

{
  sleep 5
  printf "fuzz\n"
  sleep 3
} | timeout "${TIMEOUT_SECS}" "${QEMU}" \
  -cdrom "${ISO}" \
  -display none \
  -serial stdio \
  -no-reboot \
  -no-shutdown \
  | tee "${LOG}" || true

if ! rg -q "fuzz: ok" "${LOG}"; then
  echo "fuzz: missing completion" >&2
  exit 1
fi

if rg -q "KERNEL PANIC" "${LOG}"; then
  echo "fuzz: kernel panic" >&2
  exit 1
fi

echo "fuzz: ok"
