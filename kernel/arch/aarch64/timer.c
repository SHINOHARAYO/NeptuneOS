#include <stdint.h>
#include <kernel/io.h>
#include <arch/gic.h>
#include <kernel/log.h>

#define CNTFRQ_EL0  "cntfrq_el0"
#define CNTP_TVAL_EL0 "cntp_tval_el0"
#define CNTP_CTL_EL0 "cntp_ctl_el0"

static uint64_t timer_interval = 0;

static inline uint64_t read_cntfrq(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static inline void write_cntp_tval(uint64_t val)
{
    __asm__ volatile("msr cntp_tval_el0, %0" : : "r"(val));
}

static inline void write_cntp_ctl(uint64_t val)
{
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(val));
}

void pit_init(uint32_t freq)
{
    /* Use ARM Generic Timer */
    uint64_t cntfrq = read_cntfrq();
    log_info("ARM Generic Timer: Frequency read");

    if (freq == 0) freq = 100;

    timer_interval = cntfrq / freq;
    
    write_cntp_tval(timer_interval);
    write_cntp_ctl(1); /* Enable, no IMASK */

    /* Enable PPI 30 (Physical Timer) at GIC */
    /* PPIs are 16 - 31. ID 30 is correct. */
    gic_enable_irq(30);

    log_info("ARM Generic Timer initialized");
}

void arm_timer_reload(void)
{
    write_cntp_tval(timer_interval);
    write_cntp_ctl(1);
}
