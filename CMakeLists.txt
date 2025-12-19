cmake_minimum_required(VERSION 3.16)
project(NeptuneOS C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_language(ASM)

find_program(GRUB_MKRESCUE
    NAMES grub2-mkrescue grub-mkrescue
    REQUIRED
)

set(KERNEL_SOURCES
    kernel/boot/boot.s
    kernel/arch/x86_64/gdt.s
    kernel/arch/x86_64/gdt.c
    kernel/arch/x86_64/mmu.c
    kernel/arch/x86_64/paging.c
    kernel/arch/x86_64/idt.c
    kernel/console.c
    kernel/panic.c
    kernel/serial.c
    kernel/log.c
    kernel/mem.c
    kernel/printf.c
    kernel/kernel.c
)

add_executable(kernel ${KERNEL_SOURCES})
set_target_properties(kernel PROPERTIES OUTPUT_NAME kernel.elf)
target_include_directories(kernel PRIVATE ${CMAKE_SOURCE_DIR}/kernel/include)

set_source_files_properties(
    kernel/arch/x86_64/idt.c
    PROPERTIES COMPILE_OPTIONS "-mno-sse;-mno-sse2;-mno-mmx;-mgeneral-regs-only"
)

target_compile_options(kernel PRIVATE
    -ffreestanding
    -fno-stack-protector
    -fno-pic
    -m64
    -mno-red-zone
    -mcmodel=kernel
    -Wall -Wextra -Wpedantic
)

target_link_options(kernel PRIVATE
    -nostdlib
    -Wl,-T,${CMAKE_SOURCE_DIR}/linker.ld
    -Wl,-Map=${CMAKE_BINARY_DIR}/kernel.map
    -no-pie
)

set(ISODIR ${CMAKE_BINARY_DIR}/isodir)
set(ISO_IMAGE ${CMAKE_BINARY_DIR}/kernel.iso)

add_custom_command(OUTPUT ${ISO_IMAGE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ISODIR}/boot/grub
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kernel> ${ISODIR}/boot/kernel.elf
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/grub/grub.cfg ${ISODIR}/boot/grub/grub.cfg
    COMMAND ${GRUB_MKRESCUE} -o ${ISO_IMAGE} ${ISODIR}
    DEPENDS kernel ${CMAKE_SOURCE_DIR}/grub/grub.cfg
    COMMENT "Building bootable ISO with GRUB"
)

add_custom_target(iso DEPENDS ${ISO_IMAGE})

add_custom_target(run
    COMMAND qemu-system-x86_64 -cdrom ${ISO_IMAGE} -serial stdio -no-reboot -no-shutdown
    DEPENDS iso
    USES_TERMINAL
)
