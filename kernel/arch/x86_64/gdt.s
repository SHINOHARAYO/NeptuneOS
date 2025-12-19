.section .data
.align 8
.globl gdt64_descriptor
.globl gdt64
.globl gdt64_end

gdt64:
    .quad 0x0000000000000000          # null
    .quad 0x00af9a000000ffff          # code
    .quad 0x00af92000000ffff          # data

gdt64_descriptor:
    .word gdt64_end - gdt64 - 1
    .quad gdt64
gdt64_end:
