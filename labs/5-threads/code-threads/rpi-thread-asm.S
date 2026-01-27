/*
 * implement these.  ordered in difficulty.
 */
#include "rpi-asm.h"

@ return the current stack pointer.
MK_FN(rpi_get_sp)
    // asm_todo("implement this")
    mov r0, sp
    bx lr

@ void rpi_cswitch(uint32_t **old_sp_save, const uint32_t *new_sp);
@   - called to context switch from currently running thread
@     (so must save registers) to next thread (so must load
@     registers).  eg:
@        rpi_cswitch(&cur_th->saved_sp, next_th->saved_sp);
@
@ what to do:
@    1. store all callee + <lr> onto stack.
@    2. store the stack pointer reg into <&cur_th->saved_sp>
@       this causes the most confusion: we have to do this 
@       b/c no one but this code knows where the registers
@       are saved on the stack, so must store this address so 
@       they can be restored later.    single most common
@       confusion, so think about it.
@    3. move <next_th->saved_sp> into the stack pointer reg
@    4. load all the callee + <lr> using the sp.
@    5. return!
MK_FN(rpi_cswitch)
    // asm_todo("implement this")

    @ save/store all callee + lr register onto stack. 
    @ stack pointer gets updated after push
    push {r4, r5, r6, r7, r8, r9, r10, r11, lr}

    @ store the current stack pointer to the address in r0
    @ r0 stores the first function argument which was **old_sp_save
    @ this is a pointer to the pointer that holds the old_sp
    str sp, [r0]

    @ move next thread's saved sp into stack pointer reg
    mov sp, r1

    @ load saved regs for next thread using updated sp
    pop {r4, r5, r6, r7, r8, r9, r10, r11, lr}

    bx lr

@ [Make sure you can answer: why do we need this
@ weird little routine?]
@
@ use this to setup each thread for the first time.
@ setup the stack so that when cswitch runs it will:
@	- load address of <rpi_init_trampoline> into LR
@	- <code> into r1, 
@	- <arg> into r0
@ 
MK_FN(rpi_init_trampoline)
    // asm_todo("implement this")
    mov r0, r4
    blx r5
    bl rpi_exit 
