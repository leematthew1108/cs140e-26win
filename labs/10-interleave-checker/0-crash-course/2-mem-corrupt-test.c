#include "single-step-lib.h"

static volatile uint32_t target = 0;

static void write_target(void) {
    target = 1234;
}

void notmain(void) {
    kmalloc_init_mb(1);

    enum { stack_size = 64 * 1024 };
    void *stack = kmalloc(stack_size);

    single_step_watch_addr32(&target);

    single_step_verbose(0);
    single_step_trace_regs(0);

    single_step_run("write_target", write_target, 0, stack, stack_size);

    trace("DONE: target=%x\n", target);
}
