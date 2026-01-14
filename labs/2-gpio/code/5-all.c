#include "rpi.h"

void notmain(void) {
    // the different pins.
    enum { 
        led0    = 20,
        led1    = 27,
        led2    = 47,
    };

    // configure.  can be in any order, just do before
    // use.
    gpio_set_output(led0);
    gpio_set_output(led1);
    gpio_set_output(led2);

    while(1) {
        gpio_set_on(led0);
        gpio_set_on(led1);
        gpio_set_off(led2);
        delay_cycles(1000000);
        gpio_set_off(led0);
        gpio_set_off(led1);
        gpio_set_on(led2);
        delay_cycles(1000000);
    }
}
