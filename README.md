# NeptuneOS (toy x86_64 kernel)

Tiny higher-half x86_64 hobby kernel with Multiboot2 boot, GRUB ISO image, VGA console/serial logging, IDT setup, and a bitmap-based physical memory manager.

## Features
- Multiboot2 header and higher-half paging (identity dropped after bootstrapping).
- GDT rebuilt on the heap (with TSS) and IDT rebuilt on the heap; basic exception handlers and panic flow.
- VGA text console with scrolling and COM1 serial logging.
- Physical memory manager using per-region bitmaps (handles multiple E820 ranges).
- Kernel heap with slab/large allocs, free coalescing, stats dump, and self-tests.
- PIC remap + PIT timer ticks with IRQ handlers; keyboard and COM1 IRQ stubs unmasked.
- Simple allocator self-test and timer tick validation at boot with verbose logs.
- In-kernel terminal REPL with line input and basic built-in commands.
- User-mode scaffolding: layout constants, per-process page tables, and ring3 entry helper.
- int 0x80 syscall ABI with exit/yield/read/write/open/close/spawn/exec, error codes, and handle table.
- Minimal ELF64 loader for PT_LOAD segments into user address spaces.
- Tiny in-memory FS for user images loaded by path with argv/env stack setup.
- TTY-backed stdin/stdout with console/serial output.
- User-mode shell REPL with syscall-backed IO and command dispatch.
- Basic round-robin scheduler with per-thread stacks, context switch, and tick-based preemption.

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

User programs can be rebuilt into `kernel/user_images.c` with:
```sh
scripts/build-user.sh
```

## Run in QEMU
```sh
cmake --build build --target iso
qemu-system-x86_64 -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown
```
Headless run (serial log saved to `build/qemu-serial.log`):
```sh
cmake --build build --target run-headless
```
Regression smoke (headless boot checks):
```sh
cmake --build build --target regression
```
Fuzz smoke (headless user-mode syscall fuzz):
```sh
cmake --build build --target fuzz
```
Console logs appear on VGA; duplicate logs are emitted to the serial port (shown in `-serial stdio`).

## Layout
- `kernel/boot/boot.s` – Multiboot2 header, early 32-bit entry, paging bootstrap to long mode.
- `kernel/arch/x86_64` – GDT/IDT, paging, PIC/PIT setup, IRQ stubs.
- `kernel/console.c`, `kernel/serial.c`, `kernel/log.c` – VGA/serial output and logging.
- `kernel/mem.c` – Physical memory manager (per-region bitmaps), PMM stats helpers.
- `kernel/heap.c` – Kernel heap with slab/large buckets, coalescing, stats, and verification.
- `kernel/timer.c` – Tick counter and callback hooks driven by PIT IRQ.
- `kernel/kernel.c` – Higher-half entry, init sequence, allocator/timer self-tests.
- `linker.ld` – Higher-half linker script with low bootstrap sections.
- `grub/grub.cfg` – GRUB boot menu for the ISO.

## TODO / Next Steps
No.

## Notes
- The PMM logs each managed region (start/end, usable pages) at boot to both console and serial.
- After initialization, the identity map is dropped; only higher-half mappings remain. Avoid using low virtual addresses after that point.
- Panic paths emit messages to both VGA and serial, then attempt a reboot.
