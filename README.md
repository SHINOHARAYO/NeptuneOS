# NeptuneOS

NeptuneOS is a hobby kernel supporting x86_64 and AArch64. It features a higher-half design, Multiboot2 support, and a basic POSIX-like syscall ABI.

## Features

### Core Features
- **Higher-half kernel design**
- **Physical Memory Manager**: Bitmap-based allocation.
- **Kernel Heap**: Slab allocator implementation.
- **Scheduler**: Round-robin with wait queues and sleep support.
- **User Space**: `syscall`/`sysret` interface, ELF64 loader, VFS, and a basic shell.

### Architecture Support

#### x86_64
- **Status**: Stable
- **Features**:
    - Ring 3 User Mode.
    - Fast Syscalls (`syscall`).
    - VGA/Serial output.
    - PMM, Heap, Scheduler fully integrated.

#### AArch64 (ARM64)
- **Status**: Unstable!!!!!!
- **Features**:
    - **Higher Half**: Kernel runs in high virtual address space (`0xFFFFFFFF80...`).
    - **User Mode**: Ring 3 (EL0) support with `svc` interface.
    - **Interrupts**: GICv2 driver (Distributor/CPU Interface).
    - **Timer**: ARM Generic Timer (EL1 Physical Timer).
    - **Console**: PL011 UART interactive shell.
    - **Power**: PSCI Shutdown and Reboot support.

## Prerequisites

- CMake 3.16+
- **x86_64**: `gcc`, `grub-mkrescue`, `qemu-system-x86_64`
- **AArch64**: `gcc-aarch64-linux-gnu`, `qemu-system-aarch64`

## Getting Started

### Building

```sh
# x86_64
cmake -S . -B build && cmake --build build

# AArch64
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake -DARCH=aarch64 -S . -B build-arm && cmake --build build-arm
```

### Running in QEMU

```sh
# x86_64 (VGA + Serial)
cmake --build build --target run

# AArch64 (Serial)
cmake --build build-arm --target run-arm64
```

## Project Layout

- `kernel/`
    - `arch/`: Architecture-specific implementations (HAL).
        - `x86_64`: GDT, IDT, Paging, Syscalls.
        - `aarch64`: Boot stubs, GIC, Timer, Vectors.
    - `include/`: Kernel headers.
    - `mem.c`, `heap.c`: Memory management.
    - `sched.c`: Scheduler core.
- `user/`: User-space libraries and shell.
- `scripts/`: Build and regression test scripts.
