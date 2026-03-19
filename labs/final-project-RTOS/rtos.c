#include "rpi.h"
#include "rpi-interrupts.h"
#include "timer-interrupt.h"
// For switchto/reg context.
#include "switchto.h"

// Provided by our local IRQ entry assembly.
void disable_interrupts(void);
void enable_interrupts(void);

//*******************************************************
// Preemption sanity test:
// - tasks run in SYS_MODE
// - timer IRQ does full-context save and switches to another thread
// - we validate that both threads resume correctly

enum { STACK_WORDS = 256 };
enum { TARGET_TICKS = 500 };

typedef struct tcb {
    regs_t regs;
    uint32_t stack[STACK_WORDS];
    volatile uint32_t exec_cnt;
} tcb_t;

static tcb_t t0, t1;
static volatile uint32_t tick_cnt;
static volatile uint32_t done;
static tcb_t *cur;
static volatile uint32_t printed;

// IRQ timing metrics (for tuning the 1ms tick rate).
static volatile uint32_t last_irq_us;
static volatile uint32_t delta_min_us;
static volatile uint32_t delta_max_us;
static volatile uint32_t delta_sum_us;
static volatile uint32_t delta_samples;

static void task_exit(int retv) {
    (void)retv;
    printk("task_exit (should not return)\n");
    clean_reboot();
    not_reached();
}

static void task0(void) {
    for (;;) {
        t0.exec_cnt++;
        if (done && !printed) {
            printed = 1;
            printk("PREEMPT sanity: tick_cnt=%u irq[min]=%u irq[max]=%u irq[sum]=%u irq[samples]=%u\n",
                   tick_cnt, delta_min_us, delta_max_us, delta_sum_us, delta_samples);
            clean_reboot();
            not_reached();
        }
    }
}

static void task1(void) {
    for (;;) {
        t1.exec_cnt++;
        // task1 intentionally does not print to avoid UART interleaving.
        (void)done;
    }
}

// Catch any exceptions we didn't expect.
void fast_interrupt_vector(unsigned pc) { panic("rtos: unexpected fast interrupt: pc=%x\n", pc); }
void syscall_vector(unsigned pc) { panic("rtos: unexpected syscall: pc=%x\n", pc); }
void reset_vector(unsigned pc) { panic("rtos: unexpected reset exception: pc=%x\n", pc); }
void undefined_instruction_vector(unsigned pc) { panic("rtos: unexpected undefined instruction: pc=%x\n", pc); }
void prefetch_abort_vector(unsigned pc) { panic("rtos: unexpected prefetch abort: pc=%x\n", pc); }
void data_abort_vector(unsigned pc) { panic("rtos: unexpected data abort: pc=%x\n", pc); }

// Called by our local IRQ entry assembly.
// Must never return.
__attribute__((noreturn))
void timer_irq_handler(regs_t *saved) {
    dev_barrier();

    // Disambiguate which interrupt source fired.
    unsigned pending = GET32(IRQ_basic_pending);
    if ((pending & ARM_Timer_IRQ) == 0) {
        // Unknown interrupt: just resume current thread.
        cur->regs = *saved;
        switchto(&cur->regs);
        not_reached();
    }

    // Acknowledge/clear timer IRQ.
    PUT32(ARM_Timer_IRQ_Clear, 1);
    dev_barrier();

    // Update IRQ timing metrics.
    uint32_t now_us = timer_get_usec_raw();
    uint32_t d = 0;
    if (last_irq_us) {
        d = now_us - last_irq_us;
        if (delta_samples == 0) {
            delta_min_us = d;
            delta_max_us = d;
        } else {
            if (d < delta_min_us) delta_min_us = d;
            if (d > delta_max_us) delta_max_us = d;
        }
        delta_sum_us += d;
        delta_samples++;
    }
    last_irq_us = now_us;

    // Save full register context for the thread we interrupted.
    cur->regs = *saved;

    // Update scheduler tick + done flag.
    tick_cnt++;
    if (tick_cnt >= TARGET_TICKS)
        done = 1;

    // Alternate threads every tick (for sanity-checking preemption correctness).
    cur = (cur == &t0) ? &t1 : &t0;
    switchto(&cur->regs);
    not_reached();
}

void notmain(void) {
    printk("RTOS preempt sanity: init\n");

    interrupt_init();

    // Basic timer setup. We'll retune tick period later once we implement periodic release.
    // Using prescale=16 and load near 0x3d gives ~1ms tick on the RPi Zero.
    timer_init(16, 0x3d);

    // Initialize thread stacks and contexts.
    t0.exec_cnt = 0;
    t1.exec_cnt = 0;
    tick_cnt = 0;
    done = 0;
    printed = 0;

    last_irq_us = timer_get_usec_raw();
    delta_min_us = 0;
    delta_max_us = 0;
    delta_sum_us = 0;
    delta_samples = 0;

    // Run tasks in SYS_MODE so IRQ entry can save interrupted sp/lr via caret (^).
    uint32_t task_cpsr = SYS_MODE; // interrupts enabled (I bit = 0)

    t0.regs = switchto_mk((uint32_t)task0, &t0.stack[STACK_WORDS], task_cpsr, task_exit);
    t1.regs = switchto_mk((uint32_t)task1, &t1.stack[STACK_WORDS], task_cpsr, task_exit);

    // Start with thread 0.
    cur = &t0;

    enable_interrupts();

    // Jump into first task; timer IRQ will preempt and switchto others.
    switchto(&cur->regs);
    not_reached();
}

