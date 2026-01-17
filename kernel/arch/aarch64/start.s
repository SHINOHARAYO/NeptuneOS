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
    ldr x0, =page_tables_start
    ldr x1, =page_tables_end
    sub x2, x1, x0
    mov x1, #0
1:  str xzr, [x0], #8
    sub x2, x2, #8
    cbnz x2, 1b

    ldr x0, =boot_pml4
    
    /* Identity Map (0x40000000 is 1GB start). 
       We map first 512GB via L0[0].
       L0[0] -> boot_low_pdpt
    */
    ldr x1, =boot_low_pdpt
    mov x2, #3 /* Table, Valid */
    orr x2, x1, x2
    str x2, [x0]

    /* Higher Half Map. 
       Virt 0xFFFFFFFF80000000. 
       L0 index = 511.
       L0[511] -> boot_high_pdpt
    */
    ldr x1, =boot_high_pdpt
    mov x2, #3
    orr x2, x1, x2
    str x2, [x0, #511 * 8]

    /* Index 256 (L0): HHDM -> boot_high_pdpt.
       HHDM Base 0xFFFF8000... is Index 256 in L0.
    */
    str x2, [x0, #256 * 8]

    /* Setup Low PDPT (Identity).
       Slot 0 (0-1GB) covers 0x00000000 - 0x3FFFFFFF (includes 0x09000000).
       Entry 0: Output 0x00000000.
       Attributes: AttrIndx=1 (Device), AF=1, Valid=1, Block(0).
    */
    ldr x0, =boot_low_pdpt
    mov x1, #0
    ldr x2, =((1 << 10) | (1 << 2) | 1)
    orr x2, x2, x1
    str x2, [x0, #0] /* Index 0 */

    /* Index 1: 0x40000000 (1GB). Normal Memory. 
       AttrIndx=0.
    */
    ldr x1, =0x40000000
    ldr x2, =((1 << 10) | (3 << 8) | 1) /* AF | SH | Valid(1) | Block(0) */
    orr x2, x2, x1
    str x2, [x0, #8] /* Index 1 */

    /* Setup High PDPT (HHDM).
       We populate:
       Index 0, 1, 2 -> Phys 0, 1GB, 2GB (For HHDM at L0[256]).
       Index 510, 511 -> Phys 0, 1GB (For Kernel at L0[511]).
    */
    ldr x0, =boot_high_pdpt
    
    /* Index 0: Phys 0 */
    mov x1, #0
    ldr x2, =((1 << 10) | (3 << 8) | 1)
    orr x2, x2, x1
    str x2, [x0, #0]

    /* Index 1: Phys 1GB */
    ldr x1, =0x40000000
    ldr x2, =((1 << 10) | (3 << 8) | 1)
    orr x2, x2, x1
    str x2, [x0, #8]

    /* Index 2: Phys 2GB */
    ldr x1, =0x80000000
    ldr x2, =((1 << 10) | (3 << 8) | 1)
    orr x2, x2, x1
    str x2, [x0, #16]

    /* Index 510: Phys 0 */
    mov x1, #0
    ldr x2, =((1 << 10) | (3 << 8) | 1)
    orr x2, x2, x1
    str x2, [x0, #4080] /* Index 510 * 8 = 4080 */

    /* Index 511: Phys 1GB */
    ldr x1, =0x40000000
    ldr x2, =((1 << 10) | (3 << 8) | 1)
    orr x2, x2, x1
    str x2, [x0, #4088] /* Index 511 * 8 = 4088 */

    /* Set TTBR0 and TTBR1 */
    ldr x0, =boot_pml4
    msr ttbr0_el1, x0
    msr ttbr1_el1, x0

    /* Invalidate TLB and Ensure writes are visible */
    dsb ish
    tlbi vmalle1is
    dsb ish
    isb

    /* Enable MMU */
    mrs x0, sctlr_el1
    orr x0, x0, #1 /* M=1 */
    orr x0, x0, #(1 << 2) /* C=1 D-Cache */
    orr x0, x0, #(1 << 12) /* I=1 I-Cache */
    msr sctlr_el1, x0
    isb

    /* Jump to C code */
    /* Set Vector Base Address Register again just in case */
    ldr x0, =vector_table
    msr vbar_el1, x0
    
    ldr x0, =0x36d76289
    mov x1, x21 /* Pass FDT pointer as multiboot_info */
    ldr x2, =kernel_main
    br x2

.section .bss
.align 12
page_tables_start:
boot_pml4:
    .skip 4096
boot_low_pdpt:
    .skip 4096
boot_high_pdpt:
    .skip 4096
page_tables_end:

stack_bottom:
.skip 16384
stack_top:
