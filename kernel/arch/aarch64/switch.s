
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
