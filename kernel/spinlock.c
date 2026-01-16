#include "kernel/spinlock.h"
#include <stdbool.h>

void spinlock_init(spinlock_t *lock)
{
    lock->lock = 0;
    lock->flags = 0;
}

void spinlock_acquire(spinlock_t *lock)
{
    while (__atomic_test_and_set(&lock->lock, __ATOMIC_ACQUIRE)) {
        arch_cpu_relax();
    }
}

void spinlock_release(spinlock_t *lock)
{
    __atomic_clear(&lock->lock, __ATOMIC_RELEASE);
}

void spinlock_acquire_irqsave(spinlock_t *lock)
{
    arch_flags_t flags = arch_irq_save();
    lock->flags = flags;
    
    while (__atomic_test_and_set(&lock->lock, __ATOMIC_ACQUIRE)) {
        arch_cpu_relax();
    }
}

void spinlock_release_irqrestore(spinlock_t *lock)
{
    arch_flags_t flags = lock->flags;
    __atomic_clear(&lock->lock, __ATOMIC_RELEASE);
    arch_irq_restore(flags);
}
