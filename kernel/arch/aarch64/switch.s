
.section .text
.global context_switch

/*
 * void context_switch(struct context *old_ctx, struct context *new_ctx);
 * x0 = old_ctx
 * x1 = new_ctx
 */
context_switch:
    /* Save callee-saved registers to old_ctx */
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80] /* FP, LR */
    
    mov x2, sp
    str x2, [x0, #96]       /* SP */

    /* Load callee-saved registers from new_ctx */
    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldp x29, x30, [x1, #80] /* FP, LR */
    
    ldr x2, [x1, #96]       /* SP */
    mov sp, x2

    ret

/*
 * void arch_enter_user(uint64_t entry, uint64_t stack, uint64_t pml4_phys)
 * x0 = entry
 * x1 = stack
 * x2 = pml4_phys
 */
.global arch_enter_user
arch_enter_user:
    /* Set User Page Table */
    msr ttbr0_el1, x2
    dsb ish
    tlbi vmalle1is
    dsb ish
    isb

    /* Set User Stack */
    msr sp_el0, x1

    /* Set Entry Point */
    msr elr_el1, x0

    /* Set SPSR (EL0t, Interrupts Enabled) */
    /* DAIF=0, M=0000 (EL0t) */
    mov x3, #0
    msr spsr_el1, x3

    /* Clear registers to avoid leaking info */
    mov x0, #0
    mov x1, #0
    mov x2, #0
    mov x3, #0
    mov x4, #0
    mov x5, #0
    mov x6, #0
    mov x7, #0
    mov x8, #0
    mov x9, #0
    mov x10, #0
    mov x11, #0
    mov x12, #0
    mov x13, #0
    mov x14, #0
    mov x15, #0
    mov x16, #0
    mov x17, #0
    mov x18, #0
    mov x19, #0
    mov x20, #0
    mov x21, #0
    mov x22, #0
    mov x23, #0
    mov x24, #0
    mov x25, #0
    mov x26, #0
    mov x27, #0
    mov x28, #0
    mov x29, #0
    mov x30, #0

    eret
