#include "rpi.h"
#include "mbox.h"

uint32_t rpi_temp_get(void) ;

#include "cycle-count.h"

// compute cycles per second using
//  - cycle_cnt_read();
//  - timer_get_usec();
unsigned cyc_per_sec(void) {
    // todo("implement this!\n");

    unsigned cyc_start = cycle_cnt_read();

    unsigned s = timer_get_usec();
    while((timer_get_usec() - s) < 1000*1000)
        ;
    unsigned cyc_end = cycle_cnt_read();

    unsigned tot = cyc_end - cyc_start;
    return tot;
}

uint32_t measure_10k_put32(volatile uint32_t *ptr) {
    uint32_t s = cycle_cnt_read();
    
    // Loop 10,000 times
    for (int i = 0; i < 10000; i++) {
        put32(ptr, 0);
        asm volatile( ".align 5" ); 
    }
    
    uint32_t e = cycle_cnt_read();
    return e - s; // Returns total cycles for 10k iterations
}

uint32_t measure_10k_get32(volatile uint32_t *ptr) {
    uint32_t s = cycle_cnt_read();
    
    // Loop 10,000 times
    for (int i = 0; i < 10000; i++) {
        get32(ptr);
        asm volatile( ".align 5" ); 
    }
    
    uint32_t e = cycle_cnt_read();
    return e - s; // Returns total cycles for 10k iterations
}



void notmain(void) { 
    printk("------------------------------------------------------       \n");
    output("mailbox serial number = %llx\n", rpi_get_serialnum());
    //todo("implement the rest");

    output("mailbox revision number = %x\n", rpi_get_revision());
    output("mailbox model number = %x\n", rpi_get_model());

    uint32_t size = rpi_get_memsize();
    output("mailbox physical mem: size=%d (%dMB)\n", 
            size, 
            size/(1024*1024));

    // print as fahrenheit
    unsigned x = rpi_temp_get();

    // convert <x> to C and F
    unsigned C = 0, F = 0;
    C = x/1000;
    F = C * 1.8 + 32;
    output("mailbox temp = %x, C=%d F=%d\n", x, C, F); 

    printk("------------------------------------------------------       \n");

    //todo("do overclocking!\n");
    output("Current clock rate for the ARM = %d (MHz)\n", (int)rpi_clock_curhz_get(MBOX_CLK_CPU) / 1000000);
    output("Max clock rate (config) for the ARM = %d (MHz)\n", (int)rpi_clock_maxhz_get(MBOX_CLK_CPU)/ 1000000);
    output("Measured (real) clock rate for the ARM = %d (MHz)\n", (int)rpi_clock_realhz_get(MBOX_CLK_CPU)/ 1000000);

    printk("------------------------------------------------------       \n");
    
    output("Current clock rate for the CORE = %d (MHz)\n", (int)rpi_clock_curhz_get(MBOX_CLK_CORE)/ 1000000);
    output("Max clock rate (config) for the CORE = %d (MHz) \n", (int)rpi_clock_maxhz_get(MBOX_CLK_CORE)/ 1000000);
    output("Measured (real) clock rate for the CORE = %d (MHz)\n", (int)rpi_clock_realhz_get(MBOX_CLK_CORE)/ 1000000);

    printk("------------------------------------------------------       \n");

    uint32_t y;
    output("(Current, before over-clocking) measured cycles per second: %d (M-cycles)\n", cyc_per_sec()/1000000);
    unsigned put = measure_10k_put32(&y);
    output("(Current, before over-clocking) mem write 10000 times: %d cycles (%d cycles/op)\n", put, put/10000);
    unsigned get = measure_10k_get32(&y);
    output("(Current, before over-clocking) mem read 10000 times: %d cycles (%d cycles/op)\n", get, get/10000);

    printk("------------------------------------------------------       \n");
    output("will overclock core from 250 MHz to 600 MHz. Need to reconfigure uart before.");
    unsigned baudrate_reg = 600 * 1000000 / (115200 * 8) - 1;
    uart_init_div(baudrate_reg);

    output("\nSetting CORE clock rate to: %d\n", (int)rpi_clock_hz_set(MBOX_CLK_CORE, 600*1000000)/1000000);
    output("Setting ARM clock rate to: %d\n", (int)rpi_clock_hz_set(MBOX_CLK_CPU, 1150*1000000)/1000000);
    output("Setting sdram_freq to: %d\n", (int)rpi_clock_hz_set(MBOX_CLK_SDRAM, 600*1000000)/1000000);

    printk("------------------------------------------------------       \n");
    output("(After over-clocking) measured cycles per second: %d (M-cycles)\n", cyc_per_sec()/1000000);
    put = measure_10k_put32(&y);
    output("(After over-clocking) mem write 10000 times: %d cycles (%d cycles/op)\n", put, put/10000);
    get = measure_10k_get32(&y);
    output("(After over-clocking) mem read 10000 times: %d cycles (%d cycles/op)\n", get, get/10000);
}
