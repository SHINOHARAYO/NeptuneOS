.equ HIGHER_HALF_BASE, 0xFFFFFFFF80000000

.extern _text_start
.extern _text_end
.extern _rodata_start
.extern _rodata_end
.extern _data_start
.extern _data_end
.extern _bss_start
.extern _bss_end

.section .multiboot_header,"a"
.align 8
multiboot2_header:
    .long 0xe85250d6                 # multiboot2 magic
    .long 0                          # architecture (protected mode i386)
    .long multiboot2_header_end - multiboot2_header
    .long -(0xe85250d6 + 0 + (multiboot2_header_end - multiboot2_header))
    # required end tag
    .word 0
    .word 0
    .long 8
multiboot2_header_end:

.section .text.boot,"ax"
.code32
.globl _start
.extern kernel_main
.extern gdt64_descriptor
.globl multiboot_magic32
.globl multiboot_info32
.globl pml4_table

.equ VGA_PHYS, 0xB8000

_start:
    cli
    mov %eax, multiboot_magic32
    mov %ebx, multiboot_info32

    mov $stack_top, %esp
    mov $stack_top, %ebp

    lgdt gdt64_descriptor

    call setup_paging

    mov $pml4_table, %eax
    mov %eax, %cr3

    mov %cr4, %eax
    or $0x20, %eax                    # enable PAE
    mov %eax, %cr4

    mov $0xC0000080, %ecx
    rdmsr
    or $0x00000100, %eax              # enable LME
    wrmsr

    mov %cr0, %eax
    or $0x80000001, %eax              # enable paging + protection
    mov %eax, %cr0

    ljmp $0x08, $long_mode_entry      # far jump flushes pipeline into long mode

.code64
long_mode_entry:
    /* Remap stack to higher half */
    movabs $HIGHER_HALF_BASE, %rbx
    lea stack_top(%rip), %rax
    add %rbx, %rax
    mov %rax, %rsp
    mov %rax, %rbp

    /* Reload GDT with higher-half base */
    sub $10, %rsp
    movw $(gdt64_end - gdt64 - 1), (%rsp)
    lea gdt64(%rip), %rax
    add %rbx, %rax
    mov %rax, 2(%rsp)
    lgdt (%rsp)
    add $10, %rsp

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    /* Pass multiboot info, already low identity; kernel will consume high mapping */
    mov multiboot_magic32(%rip), %edi
    mov multiboot_info32(%rip), %esi

    /* Jump to higher-half kernel entry (symbol already high) */
    lea kernel_main(%rip), %rax
    jmp *%rax

.Lhang:
    hlt
    jmp .Lhang

.align 8
gdt64:
    .quad 0x0000000000000000          # null
    .quad 0x00af9a000000ffff          # code
    .quad 0x00af92000000ffff          # data
gdt64_descriptor:
    .word gdt64_end - gdt64 - 1
    .long gdt64
gdt64_end:

.section .bss.boot,"aw",@nobits
.align 16
stack:
    .skip 16384
stack_top:

.section .data.boot,"aw"
.align 8
multiboot_magic32:
    .long 0
multiboot_info32:
    .long 0

.align 4096
pml4_table:
    .fill 512, 8, 0
.align 4096
pdpt_identity:
    .fill 512, 8, 0
.align 4096
pdpt_higher:
    .fill 512, 8, 0
.align 4096
pd_identity:
    .fill 512, 8, 0
.align 4096
pd_higher:
    .fill 512, 8, 0

.section .text.boot,"ax"
.code32
setup_paging:
    xor %eax, %eax

    # zero paging structures
    lea pml4_table, %edi
    mov $1024, %ecx
    rep stosl

    lea pdpt_identity, %edi
    mov $1024, %ecx
    rep stosl

    lea pdpt_higher, %edi
    mov $1024, %ecx
    rep stosl

    lea pd_identity, %edi
    mov $1024, %ecx
    rep stosl

    lea pd_higher, %edi
    mov $1024, %ecx
    rep stosl

    # pml4[0] -> pdpt_identity | present | writable (identity map)
    mov $pdpt_identity, %eax
    or $0x3, %eax
    mov %eax, pml4_table
    movl $0, pml4_table + 4

    # pml4[511] -> pdpt_higher | present | writable (higher-half map)
    mov $pdpt_higher, %eax
    or $0x3, %eax
    mov %eax, pml4_table + (511 * 8)
    movl $0, pml4_table + (511 * 8) + 4

    # pdpt_identity[0] -> pd_identity | present | writable
    mov $pd_identity, %eax
    or $0x3, %eax
    mov %eax, pdpt_identity
    movl $0, pdpt_identity + 4

    # pdpt_higher[510] -> pd_higher | present | writable (higher-half kernel + devices)
    mov $pd_higher, %eax
    or $0x3, %eax
    mov %eax, pdpt_higher + (510 * 8)
    movl $0, pdpt_higher + (510 * 8) + 4

    # identity map first GiB with 2MiB pages
    lea pd_identity, %edi
    xor %ecx, %ecx
1:
    mov %ecx, %eax
    shl $21, %eax
    or $0x083, %eax                    # present | writable | PS
    mov %eax, (%edi)
    movl $0, 4(%edi)
    add $8, %edi
    inc %ecx
    cmp $512, %ecx
    jne 1b

    # higher-half kernel and devices: map phys 0..1GiB into pml4[511]/pdpt_higher[510]
    lea pd_higher, %edi
    xor %ecx, %ecx
2:
    mov %ecx, %eax
    shl $21, %eax
    or $0x083, %eax                    # present | writable | PS
    mov %eax, (%edi)
    movl $0, 4(%edi)
    add $8, %edi
    inc %ecx
    cmp $512, %ecx
    jne 2b

    ret
