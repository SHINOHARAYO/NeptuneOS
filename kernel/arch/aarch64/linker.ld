ENTRY(_start)

SECTIONS
{
    . = 0xFFFFFFFF80080000;
    _kernel_start = .;
    
    .text : AT(0x40080000) {
        _text_start = .;
        KEEP(*(.text.boot))
        *(.text*)
        _text_end = .;
    }
    
    .rodata : {
        _rodata_start = .;
        *(.rodata*)
        _rodata_end = .;
    }
    
    .data : {
        _data_start = .;
        *(.data*)
        _data_end = .;
    }
    
    .bss : { 
        . = ALIGN(16);
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        _bss_end = .;
    }

    .pgtables (NOLOAD) : {
        *(.pgtables)
    }
    
    _kernel_end = .;
    _kernel_phys_start = 0x40080000;
    _kernel_phys_end = _kernel_phys_start + (_kernel_end - _kernel_start);
}
