#include <stdint.h>

/* Paging structures are defined in boot.s; this unit keeps externs
 * for future helpers without introducing duplicate symbols. */
extern uint64_t pml4_table[];
extern uint64_t pdpt_identity[];
extern uint64_t pdpt_higher[];
extern uint64_t pd_identity[];
extern uint64_t pd_higher[];
extern uint64_t pd_higher_extra[];
