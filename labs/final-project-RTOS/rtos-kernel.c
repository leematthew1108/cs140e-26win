#include "rpi.h"
#include "rpi-interrupts.h"
#include "timer-interrupt.h"
#include "switchto.h"

// Implemented in our interrupt/trampoline assembly.
void timer_irq_handler(regs_t *saved);
void rtos_swi_handler(regs_t *saved);

// Provided by our local IRQ entry assembly.
void enable_interrupts(void);
void disable_interrupts(void);

enum { STACK_WORDS = 512 };

// Scheduling: fixed priorities (higher number => higher priority).
enum { PR_IDLE = 0, PR_L = 1, PR_M = 2, PR_H = 3 };

// Periods (us). H must be 1kHz.
enum {
    PERIOD_H_US = 1000,
    PERIOD_M_US = 2000,
    PERIOD_L_US = 5000,
};

// How many releases each periodic task should execute.
// Number of periodic releases per task (used for stats).
// Increase to make the distributions smoother.
enum { N_RELEASES = 300 };

// Bounded compute / mutex hold times (cycle counts).
enum {
    // Workload sizing chosen to keep both RTOS and baseline reliable.
    // Later we can increase contention if needed.
    H_COMPUTE_US = 100,
    // Increase lower-priority compute so the deferred baseline
    // shows more release/start jitter than the preemptive RTOS.
    M_COMPUTE_US = 320,
    L_HOLD_MUTEX_US = 900,
    // Extra compute outside the mutex critical section for L.
    // This increases CPU load without directly inflating lock hold time.
    L_EXTRA_COMPUTE_US = 350,
};

typedef enum {
    TASK_READY = 0,
    TASK_SLEEPING = 1,
    TASK_BLOCKED = 2,
} task_state_t;

typedef struct tcb tcb_t;

typedef struct mutex {
    tcb_t *owner;
    tcb_t *waitq_head; // sorted by priority (high->low)
} mutex_t;

struct tcb {
    regs_t regs;
    uint32_t stack[STACK_WORDS];
    uint32_t priority;
    task_state_t state;

    // Timed waiting.
    uint32_t wake_us;

    // Mutex waiting queue linkage.
    tcb_t *next_wait;
    uint32_t in_mutex_wait;

    // Stats (accumulate).
    int32_t jitter_min_us;
    int32_t jitter_max_us;
    int32_t jitter_sum_us;
    uint64_t jitter_sumsq_us2;
    uint32_t jitter_samples;

    uint32_t lock_lat_min_us;
    uint32_t lock_lat_max_us;
    uint32_t lock_lat_sum_us;
    uint64_t lock_lat_sumsq_us2;
    uint32_t lock_lat_samples;

    // Work counter / completion.
    uint32_t releases_done;
};

static tcb_t idle_t, tH, tM, tL;
static tcb_t *cur;
static mutex_t g_mutex;

static volatile uint32_t tick_cnt;
static volatile uint32_t finished_cnt;
static volatile uint32_t stats_printed;
static volatile uint32_t done_all;
// Becomes 1 once the first scheduled thread has started.
// Prevents early timer IRQs from running scheduling logic before
// the thread state/cur pointers are valid.
static volatile uint32_t rtos_started;

// Used to resume interrupted context safely during early boot.
// Must not live on the IRQ stack (switchto may adjust SP early).
static regs_t irq_resume_regs;

static uint32_t base_us;

// (no baseline-only debug prints in final runs)

// Histogram configuration (printed to UART at end).
// Jitter is signed, but for this workload it's usually >= 0. We still
// keep a negative bucket.
enum {
    JIT_BIN_US = 50,
    JIT_MAX_US = 3000,
    JIT_NBINS = (JIT_MAX_US / JIT_BIN_US) + 2, // [0]=neg, [1..]=0.., last=overflow
    LOCK_BIN_US = 20,
    LOCK_MAX_US = 1000,
    LOCK_NBINS = (LOCK_MAX_US / LOCK_BIN_US) + 1, // last = overflow
};

static uint32_t jit_hist_H[JIT_NBINS], jit_hist_M[JIT_NBINS], jit_hist_L[JIT_NBINS];
static uint32_t lock_hist_H[LOCK_NBINS], lock_hist_L[LOCK_NBINS];

static inline unsigned jit_bucket(int32_t jitter_us) {
    if (jitter_us < 0) return 0;
    unsigned b = (unsigned)jitter_us / JIT_BIN_US;
    if (b >= (JIT_NBINS - 1)) return (JIT_NBINS - 1);
    return 1 + b;
}

static inline unsigned lock_bucket(uint32_t lat_us) {
    unsigned b = lat_us / LOCK_BIN_US;
    if (b >= (LOCK_NBINS - 1)) return (LOCK_NBINS - 1);
    return b;
}

static tcb_t *pick_next_ready(void) {
    tcb_t *next = &idle_t;

    if (tL.state == TASK_READY && tL.priority > next->priority) next = &tL;
    if (tM.state == TASK_READY && tM.priority > next->priority) next = &tM;
    if (tH.state == TASK_READY && tH.priority > next->priority) next = &tH;

    return next;
}

static inline void yield_swi(void) {
    // Voluntary switch point used by sleep_until and mutex_lock.
    asm volatile("swi 0" : : : "memory");
}

// Called by our local IRQ entry assembly.
// Must never return.
__attribute__((noreturn))
void timer_irq_handler(regs_t *saved) {
    dev_barrier();

    // Acknowledge/clear timer IRQ.
    unsigned pending = GET32(IRQ_basic_pending);
    if ((pending & ARM_Timer_IRQ) == 0) {
        // Unexpected interrupt source: resume the interrupted thread.
        if (!rtos_started) {
            irq_resume_regs = *saved;
            switchto(&irq_resume_regs);
        }
        cur->regs = *saved;
        switchto(&cur->regs);
    }

    PUT32(ARM_Timer_IRQ_Clear, 1);
    dev_barrier();

    uint32_t now_us = timer_get_usec_raw();

    // Early boot guard: before the first thread runs, avoid saving
    // interrupted context into `cur->regs` / touching thread state.
    // Just resume the interrupted context.
    if (!rtos_started) {
        irq_resume_regs = *saved;
        switchto(&irq_resume_regs);
    }

    // Save interrupted context.
    cur->regs = *saved;

    tick_cnt++;

    // Wake up sleeping periodic tasks.
    if (tH.state == TASK_SLEEPING && now_us >= tH.wake_us) tH.state = TASK_READY;
    if (tM.state == TASK_SLEEPING && now_us >= tM.wake_us) tM.state = TASK_READY;
    if (tL.state == TASK_SLEEPING && now_us >= tL.wake_us) tL.state = TASK_READY;

#ifdef NONPREEMPTIVE
    // Baseline: "deferred" behavior (no true preemption).
    // - If the interrupted thread is IDLE, allow the scheduler to start any
    //   newly-ready higher-priority periodic tasks.
    // - Otherwise, just resume the interrupted thread, so we avoid mid-job
    //   context changes.
    if (cur->state == TASK_READY && cur != &idle_t) {
        switchto(&cur->regs);
    } else {
        tcb_t *next = pick_next_ready();
        cur = next;
        switchto(&cur->regs);
    }
#else
    // True preemption: select and restore the highest-priority READY thread
    // directly inside the timer IRQ handler.
    cur = pick_next_ready();
    switchto(&cur->regs);
#endif
    not_reached();
}

// Called by the SWI trampoline for voluntary context switching.
__attribute__((noreturn))
void rtos_swi_handler(regs_t *saved) {
    dev_barrier();

    // Save interrupted context.
    cur->regs = *saved;

    // For SWI, we do not advance time or wake sleeping tasks.
    cur = pick_next_ready();
    switchto(&cur->regs);
    not_reached();
}

static inline uint32_t timer_now_us(void) {
    dev_barrier();
    uint32_t u = timer_get_usec_raw();
    dev_barrier();
    return u;
}

static void rtos_sleep_until(uint32_t wake_us) {
    uint32_t now_us = timer_now_us();
    if (now_us >= wake_us) {
#ifdef NONPREEMPTIVE
        // Baseline/cooperative behavior:
        // If we're already late, yield once to let other READY threads run.
        yield_swi();
#endif
        return;
    }

    cur->wake_us = wake_us;
    cur->state = TASK_SLEEPING;

    // Switch away immediately; we'll be made READY by the timer IRQ.
    yield_swi();

    // When we resume, the timer IRQ should have already set READY.
    cur->state = TASK_READY;
}

static void busy_delay_us(uint32_t us) {
    uint32_t start = timer_get_usec_raw();
    while ((timer_get_usec_raw() - start) < us) {
        ;
    }
}

#ifdef NONPREEMPTIVE
static void baseline_sleep_until(uint32_t wake_us) {
    disable_interrupts();
    cur->wake_us = wake_us;
    cur->state = TASK_SLEEPING;
    enable_interrupts();
    yield_swi();
    cur->state = TASK_READY;
}
#endif

static void mark_finished_and_maybe_print(void) {
    disable_interrupts();
    finished_cnt++;
    uint32_t v = finished_cnt;

    if (v == 3 && !stats_printed) {
        stats_printed = 1;
        done_all = 1;

        enable_interrupts();

#ifdef NONPREEMPTIVE
        printk("BASELINE stats (fixed-priority, NONPREEMPTIVE timer)\n");
#else
        printk("RTOS stats (fixed-priority preemptive timer)\n");
#endif
        printk("timer: tick_cnt=%u base_us=%u\n", tick_cnt, base_us);

        printk("H jitter: min=%d max=%d sum=%d sumsq_hi=%u sumsq_lo=%u samples=%u lock_lat: min=%u max=%u sum=%u sumsq_hi=%u sumsq_lo=%u samples=%u\n",
               tH.jitter_min_us, tH.jitter_max_us, tH.jitter_sum_us,
               (uint32_t)(tH.jitter_sumsq_us2 >> 32), (uint32_t)(tH.jitter_sumsq_us2 & 0xffffffff), tH.jitter_samples,
               tH.lock_lat_min_us, tH.lock_lat_max_us, tH.lock_lat_sum_us,
               (uint32_t)(tH.lock_lat_sumsq_us2 >> 32), (uint32_t)(tH.lock_lat_sumsq_us2 & 0xffffffff), tH.lock_lat_samples);

        printk("M jitter: min=%d max=%d sum=%d sumsq_hi=%u sumsq_lo=%u samples=%u\n",
               tM.jitter_min_us, tM.jitter_max_us, tM.jitter_sum_us,
               (uint32_t)(tM.jitter_sumsq_us2 >> 32), (uint32_t)(tM.jitter_sumsq_us2 & 0xffffffff), tM.jitter_samples);

        printk("L jitter: min=%d max=%d sum=%d sumsq_hi=%u sumsq_lo=%u samples=%u lock_lat: min=%u max=%u sum=%u sumsq_hi=%u sumsq_lo=%u samples=%u\n",
               tL.jitter_min_us, tL.jitter_max_us, tL.jitter_sum_us,
               (uint32_t)(tL.jitter_sumsq_us2 >> 32), (uint32_t)(tL.jitter_sumsq_us2 & 0xffffffff), tL.jitter_samples,
               tL.lock_lat_min_us, tL.lock_lat_max_us, tL.lock_lat_sum_us,
               (uint32_t)(tL.lock_lat_sumsq_us2 >> 32), (uint32_t)(tL.lock_lat_sumsq_us2 & 0xffffffff), tL.lock_lat_samples);

        printk("H jitter_hist: neg=%u bin_us=%u max_us=%u overflow=%u\n", jit_hist_H[0], JIT_BIN_US, JIT_MAX_US, jit_hist_H[JIT_NBINS - 1]);
        printk("M jitter_hist: neg=%u bin_us=%u max_us=%u overflow=%u\n", jit_hist_M[0], JIT_BIN_US, JIT_MAX_US, jit_hist_M[JIT_NBINS - 1]);
        printk("L jitter_hist: neg=%u bin_us=%u max_us=%u overflow=%u\n", jit_hist_L[0], JIT_BIN_US, JIT_MAX_US, jit_hist_L[JIT_NBINS - 1]);
        printk("H lock_hist: bin_us=%u max_us=%u overflow=%u\n", LOCK_BIN_US, LOCK_MAX_US, lock_hist_H[LOCK_NBINS - 1]);
        printk("L lock_hist: bin_us=%u max_us=%u overflow=%u\n", LOCK_BIN_US, LOCK_MAX_US, lock_hist_L[LOCK_NBINS - 1]);

        // Print the actual bucket counts as a compact list.
        printk("H jitter_hist_counts:");
        for (unsigned i = 0; i < JIT_NBINS; i++) printk(" %u", jit_hist_H[i]);
        printk("\n");
        printk("M jitter_hist_counts:");
        for (unsigned i = 0; i < JIT_NBINS; i++) printk(" %u", jit_hist_M[i]);
        printk("\n");
        printk("L jitter_hist_counts:");
        for (unsigned i = 0; i < JIT_NBINS; i++) printk(" %u", jit_hist_L[i]);
        printk("\n");

        printk("H lock_hist_counts:");
        for (unsigned i = 0; i < LOCK_NBINS; i++) printk(" %u", lock_hist_H[i]);
        printk("\n");
        printk("L lock_hist_counts:");
        for (unsigned i = 0; i < LOCK_NBINS; i++) printk(" %u", lock_hist_L[i]);
        printk("\n");

        clean_reboot();
        not_reached();
    }

    enable_interrupts();
}

static void mutex_wait_enqueue(mutex_t *m, tcb_t *t) {
    // Insert in priority order: head has the highest priority.
    tcb_t **p = &m->waitq_head;
    while (*p && (*p)->priority >= t->priority) {
        p = &(*p)->next_wait;
    }
    t->next_wait = *p;
    *p = t;
}

static uint32_t mutex_lock(mutex_t *m) {
    uint32_t req_us = timer_now_us();

    for (;;) {
        if (m->owner == cur) {
            // Resumed after being handed ownership.
            cur->in_mutex_wait = 0;
            return timer_now_us() - req_us;
        }

        if (m->owner == 0) {
            m->owner = cur;
            return timer_now_us() - req_us;
        }

        // Block until unlocked hands ownership to us.
        disable_interrupts();
        if (!cur->in_mutex_wait) {
            mutex_wait_enqueue(m, cur);
            cur->in_mutex_wait = 1;
        }
        cur->state = TASK_BLOCKED;
        enable_interrupts();

        // Voluntary switch away; we'll resume once we're handed ownership.
        yield_swi();

        // Once we resume, ownership should have been transferred.
        // Loop re-check keeps this robust.
    }
}

static void mutex_unlock(mutex_t *m) {
    if (m->owner != cur) {
        panic("mutex_unlock from non-owner\n");
    }

    tcb_t *w = 0;
    disable_interrupts();
    if (m->waitq_head) {
        // Priority-ordered waiters: transfer ownership to the head.
        w = m->waitq_head;
        m->waitq_head = w->next_wait;
        w->next_wait = 0;
        m->owner = w;
        w->state = TASK_READY;
        w->in_mutex_wait = 0;
    } else {
        m->owner = 0;
    }
    enable_interrupts();

    // If the woken task has higher priority, switch away promptly.
#ifndef NONPREEMPTIVE
    if (w && w->priority > cur->priority) yield_swi();
#endif
}

static void task_idle(void) {
    for (;;) {
        if (done_all)
            clean_reboot();

        // In non-preemptive baseline mode, idle must yield so that newly
        // woken periodic tasks can run after `rtos_sleep_until()`.
        yield_swi();
    }
}

static void stats_update_jitter(tcb_t *t, int32_t jitter_us) {
    uint64_t ju2 = (uint64_t)((int64_t)jitter_us * (int64_t)jitter_us);
    if (t->jitter_samples == 0) {
        t->jitter_min_us = jitter_us;
        t->jitter_max_us = jitter_us;
        t->jitter_sum_us = jitter_us;
        t->jitter_sumsq_us2 = ju2;
        t->jitter_samples = 1;
    } else {
        if (jitter_us < t->jitter_min_us) t->jitter_min_us = jitter_us;
        if (jitter_us > t->jitter_max_us) t->jitter_max_us = jitter_us;
        t->jitter_sum_us += jitter_us;
        t->jitter_sumsq_us2 += ju2;
        t->jitter_samples++;
    }
}

static void stats_update_lock_lat(tcb_t *t, uint32_t lat_us) {
    uint64_t lu2 = (uint64_t)lat_us * (uint64_t)lat_us;
    if (t->lock_lat_samples == 0) {
        t->lock_lat_min_us = lat_us;
        t->lock_lat_max_us = lat_us;
        t->lock_lat_sum_us = lat_us;
        t->lock_lat_sumsq_us2 = lu2;
        t->lock_lat_samples = 1;
    } else {
        if (lat_us < t->lock_lat_min_us) t->lock_lat_min_us = lat_us;
        if (lat_us > t->lock_lat_max_us) t->lock_lat_max_us = lat_us;
        t->lock_lat_sum_us += lat_us;
        t->lock_lat_sumsq_us2 += lu2;
        t->lock_lat_samples++;
    }
}

static void periodic_task_H(void) {
    // Mark RTOS as ready for timer-driven scheduling.
    rtos_started = 1;

    uint32_t next_us = base_us;
    for (uint32_t i = 0; i < N_RELEASES; i++) {
        rtos_sleep_until(next_us);

        uint32_t start_us = timer_now_us();
        int32_t jitter = (int32_t)(start_us - next_us);
        stats_update_jitter(&tH, jitter);
        jit_hist_H[jit_bucket(jitter)]++;

        // Measure time-to-acquire the mutex.
        uint32_t lock_lat_us = mutex_lock(&g_mutex);
        stats_update_lock_lat(&tH, lock_lat_us);
        lock_hist_H[lock_bucket(lock_lat_us)]++;

        busy_delay_us(H_COMPUTE_US);
        mutex_unlock(&g_mutex);

#ifdef NONPREEMPTIVE
        next_us += PERIOD_H_US;
        baseline_sleep_until(next_us);
#else
        next_us += PERIOD_H_US;
#endif
    }

    tH.releases_done = 1;
    mark_finished_and_maybe_print();
    // Finished task: do not keep winning fixed-priority scheduling.
    disable_interrupts();
    cur->state = TASK_BLOCKED;
    enable_interrupts();
    yield_swi();
    not_reached();
}

static void periodic_task_M(void) {
    uint32_t next_us = base_us;
    for (uint32_t i = 0; i < N_RELEASES; i++) {
        rtos_sleep_until(next_us);

        uint32_t start_us = timer_now_us();
        int32_t jitter = (int32_t)(start_us - next_us);
        stats_update_jitter(&tM, jitter);
        jit_hist_M[jit_bucket(jitter)]++;

        busy_delay_us(M_COMPUTE_US);

#ifdef NONPREEMPTIVE
        next_us += PERIOD_M_US;
        baseline_sleep_until(next_us);
#else
        next_us += PERIOD_M_US;
#endif
    }

    tM.releases_done = 1;
    mark_finished_and_maybe_print();
    disable_interrupts();
    cur->state = TASK_BLOCKED;
    enable_interrupts();
    yield_swi();
    not_reached();
}

static void periodic_task_L(void) {
    uint32_t next_us = base_us;
    for (uint32_t i = 0; i < N_RELEASES; i++) {
        rtos_sleep_until(next_us);

        uint32_t start_us = timer_now_us();
        int32_t jitter = (int32_t)(start_us - next_us);
        stats_update_jitter(&tL, jitter);
        jit_hist_L[jit_bucket(jitter)]++;

        // Low priority holds the mutex for a bounded time to create contention.
        uint32_t lock_lat_us = mutex_lock(&g_mutex);
        stats_update_lock_lat(&tL, lock_lat_us);
        lock_hist_L[lock_bucket(lock_lat_us)]++;
        busy_delay_us(L_HOLD_MUTEX_US);
        mutex_unlock(&g_mutex);
        busy_delay_us(L_EXTRA_COMPUTE_US);

#ifdef NONPREEMPTIVE
        next_us += PERIOD_L_US;
        baseline_sleep_until(next_us);
#else
        next_us += PERIOD_L_US;
#endif
    }

    tL.releases_done = 1;
    mark_finished_and_maybe_print();
    disable_interrupts();
    cur->state = TASK_BLOCKED;
    enable_interrupts();
    yield_swi();
    not_reached();
}

static void task_exit(int retv) {
    (void)retv;
    clean_reboot();
    not_reached();
}

void notmain(void) {
    printk("RTOS kernel: start\n");

    interrupt_init();
    // No cycle-counter init needed for timer-based busy delays.

    // ~1ms tick: with prescale=16, load=0x3d gives delta ~= 1ms on RPi Zero.
    timer_init(16, 0x3d);

    // Initialize globals.
    tick_cnt = 0;
    finished_cnt = 0;
    stats_printed = 0;
    done_all = 0;
    rtos_started = 0;

    g_mutex.owner = 0;
    g_mutex.waitq_head = 0;

    // Initialize TCBs.
    uint32_t task_cpsr = SYS_MODE; // kernel-mode but shares sp/lr with USER for caret (^)

    idle_t.priority = PR_IDLE;
    idle_t.state = TASK_READY;
    idle_t.regs = switchto_mk((uint32_t)task_idle, &idle_t.stack[STACK_WORDS], task_cpsr, task_exit);

    tH.priority = PR_H;
    tM.priority = PR_M;
    tL.priority = PR_L;

    tH.state = TASK_READY;
    tM.state = TASK_READY;
    tL.state = TASK_READY;
    tH.regs = switchto_mk((uint32_t)periodic_task_H, &tH.stack[STACK_WORDS], task_cpsr, task_exit);
    tM.regs = switchto_mk((uint32_t)periodic_task_M, &tM.stack[STACK_WORDS], task_cpsr, task_exit);
    tL.regs = switchto_mk((uint32_t)periodic_task_L, &tL.stack[STACK_WORDS], task_cpsr, task_exit);

    // Zero stats counters.
    tH.jitter_samples = 0;
    tM.jitter_samples = 0;
    tL.jitter_samples = 0;
    tH.jitter_sumsq_us2 = 0;
    tM.jitter_sumsq_us2 = 0;
    tL.jitter_sumsq_us2 = 0;
    tH.lock_lat_samples = 0;
    tH.lock_lat_sumsq_us2 = 0;
    tH.in_mutex_wait = 0;
    tM.in_mutex_wait = 0;
    tL.in_mutex_wait = 0;
    tL.lock_lat_samples = 0;
    tL.lock_lat_sumsq_us2 = 0;

    for (unsigned i = 0; i < JIT_NBINS; i++) {
        jit_hist_H[i] = 0;
        jit_hist_M[i] = 0;
        jit_hist_L[i] = 0;
    }
    for (unsigned i = 0; i < LOCK_NBINS; i++) {
        lock_hist_H[i] = 0;
        lock_hist_L[i] = 0;
    }

    // All periodic tasks use the same base time for expected releases.
    base_us = timer_now_us();

    cur = &tH;

    enable_interrupts();
    switchto(&cur->regs);
    not_reached();
}

