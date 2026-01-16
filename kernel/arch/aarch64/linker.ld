ENTRY(_start)

SECTIONS
{
    . = 0x40080000;
    _kernel_start = .;
    
    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    }
    
    .rodata : {
        *(.rodata*)
    }
    
    .data : {
        *(.data*)
    }
    
    .bss : { 
        . = ALIGN(16);
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    }
    
    _kernel_end = .;
    _kernel_phys_start = 0x40080000;
    _kernel_phys_end = 0x40080000 + SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) + SIZEOF(.bss);
}
