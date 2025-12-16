#include <stdint.h>

/* Placeholder for future GDT helpers; currently tables are defined in gdt.s. */
void gdt_dummy_reference(void)
{
    (void)sizeof(uint64_t);
}
