cmake_minimum_required(VERSION 3.16)
project(NeptuneOS C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_language(ASM)

find_program(GRUB_MKRESCUE
    NAMES grub2-mkrescue grub-mkrescue
    REQUIRED
)


set(ARCH "x86_64" CACHE STRING "Target architecture")

set(KERNEL_SOURCES_COMMON
    kernel/sched.c
    kernel/irq.c
    kernel/panic.c
    kernel/log.c
    kernel/spinlock.c
    kernel/timer.c
    kernel/tty.c
    kernel/fs.c
    kernel/ramfs.c
    kernel/vfs.c
    kernel/block.c
    kernel/fat.c
    kernel/heap.c
    kernel/mem.c
    kernel/printf.c
    kernel/terminal.c
    kernel/user.c
    kernel/user_images.c
    kernel/syscall.c
    kernel/elf.c
    kernel/kernel.c
)

set(KERNEL_SOURCES_X86_64
    kernel/boot/boot.s
    kernel/arch/x86_64/gdt.s
    kernel/arch/x86_64/switch.s
    kernel/arch/x86_64/syscall_stub.s
    kernel/arch/x86_64/preempt_stub.s
    kernel/arch/x86_64/gdt.c
    kernel/arch/x86_64/mmu.c
    kernel/arch/x86_64/paging.c
    kernel/arch/x86_64/idt.c
    kernel/arch/x86_64/pic.c
    kernel/arch/x86_64/syscall_msr.c
    kernel/arch/x86_64/pit.c
    kernel/arch/x86_64/sched.c
    kernel/arch/x86_64/serial.c
    kernel/console.c
    kernel/pci.c
    kernel/acpi.c
    kernel/ata.c
)

set(KERNEL_SOURCES_AARCH64
    kernel/arch/aarch64/start.s
    kernel/arch/aarch64/vectors.s
    kernel/arch/aarch64/sched.c
    kernel/arch/aarch64/switch.s
    kernel/arch/aarch64/serial.c
    kernel/arch/aarch64/stubs.c
    kernel/arch/aarch64/vectors.c
    kernel/arch/aarch64/gic.c
    kernel/arch/aarch64/timer.c
    kernel/arch/aarch64/trampolines.s
)

if(ARCH STREQUAL "x86_64")
    set(KERNEL_SOURCES ${KERNEL_SOURCES_COMMON} ${KERNEL_SOURCES_X86_64})
    set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/linker.ld")
    set(ARCH_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/include")
elseif(ARCH STREQUAL "aarch64")
    set(KERNEL_SOURCES ${KERNEL_SOURCES_COMMON} ${KERNEL_SOURCES_AARCH64})
    set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/kernel/arch/aarch64/linker.ld")
    set(ARCH_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/kernel/arch/aarch64/include")
else()
    message(FATAL_ERROR "Unsupported architecture: ${ARCH}")
endif()

add_executable(kernel-${ARCH} ${KERNEL_SOURCES})
set_target_properties(kernel-${ARCH} PROPERTIES OUTPUT_NAME kernel.elf)
target_include_directories(kernel-${ARCH} PRIVATE 
    ${CMAKE_SOURCE_DIR}/kernel/include
    ${ARCH_INCLUDE_DIR}
)

set_source_files_properties(
    kernel/arch/x86_64/idt.c
    PROPERTIES COMPILE_OPTIONS "-mno-sse;-mno-sse2;-mno-mmx;-mgeneral-regs-only"
)
set_source_files_properties(
    kernel/pci.c
    kernel/acpi.c
    PROPERTIES COMPILE_OPTIONS "-mno-sse;-mno-sse2;-mno-mmx;-mno-80387;-mgeneral-regs-only"
)


if(ARCH STREQUAL "x86_64")
    target_compile_options(kernel-${ARCH} PRIVATE
        -ffreestanding
        -fno-stack-protector
        -fno-pic
        -m64
        -mno-red-zone
        -mcmodel=kernel
        -Wall -Wextra -Wpedantic
    )
elseif(ARCH STREQUAL "aarch64")
    target_compile_options(kernel-${ARCH} PRIVATE
        -ffreestanding
        -nostdlib
        -fno-stack-protector
        -fno-pic
        -mgeneral-regs-only
        -mstrict-align
        -mno-outline-atomics
        -Wall -Wextra -Werror
    )
endif()

target_link_options(kernel-${ARCH} PRIVATE
    -nostdlib
    -Wl,-T,${LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_BINARY_DIR}/kernel.map
    -no-pie
)
target_link_libraries(kernel-${ARCH} PRIVATE gcc)

set(ISODIR ${CMAKE_BINARY_DIR}/isodir)
set(ISO_IMAGE ${CMAKE_BINARY_DIR}/kernel.iso)
set(QEMU_SERIAL_LOG ${CMAKE_BINARY_DIR}/qemu-serial.log)

add_custom_command(OUTPUT ${ISO_IMAGE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ISODIR}/boot/grub
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kernel-${ARCH}> ${ISODIR}/boot/kernel.elf
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/grub/grub.cfg ${ISODIR}/boot/grub/grub.cfg
    COMMAND ${GRUB_MKRESCUE} -o ${ISO_IMAGE} ${ISODIR}
    DEPENDS kernel-${ARCH} ${CMAKE_SOURCE_DIR}/grub/grub.cfg
    COMMENT "Building bootable ISO with GRUB"
)

add_custom_target(iso DEPENDS ${ISO_IMAGE})

add_custom_target(run
    COMMAND qemu-system-x86_64 -cdrom ${ISO_IMAGE} -serial stdio -m 4G -no-reboot -no-shutdown
    DEPENDS iso
    USES_TERMINAL
)

add_custom_target(run-headless
    COMMAND ${CMAKE_COMMAND} -E rm -f ${QEMU_SERIAL_LOG}
    COMMAND ${CMAKE_COMMAND} -E echo "Serial log: ${QEMU_SERIAL_LOG}"
    COMMAND qemu-system-x86_64 -cdrom ${ISO_IMAGE} -display none -serial file:${QEMU_SERIAL_LOG} -no-reboot -no-shutdown
    DEPENDS iso
    USES_TERMINAL
)

add_custom_target(run-arm64
    COMMAND qemu-system-aarch64 -M virt -cpu cortex-a57 -kernel $<TARGET_FILE:kernel-${ARCH}> -serial stdio -m 1G -no-reboot -no-shutdown
    DEPENDS kernel-${ARCH}
    USES_TERMINAL
)

add_custom_target(run-arm64-headless
    COMMAND qemu-system-aarch64 -M virt -cpu cortex-a57 -kernel $<TARGET_FILE:kernel-${ARCH}> -nographic -m 1G -no-reboot -no-shutdown
    DEPENDS kernel-${ARCH}
    USES_TERMINAL
)

add_custom_target(regression
    COMMAND ${CMAKE_COMMAND} -E env BUILD_DIR=${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/scripts/regression.sh
    DEPENDS iso
    USES_TERMINAL
)

add_custom_target(fuzz
    COMMAND ${CMAKE_COMMAND} -E env BUILD_DIR=${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/scripts/fuzz.sh
    DEPENDS iso
    USES_TERMINAL
)

# Deep clean: Remove the entire build directories
add_custom_target(distclean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_SOURCE_DIR}/build ${CMAKE_SOURCE_DIR}/build-arm
    COMMENT "Removing 'build' and 'build-arm' directories"
)
