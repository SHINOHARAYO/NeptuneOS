# NeptuneOS (toy x86_64 kernel)

Tiny higher-half x86_64 hobby kernel with Multiboot2 boot, GRUB ISO image, VGA console/serial logging, IDT setup, and a bitmap-based physical memory manager.

## Features
- Multiboot2 header and higher-half paging (identity dropped after bootstrapping).
- GDT/IDT setup with basic exception handlers and panic flow.
- VGA text console with scrolling and COM1 serial logging.
- Physical memory manager using per-region bitmaps (handles multiple E820 ranges).
- Simple allocator self-test at boot with verbose logs.

## Prerequisites
- CMake 3.16+
- x86_64 cross-capable GCC/Clang (host GCC works for freestanding build)
- `grub-mkrescue` (or `grub2-mkrescue`)
- `qemu-system-x86_64` for running the ISO

## Build
```sh
cmake -S . -B build
cmake --build build
```
This produces `build/kernel.elf` and the GRUB ISO `build/kernel.iso`.

## Run in QEMU
```sh
cmake --build build --target iso
qemu-system-x86_64 -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown
```
Console logs appear on VGA; duplicate logs are emitted to the serial port (shown in `-serial stdio`).

## Layout
- `kernel/boot/boot.s` – Multiboot2 header, early 32-bit entry, paging bootstrap to long mode.
- `kernel/arch/x86_64` – IDT, paging, and GDT helpers.
- `kernel/console.c`, `kernel/serial.c`, `kernel/log.c` – VGA/serial output and logging.
- `kernel/mem.c` – Physical memory manager (per-region bitmaps), PMM stats helpers.
- `kernel/kernel.c` – Higher-half entry, init sequence, allocator self-test.
- `linker.ld` – Higher-half linker script with low bootstrap sections.
- `grub/grub.cfg` – GRUB boot menu for the ISO.

## Notes
- The PMM logs each managed region (start/end, usable pages) at boot to both console and serial.
- After initialization, the identity map is dropped; only higher-half mappings remain. Avoid using low virtual addresses after that point.
- Panic paths halt the CPU and emit messages to both VGA and serial for debugging.
