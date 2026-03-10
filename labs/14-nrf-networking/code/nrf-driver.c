// skeleton for nrf driver.   
//    - sorry it's so large.  the device is pretty complicated and we're
//      just doing a single lab.
//
// --------------------------------------------------------------------
// key files:
//  1. nrf.h: <nrf_t> the nic structure and <nrf_config_t> some of the
//     configuration.  
//  2. <nrf-hw-support.h>: many accessors for reading and writing
//     NRF registers and for SPI.  you really want to look at this 
//     to save time.
//  3. <nrf-hw-support.c>: SPI read/write routines, routines to dump the 
//     NRF configuration.
//  4. <nrf-default-values.h> default values for NRF hardware choices.
//     you want to look at to see what we can change and for where
//     to get the requested values.
//  5. <nrf-public.c> simple veneer over the top of NRF interface.
//
// --------------------------------------------------------------------
//  big picture: stay in RX by default.  only go TX to send then back.
//  0. at startup go PowerDown->StandbyI->RX
//  1. stay in RX p22]
//  2. to send: RX -> StandbyI ->TX -> Standby-II [can send multiple]
//      then StandbyI->RX mode to get back into RX.
//
// alot is in comments, but not everything.  if you're going to use the
// comment, should double check that it's accurate!
//
// --------------------------------------------------------------------
// you definitely want to to:
//  1. check anything you can to make sure the state matches what you think
//     it should be
//  2. always follow the state machine in the datasheet.
//  3. general strategy: in RX mode, quickly flip to TX if have something
//     to send, flip back to RX.
//
// --------------------------------------------------------------------
// useful extensions:
// 0. send large packets by fragmenting and reassembling.
// 1. a reliable FIFO that handles duplicates, out of order, corruption,
//    packets from previous sessions (e.g., when one reboots).
// 2. network boot.  this is fun, very useful.
// 3. possible: add interrupts for receiving packets.  don't do much
//    in the handler.  can also let the remote node send "REBOOT"
//    commands so that you can control a big network.
//
#include "nrf.h"
#include "nrf-hw-support.h"

// precompute the rx and tx configs so we can 
// just set them without thinking.
//
// enable crc, enable 2 byte
#   define set_bit(x)       (1<<(x))
#   define enable_crc      set_bit(3)
#   define crc_two_byte    set_bit(2)
#   define pwr_up          set_bit(PWR_UP_BIT)
#   define mask_int        set_bit(6)|set_bit(5)|set_bit(4)
enum {
    // pre-computed: can write into NRF_CONFIG to enable TX.
    tx_config = enable_crc | crc_two_byte | pwr_up | mask_int,
    // pre-computed: can write into NRF_CONFIG to enable RX.
    rx_config = tx_config  | set_bit(PRIM_RX)
} ;

// set <ce> low: we use a helper so can
// do some error checking.
static inline void ce_lo(uint8_t ce) {
    if(ce != 6 && ce != 5)
        panic("expected 6 or 5, have: %d\n", ce);
    // recall: we didn't use any dev barriers in <gpio.c>
    dev_barrier();
    // todo("implement this\n");
    gpio_write(ce, 0); // Pull CE low
}

// set <ce> high: we use a helper so we can
// do some error checking.
static inline void ce_hi(uint8_t ce) {
    if(ce != 6 && ce != 5)
        panic("expected 6 or 5, have: %d\n", ce);
    // recall: we didn't use any dev barriers in <gpio.c>
    dev_barrier();
    // todo("implement this\n");
    gpio_write(ce, 1); // Pull CE high
}

// Forward declarations for state machine transitions
static void nrf_rx_mode(nrf_t *n);
static void nrf_tx_mode(nrf_t *n);

// initialize the NRF: [extension: pass in a channel]
nrf_t *nrf_init(nrf_conf_t c, uint32_t rxaddr, unsigned acked_p) {
    // to write yours, comment this out and start editing below.
    // if you get weird results later (part 2, part 3 etc), 
    // flip back on.
    // return staff_nrf_init(c, rxaddr, acked_p);

    // start of initialization: go through and handle no-ack first,
    // then ack.  i'd do one test at a time.

    nrf_t *n = kmalloc(sizeof *n);
    n->config = c;      // set initial config.
    nrf_stat_start(n);  // reset stats.

    // configure the spi hardware and ce pin.
    // <nrf-hw-support.c>
    n->spi = nrf_spi_init(c.ce_pin, c.spi_chip);

    n->rxaddr = rxaddr;     // set the rxaddr
    cq_init(&n->recvq, 1);  // initialize the circular queue

    // p22: put in PWR_DOWN so can configure.
    nrf_put8_chk(n, NRF_CONFIG, 0);
    assert(!nrf_is_pwrup(n));

    // disable all pipes.
    nrf_put8_chk(n, NRF_EN_RXADDR, 0);

    if(!acked_p) {
        // disable retran [reg1: NRF_EN_AA]
        // enable pipe 1: 
        //   - NRF_EN_RXADDR
        //   - NRF_SETUP_RETR
        // todo("fill this in!\n");

        // No ACKs, p57
        nrf_put8_chk(n, NRF_EN_AA, 0);         // Disable Auto-Ack for all pipes
        nrf_put8_chk(n, NRF_EN_RXADDR, 0b10);  // Enable Pipe 1 (bit 1)
        nrf_put8_chk(n, NRF_SETUP_RETR, 0);    // Disable retransmissions

        // when done, these should be true.
        // <nrf-hw-support.h>
        assert(nrf_pipe_is_enabled(n, 1));
        assert(!nrf_pipe_is_acked(n, 1));
        assert(!nrf_pipe_is_enabled(n, 0));
    
    } else {
        // p 57 and p75 (protocol)
        // reg1: NRF_EN_AA: both retran pipe(0) and pipe(1) have 
        //    to be ENAA_P* = 1
        // reg=2: NRF_EN_RXADDR: enable pipes --- always enable pipe 
        //    0 for retran.

        // reg = 4: NRF_SETUP_RETR: set retrans attempt and delay
        // compute NRF_SETUP_RETR using:
        //  - nrf_default_retran_attempts;
        //  - nrf_default_retran_delay [note you have to 
        //    convert to the right encoding]

        // todo("fill this in!\n");

        // With ACKs. 
        // Enable Auto-Ack on Pipe 0 and Pipe 1 (bits 0 and 1)
        nrf_put8_chk(n, NRF_EN_AA, 0b11);      
        
        // Enable Pipe 0 (for receiving ACKs) and Pipe 1 (for data)
        nrf_put8_chk(n, NRF_EN_RXADDR, 0b11);  

        // p.58: SETUP_RETR: bits 7:4 = delay, bits 3:0 = count
        // Delay encoding: 250us steps. Formula: (delay / 250) - 1. 
        // 2000us / 250 - 1 = 7.
        uint8_t retran_delay = (nrf_default_retran_delay / 250) - 1; 
        uint8_t retran_cnt = nrf_default_retran_attempts;
        nrf_put8_chk(n, NRF_SETUP_RETR, (retran_delay << 4) | retran_cnt);

        // double check that both are enabled + acked.
        // helpers are in: <nrf-hw-support.h>
        assert(nrf_pipe_is_enabled(n, 0));
        assert(nrf_pipe_is_enabled(n, 1));
        assert(nrf_pipe_is_acked(n, 1));
        assert(nrf_pipe_is_acked(n, 0));
    }

    // turn off the other pipes.
    // todo("turn off other pipes\n");
    for(int i = 2; i < 6; i++) {
        nrf_bit_clr(n, NRF_EN_RXADDR, i); // Disable receiving on this pipe
        nrf_bit_clr(n, NRF_EN_AA, i);     // Disable auto-ack on this pipe
    }

    // check
    for(int i = 2; i < 6; i++)
        assert(!nrf_pipe_is_enabled(n, i));



    // reg=3: NRF_SETUP_AW: setup address size
    // todo("look at encoding!\n");
    // p.58: SETUP_AW encoding: 01 = 3 bytes, 10 = 4 bytes, 11 = 5 bytes. 
    // So the value to write is just (bytes - 2).
    nrf_put8_chk(n, NRF_SETUP_AW, nrf_default_addr_nbytes - 2);

    // clear NRF_TX_ADDR for determinism.
    // todo("set TX_ADDR addr to 0 for determinism\n");
    // The TX address is 3-5 bytes, so we MUST use nrf_set_addr, not nrf_put8!
    nrf_set_addr(n, NRF_TX_ADDR, 0, nrf_default_addr_nbytes);

    // set NRF_RX_PW_P1 and  NRF_RX_ADDR_P1
    // set NRF_RX_ADDR_P0 if enabled.
    // could also do when setup pipes.
    nrf_put8_chk(n, NRF_RX_PW_P1, c.nbytes);
    nrf_set_addr(n, NRF_RX_ADDR_P1, rxaddr, nrf_default_addr_nbytes);

    // README: "so the test pass always set NRF_RX_ADDR_P0 to 0"
    nrf_set_addr(n, NRF_RX_ADDR_P0, 0, nrf_default_addr_nbytes);
    
    // The test also expects the payload width of Pipe 0 to be 0 at init.
    // We will set both of these dynamically inside nrf_tx_send_ack.
    nrf_put8_chk(n, NRF_RX_PW_P0, 0);

    // Set message size = 0 for unused pipes.  
    //  [NOTE: I think redundant, but just to be sure.]
    for(int i = 2; i < 6; i++) {
        // RX_PW_P0 is 0x11, P1 is 0x12... so P0 + i maps exactly to the registers
        nrf_put8_chk(n, NRF_RX_PW_P0 + i, 0); 
    }

    // reg=6: RF_SETUP: setup data rate and power.
    // datarate already has the right encoding.
    // 
    // use:
    //  - nrf_default_data_rate;
    //  - nrf_default_db;
    // todo("setup NRF_RF_SETUP\n");
    // Set the channel (frequency) first (reg 0x5)
    nrf_put8_chk(n, NRF_RF_CH, nrf_default_channel);
    // Set data rate and power (reg 0x6)
    nrf_put8_chk(n, NRF_RF_SETUP, nrf_default_data_rate | nrf_default_db);

    // reg=7: status.  p59
    // sanity check that it is empty and nothing else is set.
    //
    // NOTE: if we are in the midst of an active system,
    // it's possible this node receives a message which will
    // change these values.   we might want to set the
    // rx addresses to something that won't get a message.
    // 
    // ideally we would do something where we use different 
    // addresses across reboots?   we get a bit of this benefit
    // by waiting the 100ms.

    // i think w/ the nic is off, this better be true.
    assert(!nrf_tx_fifo_full(n));
    assert(nrf_tx_fifo_empty(n));
    assert(!nrf_rx_fifo_full(n));
    assert(nrf_rx_fifo_empty(n));

    assert(!nrf_has_rx_intr(n));
    assert(!nrf_has_tx_intr(n));
    assert(pipeid_empty(nrf_rx_get_pipeid(n)));
    assert(!nrf_rx_has_packet(n));

    // we skip reg=0x8 (observation)
    // we skip reg=0x9 (RPD)
    // we skip reg=0xA (P0)
    // we skip reg=0x10 (TX_ADDR): used only when sending.
    nrf_put8_chk(n, NRF_FEATURE, 0);
    nrf_put8_chk(n, NRF_DYNPD, 0);

    nrf_tx_flush(n);
    nrf_rx_flush(n);

    // p20: go from <PowerDown> to <Standby-I>
    // now go from make sure you delay long enough!
    // todo("go from <PowerDown> to <Standby-I>");
    // CE must be LOW to stay in Standby-I
    ce_lo(n->config.ce_pin); 
    // Set PWR_UP bit to 1. tx_config conveniently has PWR_UP=1 and PRIM_RX=0.
    nrf_put8_chk(n, NRF_CONFIG, tx_config); 
    // p.24 Table 16: Tpd2stby (Power Down to Standby) is max 1.5ms. 2ms is safe.
    delay_ms(2);

    // now go to RX mode: invariant = we are always in RX except for the 
    // small amount of time we need to switch to TX to send a message.
    // todo("go from <Standby-I> to RX");
    // Since we'll use nrf_rx_mode elsewhere, we just call the helper!
    nrf_rx_mode(n);

    // should be true after setup.
    if(acked_p) {
        nrf_opt_assert(n, nrf_get8(n, NRF_CONFIG) == rx_config);
        nrf_opt_assert(n, nrf_pipe_is_enabled(n, 0));
        nrf_opt_assert(n, nrf_pipe_is_enabled(n, 1));
        nrf_opt_assert(n, nrf_pipe_is_acked(n, 0));
        nrf_opt_assert(n, nrf_pipe_is_acked(n, 1));
        nrf_opt_assert(n, nrf_tx_fifo_empty(n));
    } else {
        nrf_opt_assert(n, nrf_get8(n, NRF_CONFIG) == rx_config);
        nrf_opt_assert(n, !nrf_pipe_is_enabled(n, 0));
        nrf_opt_assert(n, nrf_pipe_is_enabled(n, 1));
        nrf_opt_assert(n, !nrf_pipe_is_acked(n, 1));
        nrf_opt_assert(n, nrf_tx_fifo_empty(n));
    }
    return n;
}

// put the device in RX mode: 
//   - go to <Standby-I> so we can transition in 
//     a legal state (*i think*)
// invariant:
//    we are always in RX except for the small amount 
//    of time we need to switch to TX to send a message.
//    we then immediately switch back to RX.
static void nrf_rx_mode(nrf_t *n) {
    // todo("go to RX with delay");

    // p.22: Transition to Standby-I first 
    ce_lo(n->config.ce_pin); 
    
    // Set PRIM_RX=1
    nrf_put8_chk(n, NRF_CONFIG, rx_config); 
    
    // Activate RX mode
    ce_hi(n->config.ce_pin); 
    
    // p.24: Transition from Standby-I to RX takes 130us max
    delay_us(130);

    nrf_opt_assert(n, nrf_is_rx(n));
}


// go from RX from TX via the state diagram.
// eliminate the 10usec requirement by going from:
//       RX -> StandbyI (by setting CE=0) 
//          -> TX (by writing data into the FIFO, CE=1 for 10usec)
//          -> Standby-II (CE=1)
//          -> Standby I (CE=0)
// see the state machine on p22, and [p75]
//
// NOTE: If nRF24L01+ is in standby-II mode, it goes to standby-I mode
// immediately if CE is set low. [p75]
static void nrf_tx_mode(nrf_t *n) {
    // NOTE: tx fifo should not be empty.
    //todo("go RX->StandbyI->StandbyII->TX");

    // p.22: RX -> Standby-I
    ce_lo(n->config.ce_pin); 
    
    // Set PRIM_RX=0
    nrf_put8_chk(n, NRF_CONFIG, tx_config); 
    
    // Activate TX mode. p.22: CE high for >10us to trigger send.
    // Keeping it high transitions us to Standby-II while TX FIFO has data,
    // then it will transmit.
    ce_hi(n->config.ce_pin); 
    
    // 130us settling time
    delay_us(130);

    nrf_opt_assert(n, nrf_is_tx(n));
}


int nrf_tx_send_ack(nrf_t *n, uint32_t txaddr, 
    const void *msg, unsigned nbytes) {

    // default config for acked state.
    nrf_opt_assert(n, nrf_get8(n, NRF_CONFIG) == rx_config);
    nrf_opt_assert(n, nrf_pipe_is_enabled(n, 0));
    nrf_opt_assert(n, nrf_pipe_is_enabled(n, 1));
    nrf_opt_assert(n, nrf_pipe_is_acked(n, 0));
    nrf_opt_assert(n, nrf_pipe_is_acked(n, 1));
    nrf_opt_assert(n, nrf_tx_fifo_empty(n));

    // if interrupts not enabled: make sure we check for packets.
    while(nrf_get_pkts(n))
        ;

    // TODO: you would implement the rest of the code at this point.
    // see page 75 for transmit.  do the no_ack case first.
    // NOTE: 
    //  - have to set both NRF_RX_ADDR_P0 and NRF_TX_ADDR.
    //    alot more stuff.
    //  - put the data on FIFO before state transitioning.
    //  - if you get more than max_rt_interrupts
    //    you can panic for today.  but dump out the 
    //    configuration: good chance its a bad configure.
    // int res = staff_nrf_tx_send_ack(n, txaddr, msg, nbytes);

    // How to increment the total number of retransmissions.
    //  uint8_t cnt = nrf_get8(n, NRF_OBSERVE_TX);
    //  n->tot_retrans  += bits_get(cnt,0,3);

    // Set TX address AND Pipe 0 RX address so we can receive the ACK.
    nrf_set_addr(n, NRF_TX_ADDR, txaddr, nrf_default_addr_nbytes);
    nrf_set_addr(n, NRF_RX_ADDR_P0, txaddr, nrf_default_addr_nbytes);

    // Load payload
    nrf_putn(n, NRF_W_TX_PAYLOAD, msg, nbytes);

    // Pulse TX
    nrf_tx_mode(n);

    // Wait for either success (TX_DS) or total failure (MAX_RT)
    while(!nrf_has_tx_intr(n) && !nrf_has_max_rt_intr(n));

    if (nrf_has_max_rt_intr(n)) { // max retry limit
        // Clear the MAX_RT interrupt (Bit 4) so we aren't permanently stuck
        nrf_put8(n, NRF_STATUS, (1 << 4));
        
        // p.33: If a packet is lost, we HAVE to flush the TX FIFO manually.
        nrf_tx_flush(n); 
        
        // Update stats
        n->tot_lost++;
        
        nrf_dump("MAX_RT HIT: Connection failed or massive interference", n);
        panic("Max retransmissions hit without ACK.\n");
    }

    // Success! Clear TX_DS interrupt (Bit 5)
    nrf_put8(n, NRF_STATUS, (1 << 5));

    n->tot_sent_msgs++;
    n->tot_sent_bytes += nbytes;

    // Return to listening
    nrf_rx_mode(n);

    // when done: tx interrupt better be cleared.
    nrf_opt_assert(n, !nrf_has_tx_intr(n));
    // when done: better be back in rx mode.
    nrf_opt_assert(n, nrf_get8(n, NRF_CONFIG) == rx_config);
    return nbytes;
}

// send packet without hardware ack.
int nrf_tx_send_noack(nrf_t *n, uint32_t txaddr, 
    const void *msg, unsigned nbytes) {

    // default state for no-ack config.
    nrf_opt_assert(n, nrf_get8(n, NRF_CONFIG) == rx_config);
    nrf_opt_assert(n, !nrf_pipe_is_enabled(n, 0));
    nrf_opt_assert(n, nrf_pipe_is_enabled(n, 1));
    nrf_opt_assert(n, !nrf_pipe_is_acked(n, 1));
    nrf_opt_assert(n, nrf_tx_fifo_empty(n));

    // if interrupts not enabled: make sure we check for packets.
    while(nrf_get_pkts(n))
        ;

    // TODO: you would implement the send packet code.
    // see page 75.
    // 1. put packet on TX fifo.
    // 2. put NRF in TX mode, 
    // 3. wait for TX interrupt.
    // 4. assert that tx fifo is empty.
    // 5. Clear the tx interrupt.
    // 6. put back in rx mode.  (via standbyII->standbyI)
    // NOTE: 
    //   - If nRF24L01+ is in standby-II mode, it goes to 
    //     standby-I mode immediately if CE is set low.
    // int res = staff_nrf_tx_send_noack(n, txaddr, msg, nbytes);

    // Set destination address
    nrf_set_addr(n, NRF_TX_ADDR, txaddr, nrf_default_addr_nbytes);

    // p.51: Load payload into TX FIFO
    nrf_putn(n, NRF_W_TX_PAYLOAD, msg, nbytes);

    // Pulse TX
    nrf_tx_mode(n);

    // Wait for TX_DS (Data Sent) interrupt in STATUS register
    while(!nrf_has_tx_intr(n));

    assert(nrf_tx_fifo_empty(n));

    // Clear TX_DS interrupt bit (Bit 5)
    nrf_put8(n, NRF_STATUS, (1 << 5));

    n->tot_sent_msgs++;
    n->tot_sent_bytes += nbytes;

    // Immediately return to listening mode
    nrf_rx_mode(n);

    // after done: tx interrupt better be cleared.
    nrf_opt_assert(n, !nrf_has_tx_intr(n));
    // after done: better be back in rx mode.
    nrf_opt_assert(n, nrf_get8(n, NRF_CONFIG) == rx_config);
    return nbytes;
}

// while the RX fifo is not empty: read packets into the 
// circular queue <recvq> using <cq_push_n>. 
//
// return number of packets read.
//
// NOTE: 
//   - better be in RX mode!
//   - since we don't have interrupts on, we call this before doing
//     tx to make sure there is space and to reduce chance of dropping
//     packets.
//   - if you send packets you'll have to make sure the packets won't
//     get dropped b/c the receiver is not listening.  can use
//     interrupts.
int nrf_get_pkts(nrf_t *n) {
    // you can't check for packets unless in RX mode.
    nrf_opt_assert(n, nrf_get8(n, NRF_CONFIG) == rx_config);
    if(!nrf_rx_has_packet(n)) 
        return 0; 

    // TODO:
    // data sheet gives the sequence to follow to get packets.
    // p76: 
    // do {
    //    0. assert(pipeid from STATUS != PIPEID_EMPTY)
    //    1. read packet through spi [nrf_getn]
    //    2. push onto the receive queue [cq_push_n],
    //       increment <tot_recv_msgs> and <tot_recv_bytes>
    //    3. clear rx IRQ.
    //    4. read fifo status to see if more packets: 
    //       if so, repeat from (1) --- we need to do this now in case
    //       a packet arrives b/n (1) and (2)
    // } while (rx fifo is not empty)
    // int res = staff_nrf_get_pkts(n);

    int pkts_read = 0;
    
    // p.76
    while (!nrf_rx_fifo_empty(n)) {
        uint8_t pipeid = nrf_rx_get_pipeid(n);
        if (pipeid == 0b111) break; // 0b111 means FIFO empty according to STATUS

        // temp array
        uint8_t buf[32]; 
        
        // p.51: SPI command to read RX payload
        nrf_getn(n, NRF_R_RX_PAYLOAD, buf, n->config.nbytes);

        if(!cq_push_n(&n->recvq, buf, n->config.nbytes))
            panic("not enough space in receive queue\n");

        n->tot_recv_msgs++;
        n->tot_recv_bytes += n->config.nbytes;
        pkts_read++;

        // Clear RX_DR interrupt bit in STATUS register (Bit 6).
        // p.59: Write 1 to clear interrupt.
        nrf_put8(n, NRF_STATUS, (1 << 6)); 
    }

    nrf_opt_assert(n, nrf_get8(n, NRF_CONFIG) == rx_config);
    return pkts_read;
}
