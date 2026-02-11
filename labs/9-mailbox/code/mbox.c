#include "rpi.h"
#include "mbox.h"

// dump out the entire messaage.  useful for debug.
void msg_dump(const char *msg, volatile uint32_t *u, unsigned nwords) {
    printk("%s\n", msg);
    for(int i = 0; i < nwords; i++)
        output("u[%d]=%x\n", i,u[i]);
}

/*
  This is given.

  Get board serial
    Tag: 0x00010004
    Request: Length: 0
    Response: Length: 8
    Value: u64: board serial
*/
uint64_t rpi_get_serialnum(void) {
    // 16-byte aligned 32-bit array
    volatile uint32_t msg[8] __attribute__((aligned(16)));

    // make sure aligned
    assert((unsigned)msg%16 == 0);

    msg[0] = 8*4;         // total size in bytes.
    msg[1] = 0;           // sender: always 0.
    msg[2] = 0x00010004;  // serial tag
    msg[3] = 8;           // total bytes avail for reply
    msg[4] = 0;           // request code [0].
    msg[5] = 0;           // space for 1st word of reply 
    msg[6] = 0;           // space for 2nd word of reply 
    msg[7] = 0;   // end tag

    // send and receive message
    mbox_send(MBOX_CH, msg);

#if 0
    // if you want to debug.
    output("got:\n");
    for(int i = 0; i < 8; i++)
        output("msg[%d]=%x\n", i, msg[i]);
#endif

    // should have value for success: 1<<31
    if(msg[1] != 0x80000000)
		panic("invalid response: got %x\n", msg[1]);

    // high bit should be set and reply size
    assert(msg[4] == ((1<<31) | 8));

    // for me the upper 32 bits were never non-zero.  
    // not sure if always true?
    assert(msg[6] == 0);
    return msg[5];
}

uint32_t rpi_get_memsize(void) {
    // todo("get the pi's physical memory size");

    // 16-byte aligned 32-bit array
    volatile uint32_t msg[8] __attribute__((aligned(16)));

    // make sure aligned
    assert((unsigned)msg%16 == 0);

    msg[0] = 8*4;         // total size in bytes.
    msg[1] = 0;           // sender: always 0.
    msg[2] = 0x00010005;  // serial tag for "Get ARM memory"
    msg[3] = 8;           // total bytes avail for reply
    msg[4] = 0;           // request code [0].
    msg[5] = 0;           // base address in bytes
    msg[6] = 0;           // size in bytes
    msg[7] = 0;   // end tag

    // send and receive message
    mbox_send(MBOX_CH, msg);

    // should have value for success: 1<<31
    if(msg[1] != 0x80000000)
		panic("invalid response: got %x\n", msg[1]);

    // high bit should be set and reply size
    assert(msg[4] == ((1<<31) | 8));

    return msg[6];
}


uint32_t rpi_get_model(void) {
    // todo("get the pi's model number");
    // 16-byte aligned 32-bit array
    volatile uint32_t msg[7] __attribute__((aligned(16)));

    // make sure aligned
    assert((unsigned)msg%16 == 0);

    msg[0] = 7*4;         // total size in bytes.
    msg[1] = 0;           // sender: always 0.
    msg[2] = 0x00010001;  // serial tag for Get board model
    msg[3] = 4;           // total bytes avail for reply
    msg[4] = 0;           // request code [0].
    msg[5] = 0;           // space for reply (u32 board model)
    msg[6] = 0;   // end tag

    // send and receive message
    mbox_send(MBOX_CH, msg);

    // should have value for success: 1<<31
    if(msg[1] != 0x80000000)
		panic("invalid response: got %x\n", msg[1]);

    // high bit should be set and reply size
    assert(msg[4] == ((1<<31) | 4));

    return msg[5];
}

// https://www.raspberrypi-spy.co.uk/2012/09/checking-your-raspberry-pi-board-version/
uint32_t rpi_get_revision(void) {
    // todo("get the pi's revision number");

    // 16-byte aligned 32-bit array
    volatile uint32_t msg[7] __attribute__((aligned(16)));

    // make sure aligned
    assert((unsigned)msg%16 == 0);

    msg[0] = 7*4;         // total size in bytes.
    msg[1] = 0;           // sender: always 0.
    msg[2] = 0x00010002;  // serial tag for "Get board revision"
    msg[3] = 4;           // total bytes avail for reply
    msg[4] = 0;           // request code [0].
    msg[5] = 0;           // u32: board revision
    msg[6] = 0;   // end tag

    // send and receive message
    mbox_send(MBOX_CH, msg);

    // should have value for success: 1<<31
    if(msg[1] != 0x80000000)
		panic("invalid response: got %x\n", msg[1]);

    // high bit should be set and reply size
    assert(msg[4] == ((1<<31) | 4));

    return msg[5];
}

uint32_t rpi_temp_get(void) {
    // todo("get the pi's temperature");
    // 16-byte aligned 32-bit array
    volatile uint32_t msg[8] __attribute__((aligned(16)));

    // make sure aligned
    assert((unsigned)msg%16 == 0);

    msg[0] = 8*4;         // total size in bytes.
    msg[1] = 0;           // sender: always 0.
    msg[2] = 0x00030006;  // serial tag for "Get temperature"
    msg[3] = 8;           // total bytes avail for reply
    msg[4] = 0;           // request code [0].
    msg[5] = 0;           // temperature id (should be 0)
    msg[6] = 0;           // value
    msg[7] = 0;   // end tag

    // send and receive message
    mbox_send(MBOX_CH, msg);

    // should have value for success: 1<<31
    if(msg[1] != 0x80000000)
		panic("invalid response: got %x\n", msg[1]);

    // high bit should be set and reply size
    assert(msg[4] == ((1<<31) | 8));

    return msg[6];
}

/*
Get clock rate

    Tag: 0x00030002
    Request:
        Length: 4
        Value:
            u32: clock id
    Response:
        Length: 8
        Value:
            u32: clock id
            u32: rate (in Hz)
*/
uint32_t rpi_clock_curhz_get(uint32_t clock) {
    // 16-byte aligned 32-bit array
    volatile uint32_t msg[8] __attribute__((aligned(16)));

    // make sure aligned
    assert((unsigned)msg%16 == 0);

    msg[0] = 8*4;         // total size in bytes.
    msg[1] = 0;           // sender: always 0.
    msg[2] = 0x00030002;  // serial tag 
    msg[3] = 8;           // total bytes avail for reply
    msg[4] = 0;           // request code [0].
    msg[5] = clock;       // input: clock id,returns clock id
    msg[6] = 0;           // returns rate in Hz
    msg[7] = 0;   // end tag

    // send and receive message
    mbox_send(MBOX_CH, msg);

    // should have value for success: 1<<31
    if(msg[1] != 0x80000000)
		panic("invalid response: got %x\n", msg[1]);

    // high bit should be set and reply size
    assert(msg[4] == ((1<<31) | 8));

    return msg[6];
}


/*
Get clock rate measured

    Tag: 0x00030047
    Request:
        Length: 4
        Value:
            u32: clock id
    Response:
        Length: 8
        Value:
            u32: clock id
            u32: rate (in Hz)

*/
uint32_t rpi_clock_realhz_get(uint32_t clock) {
        // 16-byte aligned 32-bit array
    volatile uint32_t msg[8] __attribute__((aligned(16)));

    // make sure aligned
    assert((unsigned)msg%16 == 0);

    msg[0] = 8*4;         // total size in bytes.
    msg[1] = 0;           // sender: always 0.
    msg[2] = 0x00030047;  // serial tag 
    msg[3] = 8;           // total bytes avail for reply
    msg[4] = 0;           // request code [0].
    msg[5] = clock;       // input: clock id,returns clock id
    msg[6] = 0;           // returns rate in Hz
    msg[7] = 0;   // end tag

    // send and receive message
    mbox_send(MBOX_CH, msg);

    // should have value for success: 1<<31
    if(msg[1] != 0x80000000)
		panic("invalid response: got %x\n", msg[1]);

    // high bit should be set and reply size
    assert(msg[4] == ((1<<31) | 8));

    return msg[6];
}

/*
Set clock rate

    Tag: 0x00038002
    Request:
        Length: 12
        Value:
            u32: clock id
            u32: rate (in Hz)
            u32: skip setting turbo
    Response:
        Length: 8
        Value:
            u32: clock id
            u32: rate (in Hz)

*/
uint32_t rpi_clock_hz_set(uint32_t clock, uint32_t hz) {
        // 16-byte aligned 32-bit array
    volatile uint32_t msg[9] __attribute__((aligned(16)));

    // make sure aligned
    assert((unsigned)msg%16 == 0);

    msg[0] = 9*4;         // total size in bytes.
    msg[1] = 0;           // sender: always 0.
    msg[2] = 0x00038002;  // serial tag 
    msg[3] = 12;           // total bytes avail for reply
    msg[4] = 0;           // request code [0].
    msg[5] = clock;       // input: clock id,returns clock id
    msg[6] = hz;           // input: rate, returns rate in Hz
    msg[7] = 0;           // set "skip setting turbo" to 0 (not 1, this disables it)
    msg[8] = 0;   // end tag

    // send and receive message
    mbox_send(MBOX_CH, msg);

    // should have value for success: 1<<31
    if(msg[1] != 0x80000000)
		panic("invalid response: got %x\n", msg[1]);

    // high bit should be set and reply size
    assert(msg[4] == ((1<<31) | 8));

    return msg[6];
}

/*

Get max clock rate

    Tag: 0x00030004
    Request:
        Length: 4
        Value:
            u32: clock id
    Response:
        Length: 8
        Value:
            u32: clock id
            u32: rate (in Hz)

*/
uint32_t rpi_clock_maxhz_get(uint32_t clock) {
        // 16-byte aligned 32-bit array
    volatile uint32_t msg[8] __attribute__((aligned(16)));

    // make sure aligned
    assert((unsigned)msg%16 == 0);

    msg[0] = 8*4;         // total size in bytes.
    msg[1] = 0;           // sender: always 0.
    msg[2] = 0x00030004;  // serial tag 
    msg[3] = 8;           // total bytes avail for reply
    msg[4] = 0;           // request code [0].
    msg[5] = clock;       // input: clock id,returns clock id
    msg[6] = 0;           // returns rate in Hz
    msg[7] = 0;   // end tag

    // send and receive message
    mbox_send(MBOX_CH, msg);

    // should have value for success: 1<<31
    if(msg[1] != 0x80000000)
		panic("invalid response: got %x\n", msg[1]);

    // high bit should be set and reply size
    assert(msg[4] == ((1<<31) | 8));

    return msg[6];
}
