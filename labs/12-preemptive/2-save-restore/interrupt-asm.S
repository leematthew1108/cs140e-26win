#include "rpi-asm.h"

@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ part 1: used by <2-simple-save-test.c>
@

@ terminal syscall: print the registers we have.
syscall_reg_save:
    @ asm_todo("save all 17 registers onto stack: pass pointer to syscall_handler");
    @ 1) switch to a known-good exception stack (from rpi-constants.h)
    mov sp, #INT_STACK_ADDR

    @ 2) allocate space for 17 uint32_t regs on the stack
    sub sp, sp, #(17*4)            @ sp -> regs[0]

    @ 3) save r0-r12 (these are shared across modes, still contain user values)
    stm sp, {r0-r12}             @ regs[0..12] = r0..r12

    @ 4) save USER sp/lr into regs[13], regs[14] using caret (^)
    add r1, sp, #(13*4)            @ computes address of saved_reg[13] into r1
    stm r1, {sp, lr}^              @ regs[13]=user_sp, regs[14]=user_lr

    @ 5) save pc (return address after swi)
    str lr, [sp, #(15*4)]          @ regs[15] = lr_svc (points to regs_init_ident_swi_pc)

    @ 6) save cpsr from user (in spsr)
    mrs r1, spsr
    str r1, [sp, #(16*4)]          @ regs[16] = spsr

    @ 7) call C handler: int syscall_handler(uint32_t regs[17])
    mov r0, sp                     @ arg0 = &regs[0]
    bl syscall_handler

    asm_not_reached();

@ use rfe to load <pc> and <cpsr> from the 2 entry
@ array passed in <r0>.
MK_FN(rfe_asm)
    // asm_todo("pull this from part 1");
    mov sp, r0
    rfe sp
    asm_not_reached();

@ for test 1.
.align 5; 
.globl syscall_reg_save_vec
syscall_reg_save_vec:
    b unhandled_reset
    b unhandled_undefined_instruction
    b syscall_reg_save
    b unhandled_prefetch_abort
    b unhandled_data_abort
    b unhandled_interrupt
    asm_not_reached();

@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ used by the rest of tests.

@ don't forget to fix lr!
prefetch_reg_save:
    // asm_todo("save 17 registers onto stack: pass pointer to prefetc_abort_handler");
    @ 1) switch to the known-good exception stack
    mov sp, #INT_STACK_ADDR

    @ 2) allocate space for 17 uint32_t regs on the stack
    sub sp, sp, #(17*4)            

    @ 3) save shared registers r0-r12 
    stm sp, {r0-r12}             

    @ 4) save USER sp/lr into regs[13], regs[14] using the caret (^)
    add r1, sp, #(13*4)            
    stm r1, {sp, lr}^              

    @ 5) save the PC. 
    @ Because this is a Prefetch Abort, the CPU sets lr to PC + 4.
    @ We must subtract 4 to get the actual faulting instruction address.
    sub r2, lr, #4                 
    str r2, [sp, #(15*4)]          @ regs[15] = lr - 4

    @ 6) save the CPSR that the user was running with (stored in our spsr)
    mrs r1, spsr
    str r1, [sp, #(16*4)]          @ regs[16] = spsr

    @ 7) Call the C handler: void prefetch_abort_handler(uint32_t regs[17])
    mov r0, sp                     @ arg0 = &regs[0]
    bl prefetch_abort_handler
    
    asm_not_reached();

.align 5; 
.globl simple_reg_save_restore_vec
simple_reg_save_restore_vec:
    b unhandled_reset
    b unhandled_undefined_instruction
    b unhandled_swi
    b prefetch_reg_save
    b unhandled_data_abort
    b unhandled_interrupt
    asm_not_reached();

@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ weak symbols used not all tests define all handlers.

@ so we can compile if they don't provide
.weak syscall_handler
MK_FN(syscall_handler)
    asm_not_reached();

@ so we can compile if they don't provide
.weak prefetch_abort_handler
MK_FN(prefetch_abort_handler)
    asm_not_reached();
