.section .text.boot
.global _start

_start:
    /* Read CPU ID */
    mrs x0, mpidr_el1
    and x0, x0, #3
    cbz x0, master_cpu
    
    /* Hang other CPUs */
hang:
    wfe
    b hang

master_cpu:
    /* Setup stack */
    ldr x0, =stack_top
    mov sp, x0

    /* Set Vector Base Address Register */
    ldr x0, =vector_table
    msr vbar_el1, x0
    
    /* Jump to C code (kernel_main) with Multiboot2 magic */
    ldr x0, =0x36d76289
    mov x1, #0
    bl kernel_main
    
    /* If return, hang */
    b hang

.section .bss
.align 16
stack_bottom:
.skip 16384 /* 16KB boot stack */
stack_top:
