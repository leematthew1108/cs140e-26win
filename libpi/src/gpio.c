/*
 * Implement the following routines to set GPIO pins to input or 
 * output, and to read (input) and write (output) them.
 *  1. DO NOT USE loads and stores directly: only use GET32 and 
 *    PUT32 to read and write memory.  See <start.S> for thier
 *    definitions.
 *  2. DO USE the minimum number of such calls.
 * (Both of these matter for the next lab.)
 *
 * See <rpi.h> in this directory for the definitions.
 *  - we use <gpio_panic> to try to catch errors.  For lab 2
 *    it only infinite loops since we don't have <printk>
 */
#include "rpi.h"
#include "gpio.h"

// See broadcomm documents for magic addresses and magic values.
//
// If you pass addresses as:
//  - pointers use put32/get32.
//  - integers: use PUT32/GET32.
//  semantics are the same.
enum {
    // Max gpio pin number.
    GPIO_MAX_PIN = 53,

    GPIO_BASE = 0x20200000,
    gpio_set0  = (GPIO_BASE + 0x1C),
    gpio_clr0  = (GPIO_BASE + 0x28),
    gpio_lev0  = (GPIO_BASE + 0x34), 

    // <you will need other values from BCM2835!>
    gpio_sel0 = (GPIO_BASE),
    three_bit_mask = 0x00000007
};

// New for lab 3
// set GPIO function for <pin> (input, output, alt...).  settings for other
// pins should be unchanged.
void gpio_set_function(unsigned pin, gpio_func_t func) {
    if(pin > GPIO_MAX_PIN)
            gpio_panic("illegal pin=%d\n", pin);
    if((func & 0b111) != func)
        gpio_panic("illegal func=%x\n", func);

    unsigned address = gpio_sel0 + (pin/10)*4;
    unsigned value = GET32(address);
    unsigned shift = pin % 10;
    value &= ~(three_bit_mask << (3*shift));
    value |= func << (3*shift);
    PUT32(address, value);
}

//
// Part 1 implement gpio_set_on, gpio_set_off, gpio_set_output
//

// set <pin> to be an output pin.
//
// NOTE: fsel0, fsel1, fsel2 are contiguous in memory, so you
// can (and should) use ptr calculations versus if-statements!
void gpio_set_output(unsigned pin) {
    gpio_set_function(pin, GPIO_FUNC_OUTPUT);
}

// Set GPIO <pin> = on.
// GPIO Set 0 sets pins 0-31
// GPIO Set 1 sets pins 32-53
void gpio_set_on(unsigned pin) {
    if(pin > GPIO_MAX_PIN)
        gpio_panic("illegal pin=%d\n", pin);

    // Implement this. 
    // NOTE: 
    //  - If you want to be slick, you can exploit the fact that 
    //    SET0/SET1 are contiguous in memory.
    PUT32(gpio_set0 + (pin/32)*4, (1 << pin%32));
}

// Set GPIO <pin> = off
void gpio_set_off(unsigned pin) {
    if(pin > GPIO_MAX_PIN)
        gpio_panic("illegal pin=%d\n", pin);

    // Implement this. 
    // NOTE: 
    //  - If you want to be slick, you can exploit the fact that 
    //    CLR0/CLR1 are contiguous in memory.
    PUT32(gpio_clr0 + (pin/32)*4, (1 << pin%32));
}

// Set <pin> to <v> (v \in {0,1})
void gpio_write(unsigned pin, unsigned v) {
    if(v)
        gpio_set_on(pin);
    else
        gpio_set_off(pin);
}

//
// Part 2: implement gpio_set_input and gpio_read
//

// set <pin> = input.
void gpio_set_input(unsigned pin) {
    gpio_set_function(pin, GPIO_FUNC_INPUT);
}

// Return 1 if <pin> is on, 0 if not.
int gpio_read(unsigned pin) {
    unsigned v = 0;

    if(pin > GPIO_MAX_PIN)
        gpio_panic("illegal pin=%d\n", pin); 


    // read pin
    v = GET32(gpio_lev0 + pin/32*4);
    unsigned mask = 1 << pin%32;
    return (v & mask) != 0;
}
