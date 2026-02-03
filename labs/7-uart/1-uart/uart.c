// simple mini-uart driver: implement every routine 
// with a <todo>.
//
// NOTE: 
//  - from broadcom: if you are writing to different 
//    devices you MUST use a dev_barrier().   
//  - its not always clear when X and Y are different
//    devices.
//  - pay attenton for errata!   there are some serious
//    ones here.  if you have a week free you'd learn 
//    alot figuring out what these are (esp hard given
//    the lack of printing) but you'd learn alot, and
//    definitely have new-found respect to the pioneers
//    that worked out the bcm eratta.
//
// historically a problem with writing UART code for
// this class (and for human history) is that when 
// things go wrong you can't print since doing so uses
// uart.  thus, debugging is very old school circa
// 1950s, which modern brains arne't built for out of
// the box.   you have two options:
//  1. think hard.  we recommend this.
//  2. use the included bit-banging sw uart routine
//     to print.   this makes things much easier.
//     but if you do make sure you delete it at the 
//     end, otherwise your GPIO will be in a bad state.
//
// in either case, in the next part of the lab you'll
// implement bit-banged UART yourself.
#include "rpi.h"

// change "1" to "0" if you want to comment out
// the entire block.
#if 1
//*****************************************************
// We provide a bit-banged version of UART for debugging
// your UART code.  delete when done!
//
// NOTE: if you call <emergency_printk>, it takes 
// over the UART GPIO pins (14,15). Thus, your UART 
// GPIO initialization will get destroyed.  Do not 
// forget!   

// header in <libpi/include/sw-uart.h>
#include "sw-uart.h"
static sw_uart_t sw_uart;

// a sw-uart putc implementation.
static int sw_uart_putc(int chr) {
    sw_uart_put8(&sw_uart,chr);
    return chr;
}

// call this routine to print stuff. 
//
// note the function pointer hack: after you call it 
// once can call the regular printk etc.
__attribute__((noreturn)) 
static void emergency_printk(const char *fmt, ...)  {
    // we forcibly initialize in case the 
    // GPIO got reset. this will setup 
    // gpio 14,15 for sw-uart.
    sw_uart = sw_uart_default();

    // all libpi output is via a <putc>
    // function pointer: this installs ours
    // instead of the default
    rpi_putchar_set(sw_uart_putc);

    // do print
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);

    // at this point UART is all messed up b/c we took it over
    // so just reboot.   we've set the putchar so this will work
    clean_reboot();
}

#undef todo
#define todo(msg) do {                          \
    emergency_printk("%s:%d:%s\nDONE!!!\n",     \
            __FUNCTION__,__LINE__,msg);         \
} while(0)

// END of the bit bang code.
#endif

// from BCM2835 aux datasheet
enum {
    AUX_IRQ = 0x20215000, // base
    AUX_ENB = AUX_IRQ + 4, // turn uart on/off
    // gap
    AUX_MU_IO = AUX_IRQ + 0x40, // read/write from uart fifos
    AUX_MU_IER = AUX_IRQ + 0x44, // enable interrupts
    AUX_MU_IIR = AUX_IRQ + 0x48, // shows interrupt status. Clear tx/rx fifos
    AUX_MU_LCR = AUX_IRQ + 0x4C, // set uart to 7 bit or 8 bit mode (0b11 for 8 bit)
    AUX_MU_MCR = AUX_IRQ + 0x50, // controls modem signal? don't need this.. (based on annotation)
    AUX_MU_CNTL = AUX_IRQ + 0x60, // turn tx/rx on/off
    AUX_MU_STAT = AUX_IRQ + 0x64, // also tells space available
    AUX_MU_BAUD = AUX_IRQ + 0x68, // set the baud rate
};

#define BIT(n) (1u << (n)) // helpful for bitmasking

//*****************************************************
// the rest you should implement.

// called first to setup uart to 8n1 115200  baud,
// no interrupts.
//  - you will need memory barriers, use <dev_barrier()>
//
//  later: should add an init that takes a baud rate.
void uart_init(void) {
    // todo("start here\n");

    // NOTE: for cross-checking: make sure write UART 
    // addresses in order

    dev_barrier();
    // first, GPIO pins should be set up before enabling the UART
    // pins 14 and 15, alt fun 5 (page 102-104)
    gpio_set_function(14, GPIO_FUNC_ALT5);
    gpio_set_function(15, GPIO_FUNC_ALT5);

    dev_barrier();

    // turn on the UART in AUX. (read-modify-write to not kill the SPI) (page 9)
    unsigned auxenb = GET32(AUX_ENB) | 1; // set bit 0 
    PUT32(AUX_ENB, auxenb);

    // gap between memory addresses (could be some other device in between)
    dev_barrier();

    // Immediately disable tx/rx (you don't want to send garbage) (page 17)
    PUT32(AUX_MU_CNTL, 0);

    // afterwards, do reads/writes in increasing memory address order to match staff checksum

    // Disable interrupts. (page 12)
    PUT32(AUX_MU_IER, 0);
 
    // Find and clear all parts of its state (e.g., FIFO queues) 
    // since we are not absolutely positive they do not hold garbage.
    // AUX_MU_IIR_REG, page 13
    PUT32(AUX_MU_IIR, (1 << 1) | (1 << 2)); // set bit 1 and bit 2 to clear rx and tx fifos

    // set UART to 8-bit mode
    // erata in datasheet: data size is a two bit field
    // write 0b11 for 8bit
    PUT32(AUX_MU_LCR, 3);

    // from staff trace, set AUX_MU_MCR to 0? not sure why but doing it to match trace
    PUT32(AUX_MU_MCR, 0);

    // Configure baud rate: 115200 baud (page 19)
    unsigned valueToSet = 270; // baudrate calculated from page 11
    PUT32(AUX_MU_BAUD,  valueToSet & 0xFFFF);
    
    // now, enable tx/rx (page 17)
    PUT32(AUX_MU_CNTL, 3);

    dev_barrier();
}

// disable the uart: make sure all bytes have been
// 
void uart_disable(void) {
    // TODO: implement this!
    dev_barrier();
    uart_flush_tx();
    // turn off (disable) the UART in AUX. (read-modify-write to not kill the SPI) (page 9)
    unsigned auxenb = GET32(AUX_ENB) & (0xFFFFFFFE); // set bit 0 to 0
    PUT32(AUX_ENB, auxenb);
    dev_barrier();
}

// returns one byte from the RX (input) hardware
// FIFO.  if FIFO is empty, blocks until there is 
// at least one byte.
int uart_get8(void) {
    // todo("implement this\n");
    while(1) {
        dev_barrier();
        if (GET32(AUX_MU_STAT) & BIT(0)) {
            break;
        }
    }
    unsigned v = GET32(AUX_MU_IO) & 0xFF;
    dev_barrier();
    return (int)v;
}

// returns 1 if the hardware TX (output) FIFO has room
// for at least one byte.  returns 0 otherwise.
int uart_can_put8(void) {
    // TODO: implement this!
    int can_put8 = GET32(AUX_MU_STAT) & BIT(1);
    return can_put8 != 0;
}

// put one byte on the TX FIFO, if necessary, waits
// until the FIFO has space.
// 2071394643 228 0-uart-putc.out
int uart_put8(uint8_t c) {
    // TODO: implement this!
    dev_barrier();
    while (uart_can_put8() != 1) {} // wait until tx fifo has space
    PUT32(AUX_MU_IO, c);
    dev_barrier();
    return 1;
}

// returns:
//  - 1 if at least one byte on the hardware RX FIFO.
//  - 0 otherwise
int uart_has_data(void) {
    // TODO: implement this!
    dev_barrier();
    unsigned rx_fifo = GET32(AUX_MU_STAT) & BIT(0);
    dev_barrier();
    return rx_fifo;
}

// returns:
//  -1 if no data on the RX FIFO.
//  otherwise reads a byte and returns it.
int uart_get8_async(void) { 
    if(!uart_has_data())
        return -1;
    return uart_get8();
}

// returns:
//  - 1 if TX FIFO empty AND idle.
//  - 0 if not empty.
int uart_tx_is_empty(void) {
    // TODO: implement this!
    dev_barrier();
    unsigned s = GET32(AUX_MU_STAT);
    dev_barrier();
    return (s & BIT(9)) != 0;
}

// return only when the TX FIFO is empty AND the
// TX transmitter is idle.  
//
// used when rebooting or turning off the UART to
// make sure that any output has been completely 
// transmitted.  otherwise can get truncated 
// if reboot happens before all bytes have been
// received.
void uart_flush_tx(void) {
    while(!uart_tx_is_empty())
        rpi_wait();
}
