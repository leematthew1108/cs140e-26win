#include "nrf-test.h"

// useful to mess around with these. 
enum { ntrial = 1000, timeout_usec = 1000, nbytes = 4 };

// example possible wrapper to recv a 32-bit value.
static int net_get32(nrf_t *nic, uint32_t *out) {
    int ret = nrf_read_exact_timeout(nic, out, 4, timeout_usec);
    if(ret != 4) {
        debug("receive failed: ret=%d\n", ret);
        return 0;
    }
    return 1;
}
// example possible wrapper to send a 32-bit value.
static void net_put32(nrf_t *nic, uint32_t txaddr, uint32_t x) {
    int ret = nrf_send_ack(nic, txaddr, &x, 4);
    if(ret != 4)
        panic("ret=%d, expected 4\n");
}

void notmain(void) {
    // Init my radio as the Server
    nrf_t *my_radio = server_mk_ack(server_addr, 4); 
    
    uint32_t counter = 0;
    while(1) {
        // Wait for partner to send me something
        uint32_t got = 0;
        if (net_get32(my_radio, &got)) {
            printk("Received %d from partner!\n", got);
            
            // 3. Ping it back to them
            delay_ms(100); // Small delay to avoid network collisions
            net_put32(my_radio, client_addr, got + 1);
        }
    }
}