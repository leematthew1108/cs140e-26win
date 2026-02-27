#include "rpi-asm.h"

MK_FN(switchto_user_asm)
    // b staff_switchto_user_asm

    // setup sp to use as pointer to pc & cpsr for rfe
    // exception stack gets reset each time so its ok to use
    // and exception stack is banked
    // add sp, r0, #(15*4)
    mov sp, r0

    // advance the r0 to point to regs[1]
    // add r0, r0, #4

    // load user r1-r14
    // (note problem with loading multiple if r0 is in the load list)
    ldm sp, {r0-r14}^

    // restore user r0
    // sub r0, r0, #4
    // ldr r0, [r0]

    // use rfe to load PC and CPSR simultaneously 
    add sp, sp, #(15*4)
    rfe sp

@ NOTE:  set <cpsr> using the following instructon so GNU
@ assembler will set all the bits.  get mismatches otherwise
@   msr   cpsr_cxsf, <reg>
MK_FN(switchto_priv_asm)
    // b staff_switchto_priv_asm
    // get the target CPSR from regs[16]
    ldr r1, [r0, #(16*4)] 

    // switch directly to target mode
    msr cpsr_cxsf, r1
    prefetch_flush(r2)

    // load all registers
    ldm r0, {r0-r15}

    asm_not_reached();

@ void priv_get_sp_lr_asm(uint32_t mode, uint32_t *sp, uint32_t *lr);
@   - assumes caller is at privileged mode [we could check this]
@   - mode in r0
@   - sp pointer in r1
@   - lr pointer in r2
@
MK_FN(priv_get_sp_lr_asm)
    // b staff_priv_get_sp_lr_asm
    // from my mode_get_lr_sp_asm from part 1 (copied here):
    mrs r3, cpsr              @ r3 = old cpsr

    and r0, r0, #0x1f         @ r0 = mode & 0x1f
    bic r12, r3, #0x1f        @ r12 = old cpsr with mode bits cleared
    orr r12, r12, r0          @ r12 = old cpsr but with new mode bits

    msr cpsr_c, r12           @ switch to target mode (preserve I/F/T)
    prefetch_flush(r12)

    str sp, [r1]              @ read banked sp of target mode
    str lr, [r2]              @ read banked lr of target mode

    msr cpsr_c, r3            @ restore original control field (mode + I/F/T)
    prefetch_flush(r12)

    bx lr
    asm_not_reached();

@ you'll have to write this, but it should mirror your other save
@ routines.
@
@ void cswitchto_priv_asm(regs_t *old, regs_t *next);
@
@ voluntary context switch: save the current registers in <old>
@ and call the right switchto on <next>
MK_FN(cswitchto_priv_asm)
    @ initially just forward to ours.
    // b staff_cswitchto_priv_asm

    @ to implement:
    @ 1. save all the callee regs (just like in your threads library)
    @    into the 17-entry register array we have been using.  note
    @    these have to be at the right offsets.
    @    e.g.:
    @       str r4,  [r0, #(4*4)]  @ r4
    @       str r5,  [r0, #(5*4)]  @ r5
    @       ... etc ...
    @   don't forget the cpsr!

    // save callee regs
    add r2, r0, #(4*4)
    stm r2, {r4-r11}

    // save sp and lr
    str sp, [r0, #(13*4)] // sp is r13
    str lr, [r0, #(14*4)] // lr is r14

    // save lr as the PC
    str lr, [r0, #(15*4)] // r15 is the PC

    // save current cpsr
    mrs r2, cpsr
    str r2, [r0, #(16*4)] // cpsr is regs[16]

    mov r0, r1 // next pointer is in r1 (passed in), move it to r0 to prepare to call switchto_priv_asm

    @ 2. just call the right switch routine: note, we should
    @    *not* reach this location when switching back!
    bl switchto_priv_asm
    asm_not_reached();

@ you'll have to write this, but it should mirror your other save
@ routines.
@
@ void cswitchto_user_asm(regs_t *old, regs_t *next);
@
@ voluntary context switch: save the current registers in <old>
@ and call the right switchto on <next>
MK_FN(cswitchto_user_asm)
    @ initially just forward to ours.
    // b staff_cswitchto_user_asm

    // asm_todo("save the registers: identical to <switchto_priv_asm>")
    // save callee regs
    add r2, r0, #(4*4)
    stm r2, {r4-r11}

    // save sp and lr
    str sp, [r0, #(13*4)] // sp is r13
    str lr, [r0, #(14*4)] // lr is r14

    // save lr as the PC
    str lr, [r0, #(15*4)] // r15 is the PC

    // save current cpsr
    mrs r2, cpsr
    str r2, [r0, #(16*4)] // cpsr is regs[16]

    mov r0, r1 // next pointer was in r1, move it to r0 to prepare to call switchto_priv_asm

    bl switchto_user_asm
    asm_not_reached();
