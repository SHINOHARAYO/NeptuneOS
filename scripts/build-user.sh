#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-user"

mkdir -p "${BUILD_DIR}"

COMMON_FLAGS=(
  -ffreestanding
  -nostdlib
  -fno-pic
  -fno-builtin
  -m64
  -mno-red-zone
  -mno-sse
  -mno-sse2
  -mno-mmx
  -mno-80387
  -msoft-float
  -fno-stack-protector
  -no-pie
  -Wl,-e,_start
  -Wl,--build-id=none
  -O2
)

programs=(hello shell init echo ls cat fuzz)

for prog in "${programs[@]}"; do
  gcc "${COMMON_FLAGS[@]}" \
    -o "${BUILD_DIR}/${prog}.elf" \
    "${ROOT}/user/${prog}.c" \
    "${ROOT}/user/libc.c"
  xxd -i -n "user_image_${prog}" "${BUILD_DIR}/${prog}.elf" > "${BUILD_DIR}/${prog}.inc"
done

ROOT_PATH="${ROOT}" python - <<'PY'
import re
from pathlib import Path
import os

root = Path(os.environ["ROOT_PATH"]).resolve()
build = root / "build-user"
inc_files = ["hello", "shell", "init", "echo", "ls", "cat", "fuzz"]

out = ["#include <stdint.h>\n\n"]

for name in inc_files:
    text = (build / f"{name}.inc").read_text()
    m = re.search(r"unsigned char (\w+)\[\] = \{\n(.*)\n\};\nunsigned int \w+_len = (\d+);", text, re.S)
    if not m:
        raise SystemExit(f"Failed to parse {name}.inc")
    sym, body, length = m.group(1), m.group(2), m.group(3)
    out.append(f"const uint8_t {sym}[] = {{\n{body}\n}};\n")
    out.append(f"const uint64_t {sym}_len = {length};\n\n")

(root / "kernel" / "user_images.c").write_text("".join(out))
PY
