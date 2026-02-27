#include "rpi-asm.h"
    
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ get USER banked regs by switching to SYSTEM mode and back.
@ if we switched to USER, we would need to use a system call to 
@ get back.

@ get the USER sp by switching to SYS_MODE 
@ and back.
MK_FN(cps_user_sp_get)
    cps #SYS_MODE
    prefetch_flush(r1);

    @ USER And SYSTEM share the same sp/lr/cpsr
    mov r0, sp

    cps #SUPER_MODE
    prefetch_flush(r1);

    bx lr

@ set the USER sp
@   - as above: use <cpsr> to switch to SYSTEM (SYS_MODE) and back.
MK_FN(cps_user_sp_set)
    // asm_todo(implement this code)
    cps #SYS_MODE
    prefetch_flush(r1);

    mov sp, r0

    cps #SUPER_MODE
    prefetch_flush(r1);

    bx lr

@ get the USER lr 
@   - as above: use <cpsr> to switch to SYSTEM (SYS_MODE) and back.
MK_FN(cps_user_lr_get)
    // asm_todo(implement this code)
    cps #SYS_MODE
    prefetch_flush(r1);

    mov r0, lr

    cps #SUPER_MODE
    prefetch_flush(r1);

    bx lr

@ set USER lr 
@   - as above: use <cpsr> -> SYSTEM -> back.
MK_FN(cps_user_lr_set)
    // asm_todo(implement this code)
    cps #SYS_MODE
    prefetch_flush(r1);

    mov lr, r0

    cps #SUPER_MODE
    prefetch_flush(r1);

    bx lr

@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ get USER banked regs by using ldm/stm instructions with the ^
@ modifier.

@ address to a uint32_t word is passed in r0
MK_FN(mem_user_sp_get)
    @ dereference the address held in <r0>
    @ and write the 32-bit result into user
    @ mode <sp> register.
    stm r0, {sp}^
    bx lr

@ address to a uint32_t word is passed in r0
MK_FN(mem_user_sp_set)
    // asm_todo("implement this using ldm + caret operator")
    ldm r0, {sp}^
    bx lr

@ address to a uint32_t word is passed in r0
MK_FN(mem_user_lr_get)
    // asm_todo("implement this code");
    stm r0, {lr}^
    bx lr

@ address to a uint32_t word is passed in r0
MK_FN(mem_user_lr_set)
    // asm_todo("implement this code")
    ldm r0, {lr}^
    bx lr

@ address to an array with two uint32_t words 
@ is passed in r0
MK_FN(mem_user_sp_lr_get)
    // asm_todo("implement this")
    stm r0, {sp, lr}^
    bx lr

@ address to an array with two uint32_t words 
@ is passed in r0
MK_FN(mem_user_sp_lr_set)
    // asm_todo("implement this")
    ldm r0, {sp, lr}^
    bx lr

@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ switch to a given privileged mode to get banked sp,lr
@
@ void mode_get_lr_sp_asm(mode, &sp, &lr)
@    - mode in r0
@    - address to hold sp in r1
@    - address to hold lr in r2
MK_FN(mode_get_lr_sp_asm)
    // asm_todo("implement this")
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

@ MK_FN(mode_set_lr_sp_asm)
@     asm_todo("implement this")
@     bx lr
