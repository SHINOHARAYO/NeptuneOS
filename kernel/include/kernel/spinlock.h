#ifndef NEPTUNE_SPINLOCK_H
#define NEPTUNE_SPINLOCK_H

#include <stdint.h>
#include <arch/processor.h>

typedef struct {
    volatile uint32_t lock;
    arch_flags_t flags; // for interrupt saving
} spinlock_t;

void spinlock_init(spinlock_t *lock);
void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);

// Acquires lock and disables interrupts, saving RFLAGS
void spinlock_acquire_irqsave(spinlock_t *lock);
// Releases lock and restores RFLAGS
void spinlock_release_irqrestore(spinlock_t *lock);

#endif
