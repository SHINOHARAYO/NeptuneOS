.section .text.boot
.global _start

_start:
    /* Save FDT pointer from x0 to x21 (callee-saved) */
    mov x21, x0

    /* Read CPU ID */
    mrs x0, mpidr_el1
    and x0, x0, #3
    cbz x0, master_cpu


    
hang:
    wfe
    b hang

master_cpu:
    /* Setup stack */
    ldr x0, =stack_top
    mov sp, x0

    /* Setup MAIR_EL1: Attr0=Normal (0xFF), Attr1=Device (0x00) */
    ldr x0, =0xFF00
    msr mair_el1, x0

    /* Setup TCR_EL1 */
    /* 
       T0SZ=16 (48-bit), T1SZ=16 (48-bit)
       TG0=00 (4KB), TG1=10 (4KB)
       IPS=000 (32-bit phys) or 010 (40-bit)? Keep simple.
       SH0=11 (Inner Shareable), SH1=11
       ORGN0=01 (WB, RA, WA), IRGN0=01
       ORGN1=01, IRGN1=01
    */
    ldr x0, =0x15B5103510 /* T1SZ=16, T0SZ=16, 4KB, valid config */
    /* Let's construct simpler value: */
    /* 
       T1SZ [21:16] = 16 (0x10)
       TG1 [31:30] = 2 (4KB)
       SH1 [29:28] = 3 (Inner Shareable)
       ORGN1 [27:26] = 1 (WB)
       IRGN1 [25:24] = 1 (WB)
       T0SZ [5:0] = 16 (0x10)
       TG0 [15:14] = 0 (4KB)
       SH0 [13:12] = 3
       ORGN0 [11:10] = 1
       IRGN0 [9:8] = 1
    */
    ldr x0, =((2 << 30) | (3 << 28) | (1 << 26) | (1 << 24) | (16 << 16) | \
              (3 << 12) | (1 << 10) | (1 << 8) | 16)
    msr tcr_el1, x0

    /* Setup Page Tables */
    /* We need:
       Identity map 0x40000000-0x80000000 (at least) -> TTBR0
       Higher Half 0xFFFFFFFF80000000 map to Phys 0x0... -> TTBR1
    */
    
    /* Clear tables */
    adrp x0, page_tables_start
    add x0, x0, :lo12:page_tables_start
    adrp x1, page_tables_end
    add x1, x1, :lo12:page_tables_end
    sub x2, x1, x0
    mov x1, #0
1:  str xzr, [x0], #8
    sub x2, x2, #8
    cbnz x2, 1b

    adrp x0, boot_pml4
    add x0, x0, :lo12:boot_pml4
    
    /* Identity Map L0[0] -> boot_low_pdpt (For current PC execution) */
    adrp x1, boot_low_pdpt
    add x1, x1, :lo12:boot_low_pdpt
    mov x2, #3 /* Table, Valid */
    orr x2, x1, x2
    str x2, [x0] /* Index 0 */

    /* Index 256 (HHDM Start 0xFFFF8000...) -> boot_high_pdpt */
    adrp x1, boot_high_pdpt
    add x1, x1, :lo12:boot_high_pdpt
    mov x2, #3
    orr x2, x1, x2
    str x2, [x0, #256 * 8]

    /* Index 511 (Kernel High 0xFFFF....) -> boot_high_pdpt */
    str x2, [x0, #511 * 8]

    /* Setup Low PDPT (Identity) */
    adrp x0, boot_low_pdpt
    add x0, x0, :lo12:boot_low_pdpt
    /* Index 0: 0-1GB (UART) -> Device (Attr0) */
    mov x1, #0
    ldr x2, =((1 << 10) | (1 << 2) | 1) /* Attr0=Device? Wait, MAIR Attr0=00(Device). Attr1=FF(Normal).
                                            PTE 4:2 = AttrIndex.
                                            000 = Attr0.
                                            001 = Attr1.
                                            So we want 0 for Device, 4 (1<<2) for Normal. */
    /* Check previous code: */
    /* ldr x2, =((1 << 10) | (3 << 8) | 1) */ 
    /* Bits 4:2 are 0. So this IS Attr0 (Device). Correct for UART. */
    
    /* We want SH=0 (Outer Shareable) for Device? Or SH=3 (Inner)?
       Device-nGnRnE implies SH is ignored (Always Outer Shareable equivalent).
       So (3<<8) or (0<<8) is fine. 
       Let's stick to simple:
       Device: AF=1, Valid=1, AttrIdx=0.
       (1<<10) | 1.
    */
    ldr x2, =((1 << 10) | 1)
    orr x2, x2, x1
    str x2, [x0, #0]
    
    /* Index 1: 1GB-2GB (Code) -> Normal (Attr1) */
    ldr x1, =0x40000000
    /* Normal: AF=1, SH=3 (Inner), AttrIdx=1, Valid=1 */
    /* (1<<10) | (3<<8) | (1<<2) | 1 */
    ldr x2, =((1 << 10) | (3 << 8) | (1 << 2) | 1)
    orr x2, x2, x1
    str x2, [x0, #8]


    /* Setup High PDPT (HHDM and Kernel) -> Normal for all RAM */
    adrp x0, boot_high_pdpt
    add x0, x0, :lo12:boot_high_pdpt
    
    /* --- HHDM Mappings --- */
    /* Index 0: Phys 0 (UART?) -> Device */
    mov x1, #0
    ldr x2, =((1 << 10) | 1) /* Device */
    orr x2, x2, x1
    str x2, [x0, #0]
    
    /* Index 1: Phys 1GB (RAM) -> Normal */
    ldr x1, =0x40000000
    ldr x2, =((1 << 10) | (3 << 8) | (1 << 2) | 1)
    orr x2, x2, x1
    str x2, [x0, #8]
    
    /* Index 2: Phys 2GB (RAM) -> Normal */
    ldr x1, =0x80000000
    ldr x2, =((1 << 10) | (3 << 8) | (1 << 2) | 1)
    orr x2, x2, x1
    str x2, [x0, #16]

    /* --- Kernel Higher Half Mappings --- */
    /* Index 510: Map to Phys 1GB (Code) -> Normal */
    ldr x1, =0x40000000
    ldr x2, =((1 << 10) | (3 << 8) | (1 << 2) | 1)
    orr x2, x2, x1
    str x2, [x0, #4080]

    /* Index 511: Map to Phys 2GB -> Normal */
    ldr x1, =0x80000000
    ldr x2, =((1 << 10) | (3 << 8) | (1 << 2) | 1)
    orr x2, x2, x1
    str x2, [x0, #4088]


    /* Set TTBR0 and TTBR1 */
    adrp x0, boot_pml4
    add x0, x0, :lo12:boot_pml4
    msr ttbr0_el1, x0
    msr ttbr1_el1, x0

    /* Invalidate TLB */
    dsb ish
    tlbi vmalle1is
    dsb ish
    isb

    /* Debug: Print 'M' (Pre-MMU) - REMOVED */

    /* Enable MMU */
    mrs x0, sctlr_el1
    orr x0, x0, #1 /* M=1 */
    orr x0, x0, #(1 << 2) /* C=1 D-Cache */
    orr x0, x0, #(1 << 12) /* I=1 I-Cache */
    msr sctlr_el1, x0
    isb

    /* Setup High Stack (Essential for Exceptions) */
    ldr x0, =stack_top /* Loads VMA */
    mov sp, x0

    /* Set Vector Base Address Register to High Address */
    ldr x0, =vector_table /* This loads VMA */
    msr vbar_el1, x0

    /* Debug: Print 'P' (Post-MMU/Stack/VBAR) - REMOVED */

    /* Enable FP/SIMD (CPACR_EL1) */
    mrs x0, cpacr_el1
    orr x0, x0, #(3 << 20) /* FPEN = 11 (Full access) */
    msr cpacr_el1, x0
    isb

    /* Debug: Print 'F' (Post-FP) - REMOVED */

    /* Clear BSS (Moved to C) */


    /* Debug: Print 'K' to UART - REMOVED */

    /* Jump to C code in Higher Half */
    ldr x0, =0x36d76289
    mov x1, x21 /* Pass FDT pointer as multiboot_info */
    ldr x2, =kernel_main /* Loads VMA */
    br x2

.section .pgtables, "aw", @nobits
.align 12
page_tables_start:
.global boot_pml4
boot_pml4:
    .skip 4096
boot_low_pdpt:
    .skip 4096
boot_high_pdpt:
    .skip 4096
page_tables_end:

stack_bottom:
.skip 65536
stack_top:
