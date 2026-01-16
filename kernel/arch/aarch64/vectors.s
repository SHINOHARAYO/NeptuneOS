.section .text
.global vector_table
.align 11 /* 2KB alignment required for VBAR_EL1 */

vector_table:
    /* Current EL with SP0 */
    b . /* Synchronous */
    .align 7
    b . /* IRQ */
    .align 7
    b . /* FIQ */
    .align 7
    b . /* SError */
    .align 7

    /* Current EL with SPx */
    b sync_handler      /* Synchronous */
    .align 7
    b irq_handler       /* IRQ */
    .align 7
    b . /* FIQ */
    .align 7
    b . /* SError */
    .align 7

    /* Lower EL using AArch64 */
    b . /* Synchronous */
    .align 7
    b . /* IRQ */
    .align 7
    b . /* FIQ */
    .align 7
    b . /* SError */
    .align 7

    /* Lower EL using AArch32 */
    b .
    .align 7
    b .
    .align 7
    b .
    .align 7
    b .

.global sync_handler
sync_handler:
    /* Save volatile registers */
    sub sp, sp, #256
    stp x0, x1, [sp, #16 * 0]
    stp x2, x3, [sp, #16 * 1]
    stp x4, x5, [sp, #16 * 2]
    stp x6, x7, [sp, #16 * 3]
    stp x8, x9, [sp, #16 * 4]
    stp x10, x11, [sp, #16 * 5]
    stp x12, x13, [sp, #16 * 6]
    stp x14, x15, [sp, #16 * 7]
    stp x16, x17, [sp, #16 * 8]
    stp x18, x30, [sp, #16 * 9]

    bl arm_sync_handler

    /* Restore not strictly needed since we panic, but good for completeness */
    b .

.global irq_handler
irq_handler:
    /* Save volatile registers */
    sub sp, sp, #256
    stp x0, x1, [sp, #16 * 0]
    stp x2, x3, [sp, #16 * 1]
    stp x4, x5, [sp, #16 * 2]
    stp x6, x7, [sp, #16 * 3]
    stp x8, x9, [sp, #16 * 4]
    stp x10, x11, [sp, #16 * 5]
    stp x12, x13, [sp, #16 * 6]
    stp x14, x15, [sp, #16 * 7]
    stp x16, x17, [sp, #16 * 8]
    stp x18, x30, [sp, #16 * 9] /* x30 is LR */
    /* Add more if needed (x19-x29 are callee saved, but interrupt should imply caller-save rules? 
       Actually, an interrupt looks like a function call to the C handler. 
       The C handler will preserve x19-x29. 
       So we only need to save x0-x18 and LR. */

    bl arm_irq_handler

    /* Restore */
    ldp x0, x1, [sp, #16 * 0]
    ldp x2, x3, [sp, #16 * 1]
    ldp x4, x5, [sp, #16 * 2]
    ldp x6, x7, [sp, #16 * 3]
    ldp x8, x9, [sp, #16 * 4]
    ldp x10, x11, [sp, #16 * 5]
    ldp x12, x13, [sp, #16 * 6]
    ldp x14, x15, [sp, #16 * 7]
    ldp x16, x17, [sp, #16 * 8]
    ldp x18, x30, [sp, #16 * 9]
    add sp, sp, #256
    
    eret
