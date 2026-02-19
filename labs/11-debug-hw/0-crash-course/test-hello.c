#include "rpi.h"
#include "single-step-lib.h"

void notmain(void) {
    kmalloc_init_mb(1);

    enum { stack_size = 64 * 1024 };
    void *stack = kmalloc(stack_size);

    void hello_asm(void);

    trace("******************<hello_asm>******************************\n");

    trace("about to print hello world single-step, w/ yapping:\n");
    single_step_verbose(1);
    single_step_reset_count();
    single_step_run("hello_asm", hello_asm, 0, stack, stack_size);
    trace("ran %d instructions\n", single_step_get_count());

    trace("about to print hello world single-step, no yapping:\n");
    single_step_verbose(0);
    single_step_reset_count();
    single_step_run("hello_asm", hello_asm, 0, stack, stack_size);
    trace("ran %d instructions\n", single_step_get_count());
}
