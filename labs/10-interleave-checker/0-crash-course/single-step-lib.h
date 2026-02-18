#ifndef __SINGLE_STEP_LIB_H__
#define __SINGLE_STEP_LIB_H__

#include "rpi.h"
#include "breakpoint.h"
#include "full-except.h"
#include "cpsr-util.h"
#include "single-step-syscalls.h"
#include "fast-hash32.h"

void single_step_verbose(int on);

void single_step_reset_count(void);
unsigned single_step_get_count(void);

// Run <fp(arg)> at user level under mismatch single-step.
// If <stack> is non-null, <nbytes> must be the size.
// Returns registers captured at exit.
regs_t single_step_run(const char *fn_name,
                       void (*fp)(),
                       uint32_t arg,
                       void *stack,
                       uint32_t nbytes);



// Enable/disable printing of register diffs each instruction.
void single_step_trace_regs(int on);

// Get the running hash computed over all register snapshots.
uint32_t single_step_get_reg_hash(void);

// Reset internal trace state (prev regs + hash + inst count).
void single_step_reset_trace(void);


// instruction profiler API
// Initialize a histogram over [pc_min, pc_max).
// pc_min/pc_max should be 4-byte aligned. pc_max > pc_min.
void single_step_prof_init(uint32_t pc_min, uint32_t pc_max);

// Enable/disable profiling (histogram increments).
void single_step_prof_on(int on);

// Zero out histogram counts.
void single_step_prof_reset(void);

// Dump PCs whose count >= min_count.
void single_step_prof_dump(unsigned min_count);

// Watch a 32-bit address for writes.
// If enabled, single-step handler will detect changes to *addr
// and panic with the current pc.
void single_step_watch_addr32(volatile uint32_t *addr);
void single_step_watch_disable(void);


#endif
