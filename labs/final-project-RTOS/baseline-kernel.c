// Baseline build: same periodic workload and mutex,
// but disable true preemption inside the timer IRQ.
#define NONPREEMPTIVE 1
#include "rtos-kernel.c"

