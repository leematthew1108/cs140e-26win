#include "single-step-lib.h"

#undef trace

static int verbose_p = 1;
#define trace(msg...) do { if(verbose_p) trace_nofn(msg); } while(0)

void single_step_verbose(int v_p) {
    verbose_p = v_p;
}

static const char *ss_cur_fn = "none";
// count how many instructions.
static volatile unsigned n_inst;

// initial registers: what we <switchto> to resume.
static regs_t scheduler_regs;
// hack to record the registers at exit for printing.
static regs_t exit_regs;

// trace globals
static int trace_regs_p = 0;
static regs_t prev_regs;
static int have_prev = 0;
static uint32_t reg_hash = 0;

// ---------------- profiler state ----------------
static int prof_on = 0;
static uint32_t prof_pc_min = 0, prof_pc_max = 0;
static unsigned prof_n = 0;
static volatile unsigned *prof_hist = 0;
// ------------------------------------------------

// memory watching globals
static volatile uint32_t *watch_addr = 0;
static uint32_t watch_prev = 0;
static int watch_have_prev = 0;


void single_step_trace_regs(int on) { trace_regs_p = on; }

uint32_t single_step_get_reg_hash(void) { return reg_hash; }

void single_step_reset_trace(void) {
    have_prev = 0;
    reg_hash = 0;
    n_inst = 0;
    watch_have_prev = 0;
}

void single_step_prof_on(int on) {
    prof_on = on;
}

void single_step_prof_init(uint32_t pc_min, uint32_t pc_max) {
    demand((pc_min & 3) == 0 && (pc_max & 3) == 0, "pc bounds not aligned");
    demand(pc_max > pc_min, "bad pc range");

    prof_pc_min = pc_min;
    prof_pc_max = pc_max;
    prof_n = (pc_max - pc_min) / 4;

    prof_hist = (volatile unsigned *)kmalloc(prof_n * sizeof(*prof_hist));
    demand(prof_hist, "kmalloc failed");

    for(unsigned i = 0; i < prof_n; i++)
        prof_hist[i] = 0;
}

void single_step_prof_reset(void) {
    if(!prof_hist) return;
    for(unsigned i = 0; i < prof_n; i++)
        prof_hist[i] = 0;
}

static inline void prof_inc(uint32_t pc) {
    if(!prof_on || !prof_hist) return;
    if(pc < prof_pc_min || pc >= prof_pc_max) return;

    unsigned idx = (pc - prof_pc_min) / 4;
    prof_hist[idx]++;
}

void single_step_prof_dump(unsigned min_count) {
    if(!prof_hist) {
        trace("prof: not initialized\n");
        return;
    }

    trace("---- single-step profiler dump (min=%d) ----\n", min_count);
    for(unsigned i = 0; i < prof_n; i++) {
        unsigned c = prof_hist[i];
        if(c >= min_count) {
            uint32_t pc = prof_pc_min + 4*i;
            trace("pc=%x: count=%d\n", pc, c);
        }
    }
    trace("-------------------------------------------\n");
}

// memory watcher api
void single_step_watch_addr32(volatile uint32_t *addr) {
    watch_addr = addr;
    watch_have_prev = 0;
}

void single_step_watch_disable(void) {
    watch_addr = 0;
    watch_have_prev = 0;
}

// single-step (mismatch) breakpoint handler:
static void single_step_handler(regs_t *regs) {
    if(!brkpt_fault_p())
        panic("impossible: should get no other faults\n");
    assert(mode_get(regs->regs[REGS_CPSR]) == USER_MODE);

    // print: inst/fault pc/machine code at pc.
    uint32_t pc = regs->regs[15];

    // add memory watch check
    if(watch_addr) {
        uint32_t cur = *watch_addr;

        if(!watch_have_prev) {
            watch_prev = cur;
            watch_have_prev = 1;
        } else if(cur != watch_prev) {
            panic("WATCH: *%x changed %x -> %x at pc=%x (inst=%d)\n",
                (uint32_t)watch_addr, watch_prev, cur, pc, n_inst);
        }
    }

    prof_inc(pc);
    n_inst++;
    reg_hash = fast_hash_inc32(regs->regs, 17 * sizeof(regs->regs[0]), reg_hash);
    trace("fault:\t%X:\t%X  @ %d\n", pc, GET32(pc), n_inst);

    // print only changed regs vs last trap
    if(trace_regs_p) {
        if(!have_prev) {
            trace("  regs: (first snapshot)\n");
            have_prev = 1;
        } else {
            int any = 0;
            for(int i = 0; i < 17; i++) {
                uint32_t cur = regs->regs[i];
                uint32_t prev = prev_regs.regs[i];
                if(cur != prev) {
                    any = 1;

                    if(i == REGS_PC)
                        trace("    pc: %x -> %x\n", prev, cur);
                    else if(i == REGS_CPSR)
                        trace("    cpsr: %x -> %x\n", prev, cur);
                    else
                        trace("    r%d: %x -> %x\n", i, prev, cur);
                }
            }
            if(!any)
                trace("    (no reg changes)\n");
        }
        prev_regs = *regs;
    }

    // handle uart race condition
    while(!uart_can_put8())
        ;

    // run the faulting instruction, mismatch everything else.
    brkpt_mismatch_set(pc);
    
    // jump back to the code that faulted.
    switchto(regs);
}

// single system call
static int syscall_handler(regs_t *r) {
    // verify we came from user-level.
    assert(mode_get(r->regs[REGS_CPSR]) == USER_MODE);

    // system call number passed in <r0>, arguments in
    // <r1>,<r2>,<r3>
    uint32_t sysno = r->regs[0];
    uint32_t arg0 = r->regs[1];
    uint32_t pc = r->regs[15];
    switch(sysno) {
    case SS_SYS_EXIT:
        trace("%s: exited with syscall=%d: arg0=%d, pc=%x\n", 
            ss_cur_fn, sysno,arg0, pc);
        exit_regs = *r;   
        switchto(&scheduler_regs);
        not_reached();
    // example: print a character.
    case SS_SYS_PUTC:
        output("%c", r->regs[1]);
        break;
    default:
        panic("illegal system call number: %d\n", sysno);
    }
    switchto(r);
    not_reached();
}

/*****************************************************************
 * standard code to create the initial thread registers.
 */
// make a user-level <cpsr> based on the 
// current <cpsr> value
static inline uint32_t cpsr_to_user(void) {
    // inherit whatever configured with.
    uint32_t cpsr = cpsr_get();

    cpsr = bits_clr(cpsr, 0, 4) | USER_MODE;
    // clear carry flags (non-determ values)
    cpsr = bits_clr(cpsr, 28, 31);
    // enable interrupts (doesn't matter here).
    cpsr = bit_clr(cpsr, 7);

    assert(mode_get(cpsr) == USER_MODE);
    return cpsr;
}

// create a new register block: we allow for a null <stack>
// for ease of testing.
static inline regs_t 
regs_init(
    void (*fp)(), 
    uint32_t arg, 
    void *stack, 
    uint32_t nbytes) {

    assert(fp);
    uint32_t initial_pc = (uint32_t)fp; 
    uint32_t cpsr = cpsr_to_user();

    uint32_t sp = 0;
    if(stack) {
        demand(nbytes>4096, stack seems small);
        sp = (uint32_t)(stack+nbytes);
    }

    // better to pass it in, but we try to be simple.
    void exit_trampoline(void);
    uint32_t exit_tramp = (uint32_t)exit_trampoline;

    // <regs_t>: 17 uint32_t entries (see: <switchto.h>
    //  - 16 general purpose regs r0..r15 (r0 at r.regs[0], 
    //    r1 at r.regs[1], etc).
    //  - the cpsr at r.regs[16].
    return (regs_t) { 
        .regs[REGS_PC] = initial_pc,
        .regs[REGS_R0] = arg,
        .regs[REGS_SP] = sp,
        .regs[REGS_CPSR] = cpsr,
        // where to jump to if the code returns.
        .regs[REGS_LR] = (uint32_t)exit_tramp,
    };
}

void single_step_on(void) {
    static int init_p = 0;

    // one-time initialization.
    if(!init_p) {
        init_p = 1;
        full_except_install(0);
        full_except_set_prefetch(single_step_handler);
        full_except_set_syscall(syscall_handler);
    }
    brkpt_mismatch_start();

    // setup so we mismatch on any instruction address
    brkpt_mismatch_set(0);
}

void single_step_off(void) {
    brkpt_mismatch_stop();
}

// run function <fp> in single stepping mode.
regs_t single_step_run(
    const char *fn_name, 
    void (*fp)(), 
    uint32_t arg,
    void *stack,
    uint32_t nbytes) 
{
    regs_t regs = regs_init(fp, arg, stack, nbytes);
    ss_cur_fn = fn_name;

    trace("PRE: about to single step <%s>\n", fn_name);
    single_step_on();
    switchto_cswitch(&scheduler_regs, &regs);
    single_step_off();
    trace("POST: done single-stepping %s\n", fn_name);

    trace("\tn_inst=%d: non-zero regs= {\n", n_inst);
    for(int i = 0; i < 16; i++) {
        let v = exit_regs.regs[i];
        if(v)
            trace("\t\tr%d = %x\n", i, v);
    }
    trace("\t\tcpsr=%x\n", exit_regs.regs[REGS_CPSR]);
    trace("\t}\n");

    trace("\treg_hash=%x\n", reg_hash);
    return exit_regs;
}

void single_step_reset_count(void) { n_inst = 0; }
unsigned single_step_get_count(void) { return n_inst; }
