// very dumb, simple interface to wrap up watchpoints better.
// only handles a single watchpoint.
#include "rpi.h"
#include "watchpoint.h"
#include "asm-helpers.h"
#include "bit-support.h"

// ----------------------------------------------------------------------------
// CP15 fault registers (Table 3-2)
//
// DFSR: Data Fault Status Register  (p 3-64)
//   mrc p15, 0, <Rd>, c5, c0, 0
// FAR:  Fault Address Register      (p 3-68)
//   mrc p15, 0, <Rd>, c6, c0, 0
cp_asm_get(cp15_dfsr, p15, 0, c5, c0, 0)   // DFSR
cp_asm_get(cp15_far,  p15, 0, c6, c0, 0)   // FAR

// ----------------------------------------------------------------------------
// CP14 debug registers (Table 13-19)
//
// DSCR: Debug Status and Control Register (p 13-7; table 13-19)
//   mrc/mcr p14, 0, <Rd>, c0, c1, 0
// WFAR: Watchpoint Fault Address Register (p 13-12; table 13-19)
//   mrc p14, 0, <Rd>, c0, c6, 0
//
// WVR0: Watchpoint Value Register 0  (WVR row: op2=6, CRm=y; y=0 => c0)
//   mcr p14, 0, <Rd>, c0, c0, 6
// WCR0: Watchpoint Control Register 0 (WCR row: op2=7, CRm=y; y=0 => c0)
//   mcr p14, 0, <Rd>, c0, c0, 7
cp_asm(cp14_dscr, p14, 0, c0, c1, 0)       // DSCR get/set
cp_asm_get(cp14_wfar, p14, 0, c0, c6, 0)   // WFAR get
cp_asm_set(cp14_wvr0, p14, 0, c0, c0, 6)   // WVR0 set
cp_asm_set(cp14_wcr0, p14, 0, c0, c0, 7)   // WCR0 set

// keep track of what we are watching.
static uint32_t watch_addr;

// DFSR fault status field:
// - If bit[10] == 0, the fault status is in bits[3:0] (lab snippet: "if bit[10]=0")
// - "Debug event" encodes as 0b0010 in FS[3:0] for ARMv6 debug data aborts.
enum { DFSR_FS_DEBUG_EVENT = 0b0010 };

// DSCR[5:2] indicates which debug exception (cheat sheet p 13-11).
// In practice we only need the watchpoint case here.
// (This matches the lab’s convention used in staff solutions.)
enum { DSCR_CAUSE_WATCHPOINT = 0b0010 };

// was it a watchpoint fault?
//  1. use dfsr 3-64  to make sure it was a debug event.
//  2. and dscr 13-11: to make sure it was a watchpoint
// First check DFSR to confirm abort is a debug event
// Once confirmed, check DSCR[5:2] (p13-11) to confirm its a watchpoint event 
int watchpt_fault_p(void) {
    uint32_t dfsr = cp15_dfsr_get();

    // - If bit[10] == 0, the fault status is in bits[3:0] (lab snippet: "if bit[10]=0")
    if(bit_is_on(dfsr, 10)) {
        return 0;
    }

    uint32_t fs = bits_get(dfsr, 0, 3);
    if (fs != DFSR_FS_DEBUG_EVENT) {
        return 0;
    }

    // now that it's confirmed it was a debug event, we check if its watchpoint
    uint32_t dscr = cp14_dscr_get();
    uint32_t cause = bits_get(dscr, 2, 5);

    return cause == DSCR_CAUSE_WATCHPOINT;
}

// is it a load fault?
//  - use dfsr 3-64
// data fault status register, page 3-64
int watchpt_load_fault_p(void) {
    if(!watchpt_fault_p())
        return 0;
    // return staff_watchpt_load_fault_p();
    // bit 11 in DFSR (source of last data fault)
    // 0 = read, 1 = write
    uint32_t dfsr = cp15_dfsr_get();
    return bit_is_off(dfsr, 11);
}

// get the pc of the fault.
//   - p13-34: use <wfar> (see 3-12) to get the fault pc 
// important:
//   - pay attention to the comment on 13-12 to see how to adjust!
uint32_t watchpt_fault_pc(void) {
    // return staff_watchpt_fault_pc();
    uint32_t wfar = cp14_wfar_get();
    // ARM state, the WFAR contains the address of the instruction causing it plus 0x8
    return wfar - 0x8;
}

// get the data address that caused the fault.
// use <far> 3-68 to get the fault addr.
uint32_t watchpt_fault_addr(void) {
    // return staff_watchpt_fault_addr();
    return cp15_far_get();
}

// set a watch-point on <addr>: 
//  1. enable cp14 if not enabled.  
//     - MAKE SURE TO DO THIS FIRST.
//  2. set wcr0 (13-21), wvr0 (13-20)
//     - don't rmw -- just set it directly.
// Important: 
//  - make sure you handle subword accesses! 
int watchpt_on(uint32_t addr) {
    watch_addr = addr;
    // return staff_watchpt_on(addr);

    // enable monitor debug mode using DSCR (p 13-7, 13-9)
    // bit 14 clear for monitor debug-mode selected
    // bit 15 set for monitor debug-mode enabled
    uint32_t dscr = cp14_dscr_get();
    dscr = bit_clr(dscr, 14);
    dscr = bit_set(dscr, 15);
    cp14_dscr_set(dscr);

    // set wrc0 and wvr0
    uint32_t wcr = 0;

    // p 13-21: bits[8:5] byte address select.
    // Select all bytes so subword (byte/halfword) accesses trigger.
    wcr = bits_set(wcr, 5, 8, 0b1111);

    // p 13-22: bits[4:3] load/store match: 01 load, 10 store, 11 either.
    wcr = bits_set(wcr, 3, 4, 0b11);

    // Privilege match (lab wants both user + privileged).
    // Bits[2:1] control privilege; 11 either (privileged ad user)
    wcr = bits_set(wcr, 1, 2, 0b11);

    // Watchpoint enabled (bit 0).
    wcr = bit_set(wcr, 0);

    cp14_wcr0_set(wcr);

    // handles subword access? (mentally recheck)
    // WVR is a word address; clearing low 2 bits is the clean, explicit version.
    cp14_wvr0_set(addr & ~0b11u);

    // Note: cp14_wcr0_set/cp14_wvr0_set already do prefetch_flush() via cp_asm_set.
    return 1;
}

// turn off watchpoint:
//   - check that <addr> is what we were watching.
//   - clear wcr0
int watchpt_off(uint32_t addr) {
    if(addr != watch_addr)
        panic("disabling invalid watchpoint %x, tracking %x\n", 
            addr, watch_addr);
    // return staff_watchpt_off(addr);
    cp14_wcr0_set(0);
    return 1;
}
