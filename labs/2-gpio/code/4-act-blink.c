// need to blink ACT led same as how 1-blink.c blinks LED from GPIO 20
// The rpi has an on-board LED controlled by GPIO pin 47. 
// It is "active low" in that when you set pin(47)=1, it turns off, when pin(47)=0, it turns on. 

// part 4: uses your GPIO code to blink a single LED connected to 
// pin 47.
//   - when run should keep blinking.
//   - to restart: you have to power-cycle the pi.
#include "rpi.h"

void notmain(void) {
    enum { led = 47 };

    gpio_set_output(led);
    while(1) {
        gpio_set_on(led);
        delay_cycles(1000000);
        gpio_set_off(led);
        delay_cycles(1000000);
    }
}
