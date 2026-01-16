#include "kernel/spinlock.h"
#include <stdbool.h>

void spinlock_init(spinlock_t *lock)
{
    lock->lock = 0;
    lock->flags = 0;
}

static inline void pause(void)
{
    __asm__ volatile("pause");
}

static inline uint64_t read_rflags(void)
{
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
    return rflags;
}

static inline void cli(void)
{
    __asm__ volatile("cli");
}

static inline void sti(void)
{
    __asm__ volatile("sti");
}

void spinlock_acquire(spinlock_t *lock)
{
    while (__atomic_test_and_set(&lock->lock, __ATOMIC_ACQUIRE)) {
        pause();
    }
}

void spinlock_release(spinlock_t *lock)
{
    __atomic_clear(&lock->lock, __ATOMIC_RELEASE);
}

void spinlock_acquire_irqsave(spinlock_t *lock)
{
    uint64_t rflags = read_rflags();
    cli();
    
    // We store the IF bit (bit 9) of the *previous* state
    // but we can just store the whole rflags for simplicity if we are careful.
    // However, we only have a 32-bit flags field in struct.
    // Storing full 64-bit rflags is safer. Let's update struct if needed?
    // Actually, we usually pass flags *out* to the caller in standard implementations (like Linux).
    // But here the signature I designed was `spinlock_acquire_irqsave(spinlock_t *lock)`.
    // This implies the lock struct holds the flags. THIS IS DANGEROUS for recursive locking or
    // if multiple locks are held (nesting order matters).
    // Better design: `uint64_t flags; spinlock_acquire_irqsave(lock, &flags);`
    // Or just store it in the lock if we guarantee no nested acquisition of *different* locks that share the same storage?
    // Actually, `spinlock_irqsave` usually returns the flags.
    
    // FOR NOW: I will change the design to store flags in the lock struct, BUT
    // this assumes strict LIFO usage if we ever nest. But wait, if we nest locks:
    // Lock A (saves flags) -> Lock B (saves flags).
    // Release B (restores flags) -> Release A (restores flags).
    // This is fine as long as we hold the lock instance.
    // The issue is if we do `acquire(A)` then `acquire(A)` (deadlock anyway).
    // Or `acquire(A)` then `acquire(B)`. B's flags field is separate. So it's fine.
    
    // One edge case: `acquire(A)` (IF was 1). A->flags stores IF=1. CLI.
    // `acquire(B)` (IF is 0). B->flags stores IF=0. CLI (noop).
    // `release(B)` -> restores IF=0. Correct.
    // `release(A)` -> restores IF=1. Correct.
    
    // Implementation:
    lock->flags = (uint32_t)(rflags & 0x200); // Save IF bit
    
    while (__atomic_test_and_set(&lock->lock, __ATOMIC_ACQUIRE)) {
        pause();
    }
}

void spinlock_release_irqrestore(spinlock_t *lock)
{
    uint32_t saved_if = lock->flags;
    __atomic_clear(&lock->lock, __ATOMIC_RELEASE);
    
    if (saved_if) {
        sti();
    }
    // If not saved_if, we leave interrupts disabled.
}
