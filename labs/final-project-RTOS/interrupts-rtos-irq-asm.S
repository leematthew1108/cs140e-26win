#include "rpi-asm.h"

@ enable/disable interrupts.
.globl enable_interrupts  
enable_interrupts:
    mrs r0,cpsr         
    bic r0,r0,#(1<<7)	
    msr cpsr_c,r0		
    bx lr		        

.globl disable_interrupts
disable_interrupts:
    mrs r0,cpsr
    orr r0,r0,#(1<<7)
    msr cpsr_c,r0
    bx lr

@ the interrupt table that we copy to 0x0.
.globl _interrupt_table
.globl _interrupt_table_end
_interrupt_table:
  ldr pc, _reset_asm                   
  ldr pc, _undefined_instruction_asm
  ldr pc, _software_interrupt_asm      
  ldr pc, _prefetch_abort_asm
  ldr pc, _data_abort_asm
  ldr pc, _reset_asm
  ldr pc, _interrupt_asm
fast_interrupt_asm:
  sub   lr, lr, #4 
  push  {lr}
  push  {r0-r12}
  mov   r0, lr
  bl    fast_interrupt_vector
  pop   {r0-r12}
  ldm   sp!, {pc}^

_reset_asm:                   .word reset_asm
_undefined_instruction_asm:   .word undefined_instruction_asm
_software_interrupt_asm:      .word syscall_asm
_prefetch_abort_asm:          .word prefetch_abort_asm
_data_abort_asm:              .word data_abort_asm
_interrupt_asm:               .word interrupt_asm
_interrupt_table_end:

@ only handler that should run since we only enable general interrupts
.globl interrupt_asm
interrupt_asm:
  @ Full-context save of the interrupted thread.
  @ We assume tasks run in SYS_MODE (threads share user sp/lr),
  @ so we can use caret (^) to access the interrupted sp/lr.
  mov sp, #INT_STACK_ADDR

  @ Allocate regs_t[17] on the exception stack.
  sub sp, sp, #(17*4)             @ sp -> regs[0]

  @ Save r0-r12.
  stm sp, {r0-r12}                @ regs[0..12]

  @ Save interrupted sp/lr into regs[13]/regs[14].
  add r1, sp, #(13*4)            @ &regs[13]
  stm r1, {sp, lr}^              @ regs[13]=user/SYS sp, regs[14]=user/SYS lr

  @ Save interrupted PC (IRQ LR points to next inst).
  sub r2, lr, #4
  str r2, [sp, #(15*4)]          @ regs[15]=pc

  @ Save interrupted CPSR from SPSR_irq.
  mrs r1, spsr
  str r1, [sp, #(16*4)]          @ regs[16]=cpsr

  @ Call the C handler with regs pointer.
  mov r0, sp
  bl  timer_irq_handler

  @ timer_irq_handler must not return.
  asm_not_reached();

reset_asm:
    mov sp, #INT_STACK_ADDR   
    sub   r0, lr, #4
    bl    reset_vector

@ unexpected(...) macro used to build weak trampolines for other exceptions.
#define unexpected(fn, offset)      \
    mov sp, #INT_STACK_ADDR;        \
    sub   r0, lr, #(offset);        \
    bl    fn

undefined_instruction_asm:
    unexpected(undefined_instruction_vector,4)  
syscall_asm:
    @ Full-context save for a voluntary context switch via `swi`.
    @ For SWI, IRQ LR/SVC LR already points to the instruction after `swi`,
    @ so we store PC = lr without subtracting 4.
    mov sp, #INT_STACK_ADDR
    sub sp, sp, #(17*4)             @ sp -> regs[0]
    stm sp, {r0-r12}                @ regs[0..12]
    add r1, sp, #(13*4)             @ &regs[13]
    stm r1, {sp, lr}^              @ regs[13]=user/SYS sp, regs[14]=user/SYS lr
    str lr, [sp, #(15*4)]         @ regs[15]=pc (instruction after swi)
    mrs r1, spsr
    str r1, [sp, #(16*4)]          @ regs[16]=cpsr
    mov r0, sp
    bl  rtos_swi_handler
    asm_not_reached();
prefetch_abort_asm:
    unexpected(prefetch_abort_vector,4)
data_abort_asm:
    unexpected(data_abort_vector,4)

@ simple demonstration of how to switch modes to get the banked lr.
#define SUPER_MODE 0b10011
.globl get_super_lr
get_super_lr:
    mrs r1, cpsr
    msr cpsr_c, #SUPER_MODE
    mov r0, lr
    msr cpsr_c, r1
    bx lr

