ENTRY(_start)

KERNEL_PHYS_BASE = 0x00100000;
HIGHER_HALF_BASE = 0xFFFFFFFF80000000;

PHDRS
{
    text_boot PT_LOAD FILEHDR PHDRS FLAGS(0x5); /* RX */
    data_boot PT_LOAD           FLAGS(0x6);     /* RW */
    text_hi   PT_LOAD           FLAGS(0x5);     /* RX */
    rodata_hi PT_LOAD           FLAGS(0x4);     /* R  */
    data_hi   PT_LOAD           FLAGS(0x6);     /* RW */
}

SECTIONS
{
    /* Low bootstrap region */
    . = KERNEL_PHYS_BASE;
    _kernel_start = .;

    .text.boot : AT(ADDR(.text.boot))
    {
        KEEP(*(.multiboot_header))
        *(.text.boot*)
    } :text_boot

    .data.boot : AT(ADDR(.data.boot)) ALIGN(0x1000)
    {
        *(.data.boot*)
    } :data_boot

    .bss.boot (NOLOAD) : ALIGN(0x1000)
    {
        *(.bss.boot*)
    } :data_boot

    /* Track physical load address */
    phys = .;

    /* Higher-half kernel, loaded at phys */
    . = HIGHER_HALF_BASE + phys;

    .text : AT(phys) ALIGN(0x1000)
    {
        _text_start = .;
        *(.text*)
        _text_end = .;
    } :text_hi
    phys = phys + SIZEOF(.text);
    phys = ALIGN(phys, 0x1000);

    .rodata : AT(phys) ALIGN(0x1000)
    {
        _rodata_start = .;
        *(.rodata*)
        _rodata_end = .;
    } :rodata_hi
    phys = phys + SIZEOF(.rodata);
    phys = ALIGN(phys, 0x200000);

    .data : AT(phys) ALIGN(0x1000)
    {
        _data_start = .;
        *(.data*)
        _data_end = .;
    } :data_hi
    phys = phys + SIZEOF(.data);
    phys = ALIGN(phys, 0x1000);

    .bss (NOLOAD) : AT(phys) ALIGN(0x1000)
    {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    } :data_hi
    phys = phys + SIZEOF(.bss);

    _kernel_end = .;
    _kernel_phys_start = KERNEL_PHYS_BASE;
    _kernel_phys_end = phys;
}
