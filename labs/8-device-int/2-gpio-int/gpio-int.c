// engler, cs140 put your gpio-int implementations in here.
#include "rpi.h"

// in libpi/include: has useful enums.
#include "rpi-interrupts.h"

enum {
    GPIO_BASE = 0x20200000,
    GPEDS0 = GPIO_BASE + 0x40,
    GPREN0 = GPIO_BASE + 0x4C,
    GPFEN0 = GPIO_BASE + 0x58,
};

// returns 1 if there is currently a GPIO_INT0 interrupt, 
// 0 otherwise.
//
// note: we can only get interrupts for <GPIO_INT0> since the
// (the other pins are inaccessible for external devices).
int gpio_has_interrupt(void) {
    // todo("implement: is there a GPIO_INT0 interrupt?\n");
    // BCM page 115
    // interrupt 49 is gpio_int[0]
    // IRQ pending 2 holds all interrupts 32 to 63
    // so interrupt 49 is the 18th bit, so shift right 17 to get it in the LSB spot
    dev_barrier();
    int out = (GET32(IRQ_pending_2) >> 17) & 1;
    dev_barrier();
    return out != 0; 
}

// p97 set to detect rising edge (0->1) on <pin>.
// as the broadcom doc states, it  detects by sampling based on the clock.
// it looks for "011" (low, hi, hi) to suppress noise.  i.e., its triggered only
// *after* a 1 reading has been sampled twice, so there will be delay.
// if you want lower latency, you should us async rising edge (p99)
//
// also have to enable GPIO interrupts at all in <IRQ_Enable_2>
void gpio_int_rising_edge(unsigned pin) {
    if(pin>=32)
        panic("Pin >= 32 is invalid");
    // todo("implement: detect rising edge\n");
    dev_barrier();
    OR32(GPREN0, 1 << pin);
    dev_barrier();
    // interrupt 49 is gpio_int[0], 49-32 = 17
    PUT32(IRQ_Enable_2, 1 << 17);
    dev_barrier();
}

// p98: detect falling edge (1->0).  sampled using the system clock.  
// similarly to rising edge detection, it suppresses noise by looking for
// "100" --- i.e., is triggered after two readings of "0" and so the 
// interrupt is delayed two clock cycles.   if you want  lower latency,
// you should use async falling edge. (p99)
//
// also have to enable GPIO interrupts at all in <IRQ_Enable_2>
void gpio_int_falling_edge(unsigned pin) {
    if(pin>=32)
        panic("Pin >= 32 is invalid");
    // todo("implement: detect falling edge\n");
    dev_barrier();
    OR32(GPFEN0, 1 << pin);
    dev_barrier();
    // interrupt 49 is gpio_int[0], 49-32 = 17
    PUT32(IRQ_Enable_2, 1 << 17);
    dev_barrier();
}

// p96: a 1<<pin is set in EVENT_DETECT if <pin> triggered an interrupt.
// if you configure multiple events to lead to interrupts, you will have to 
// read the pin to determine which caused it.
int gpio_event_detected(unsigned pin) {
    if(pin>=32)
        panic("Pin >= 32 is invalid");
    // todo("implement: is an event detected?\n");
    dev_barrier();
    int out = GET32(GPEDS0) & (1 << pin);
    dev_barrier();
    return out != 0;
}

// p96: have to write a 1 to the pin to clear the event.
void gpio_event_clear(unsigned pin) {
    if(pin>=32)
        panic("Pin >= 32 is invalid");
    // todo("implement: clear event on <pin>\n");
    dev_barrier();
    PUT32(GPEDS0, 1u << pin);  
    dev_barrier();
}
