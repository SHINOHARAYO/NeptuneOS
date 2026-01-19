#include "kernel/pipe.h"
#include "kernel/heap.h"
#include "kernel/spinlock.h"
#include "kernel/sched.h"
#include "kernel/vfs.h"
#include "kernel/syscall.h"

#define PIPE_SIZE 4096

struct pipe {
    uint8_t buffer[PIPE_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t used;
    int readers;
    int writers;
    spinlock_t lock;
    wait_queue_t read_wait;
    wait_queue_t write_wait;
};

/* We need to modify vfs.c to call us, so we assume vfs_file has a 'void *priv' or specific 'struct pipe *pipe' field */
/* vfs.c defines struct vfs_file internally, so we can't dereference it here easily unless we move definition to header 
   or access via opaque pointer. 
   Actually vfs.c implementation logic is needed. 
   But wait, pipe_create allocates struct vfs_file? No, vfs_alloc is static in vfs.c.
   
   Better plan: 
   Add pipe logic to vfs.c directly or expose vfs_alloc.
   To include pipe.c, we can make it a module used by vfs.c.
   
   Let's keep pipe implementation here but we need a 'struct pipe' which is pointed to by vfs_file.
*/

struct pipe *pipe_alloc_struct(void)
{
    struct pipe *p = (struct pipe *)kalloc_zero(sizeof(*p), 16);
    if (!p) return NULL;
    wait_queue_init(&p->read_wait);
    wait_queue_init(&p->write_wait);
    p->readers = 1; /* Initial reader */
    p->writers = 1; /* Initial writer */
    return p;
}

int64_t pipe_read_impl(struct pipe *p, void *buf, uint64_t len)
{
    if (!p || !buf) return -1;
    
    spinlock_acquire_irqsave(&p->lock);
    
    while (p->used == 0) {
        if (p->writers == 0) {
            /* EOF */
            spinlock_release_irqrestore(&p->lock);
            return 0; 
        }
        /* Block */
        /* sched_sleep release lock internally? No, sched_sleep releases global sched_lock not our lock. 
           We need to sleep with our lock held? No.
           Standard pattern: release, sleep, acquire.
           But race condition.
           We need sched_sleep_unlock(&p->lock)?
           NeptuneOS sched_sleep acquires sched_lock.
           
           Let's look at sched.c. sched_sleep(wq) just blocks current thread.
           We should add ourselves to WQ, release p->lock, then sleep?
           Race: if we release p->lock, someone interprets WQ before we sleep?
           
           We need a reliable sleep.
           For now, let's just busy-yield if no proper sleep-with-lock.
           Or use 'sched_sleep' and rely on the fact that we released p->lock before calling it, 
           assuming WQ is protected by sched_lock.
           
           Wait queues in sched.c ARE protected by sched_lock.
           So:
           1. acquire sched_lock
           2. add to WQ
           3. release sched_lock
           4. switch
           
           But we hold p->lock.
           If we release p->lock, then acquire sched_lock, we might miss a makeup.
           
           Correct: 
           spinlock_acquire(&sched_lock);
           spinlock_release(&p->lock); // Now we strictly hold sched_lock, checking condition again? 
                                       // No, we can't check p->used without p->lock easily unless atomic.
           
           Let's use busy loop with yield for now if we haven't implemented robust condition variables.
           Actually sched.h has sched_sleep_cond? No.
           
           Let's assume simple yielding for now to keep it safe.
        */
        spinlock_release_irqrestore(&p->lock);
        sched_yield();
        spinlock_acquire_irqsave(&p->lock);
    }
    
    uint64_t read = 0;
    uint8_t *out = (uint8_t *)buf;
    
    while (read < len && p->used > 0) {
        out[read++] = p->buffer[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_SIZE;
        p->used--;
    }
    
    // valid? wake writers?
    // sched_wake_all(&p->write_wait); // if we had wait queues
    
    spinlock_release_irqrestore(&p->lock);
    return read;
}

int64_t pipe_write_impl(struct pipe *p, const void *buf, uint64_t len)
{
    if (!p || !buf) return -1;
    
    spinlock_acquire_irqsave(&p->lock);
    
    if (p->readers == 0) {
        // Broken pipe
        spinlock_release_irqrestore(&p->lock);
        return -SYSCALL_EIO; // EPIPE
    }
    
    const uint8_t *in = (const uint8_t *)buf;
    uint64_t written = 0;
    
    while (written < len) {
        while (p->used == PIPE_SIZE) {
            if (p->readers == 0) {
                spinlock_release_irqrestore(&p->lock);
                return -SYSCALL_EIO;
            }
            spinlock_release_irqrestore(&p->lock);
            sched_yield();
            spinlock_acquire_irqsave(&p->lock);
        }
        
        while (written < len && p->used < PIPE_SIZE) {
            p->buffer[p->write_pos] = in[written++];
            p->write_pos = (p->write_pos + 1) % PIPE_SIZE;
            p->used++;
        }
    }
    
    spinlock_release_irqrestore(&p->lock);
    return written;
}

void pipe_close_impl(struct pipe *p, int is_writer)
{
    if (!p) return;
    spinlock_acquire_irqsave(&p->lock);
    if (is_writer) {
        p->writers--;
    } else {
        p->readers--;
    }
    int loose = (p->readers == 0 && p->writers == 0);
    spinlock_release_irqrestore(&p->lock);
    
    if (loose) {
        kfree(p);
    }
}
