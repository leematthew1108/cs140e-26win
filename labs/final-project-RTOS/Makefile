PROGS := rtos-kernel.c baseline-kernel.c
COMMON_SRC := interrupts-rtos-irq-asm.S
# Using my lab-12 context switching source.
COMMON_SRC += $(CS140E_2026_PATH)/labs/12-preemptive/2-save-restore/switchto-asm.S
OUR_START := staff-start.S
MEMMAP := ./memmap

RUN = 1

TTYUSB =
BOOTLOADER = pi-install

include $(CS140E_2026_PATH)/libpi/mk/Makefile.robust-v2

