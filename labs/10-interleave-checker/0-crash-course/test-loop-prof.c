#include "rpi.h"
#include "memmap.h"
#include "single-step-lib.h"

static void simple_loop(void) {
    volatile unsigned sum = 0;

    for(unsigned i = 0; i < 1000; i++)
        sum += i;
}

void notmain(void) {
    kmalloc_init_mb(1);

    enum { stack_size = 64 * 1024 };
    void *stack = kmalloc(stack_size);

    // initialize profiler over entire code segment
    single_step_prof_init((uint32_t)__code_start__,
                          (uint32_t)__code_end__);

    single_step_prof_on(1);
    single_step_prof_reset();

    // reduce noise
    single_step_verbose(0);
    single_step_trace_regs(0);

    single_step_run("simple_loop", simple_loop, 0, stack, stack_size);

    // turn printing back on for dump
    single_step_verbose(1);
    single_step_prof_dump(100);

    trace("n_inst=%u\n", single_step_get_count());
    trace("hash=%x\n", single_step_get_reg_hash());
}
