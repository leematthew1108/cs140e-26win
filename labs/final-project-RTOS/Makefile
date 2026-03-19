# Simple initial build: sanity-check timer IRQs in this project.
#
# Next iteration we will replace the IRQ assembly with a full-context
# save so the timer can preempt threads.

PROGS := rtos-kernel.c baseline-kernel.c
COMMON_SRC := interrupts-rtos-irq-asm.S
# Use your lab-12 context switching source (no staff disassembly).
COMMON_SRC += $(CS140E_2026_PATH)/labs/12-preemptive/2-save-restore/switchto-asm.S
OUR_START := staff-start.S
MEMMAP := ./memmap

RUN = 1

TTYUSB =
BOOTLOADER = pi-install

include $(CS140E_2026_PATH)/libpi/mk/Makefile.robust-v2

