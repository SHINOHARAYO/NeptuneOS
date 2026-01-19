# NeptuneOS

NeptuneOS is a hobby kernel supporting x86_64. It features a higher-half design, Multiboot2 support, and a basic POSIX-like syscall ABI.

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

## Prerequisites

- CMake 3.16+
- **x86_64**: `gcc`, `grub-mkrescue`, `qemu-system-x86_64`

## Getting Started

### Building

```sh
# x86_64
cmake -S . -B build && cmake --build build
```

### Running in QEMU

```sh
# x86_64 (VGA + Serial)
cmake --build build --target run
```

## Project Layout

- `kernel/`
    - `arch/`: Architecture-specific implementations (HAL).
        - `x86_64`: GDT, IDT, Paging, Syscalls.
    - `include/`: Kernel headers.
    - `mem.c`, `heap.c`: Memory management.
    - `sched.c`: Scheduler core.
- `user/`: User-space libraries and shell.
- `scripts/`: Build and regression test scripts.
