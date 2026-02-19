#include "rpi.h"
#include "single-step-lib.h"

void notmain(void) {
    kmalloc_init_mb(1);

    // turn on single step tracing (print registers that have changed since last step)
    single_step_trace_regs(1);

    trace("******************<nop_1>******************************\n");
    void nop_1(void);
    single_step_reset_count();
    single_step_reset_trace();
    single_step_run("nop_1", nop_1, 0, 0, 0);

    trace("******************<nop_10>******************************\n");
    void nop_10(void);
    single_step_reset_count();
    single_step_reset_trace();
    regs_t r = single_step_run("nop_10", nop_10, 0, 0, 0);

    extern uint32_t exit_tramp_pc[]; // from single-step-start.S
    trace("expect: pc should be %x!\n", exit_tramp_pc);

    void *exit_pc = (void *)r.regs[REGS_PC];
    if(exit_pc != exit_tramp_pc)
        panic("final pc should be = %x, is %x!\n", exit_tramp_pc, exit_pc);

    trace("SUCCESS: exit pc=%x!\n", exit_pc);
}
