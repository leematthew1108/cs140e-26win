#include "rpi.h"
#include "rpi-interrupts.h"
#include "asm-helpers.h"
#include "bit-support.h"
#include "breakpoint.h"

// -----------------------------------------------------------------------------
// CP15: Instruction Fault Status Register (IFSR) (Table 3-2, p 3-66)
//   mrc p15, 0, <Rd>, c5, c0, 1
cp_asm_get(cp15_ifsr, p15, 0, c5, c0, 1)   // IFSR (check instruction debug event)

// -----------------------------------------------------------------------------
// CP14 debug regs (Table 13-19)
//
// DSCR: Debug Status and Control Register (p13-7; table 13-19, p13-26)
//   mrc/mcr p14, 0, <Rd>, c0, c1, 0
cp_asm(cp14_dscr, p14, 0, c0, c1, 0) // DSCR (enable debugging)

// Breakpoint registers (Table 13-19, BVR/BCR rows): (also 13-16, 13-17)
//   BVR y: mrc/mcr p14, 0, <Rd>, c0, cy, 4   (Opcode_2=4, CRm=cy) (breakpoint address)
//   BCR y: mrc/mcr p14, 0, <Rd>, c0, cy, 5   (Opcode_2=5, CRm=cy) (breakpoint control/configure)
//
// We use:
//   - breakpoint 0 for mismatch (single-step)
//   - breakpoint 1 for match (simple breakpoint)
cp_asm(cp14_bvr0, p14, 0, c0, c0, 4)
cp_asm(cp14_bcr0, p14, 0, c0, c0, 5)

cp_asm(cp14_bvr1, p14, 0, c0, c1, 4)
cp_asm(cp14_bcr1, p14, 0, c0, c1, 5)


// IFSR fault status field:
// - If bit[10] == 0, the fault status is in bits[3:0] 
// - "Debug event" encodes as 0b0010 in FS[3:0] for debug prefetch aborts.
enum { IFSR_FS_DEBUG_EVENT = 0b0010 };

// DSCR[5:2] indicates which debug exception (p13-11).
// For breakpoint events, the code differs from watchpoint.
// check DSCR bits (13-11) to see if it was a breakpoint
// b0001 = a breakpoint occured
enum { DSCR_CAUSE_BREAKPOINT = 0b0001 };

// BCR fields (p13-17 / p13-19 table summaries): (Breakpoint Control Registers)
// - enable: bit0
// - privilege: bits[2:1]
// - secure/non-secure match: bits[15:14] = 0b00
// - match/mismatch: bits[22:21] (match=01, mismatch=10)
static inline uint32_t bcr_common_enable(uint32_t bcr) {
    // enable
    bcr = bit_set(bcr, 0);

    // privilege: match in both user + privileged (bits[2:1] = 0b11)
    bcr = bits_set(bcr, 1, 2, 0b11);

    // Byte Address Select: BVR is word address, 
    // BCR[8:5] used to program subword matches
    // must be set to b1111
    bcr = bits_set(bcr, 5, 8, 0b1111);

    // secure/non-secure: match both (bits[15:14] = 0b00)
    bcr = bits_set(bcr, 14, 15, 0b00);

    return bcr;
}

static inline uint32_t bcr_set_match(uint32_t bcr) {
    // bits[22:21] = 01 for match (p13-17)
    return bits_set(bcr, 21, 22, 0b01);
}

static inline uint32_t bcr_set_mismatch(uint32_t bcr) {
    // bits[22:21] = 10 for mismatch (p13-17)
    return bits_set(bcr, 21, 22, 0b10);
}

// make sure that cp14 (DSCR) is enabled (p13-7)
void brkpt_match_init(void) {
    // staff_brkpt_match_init();
    uint32_t dscr = cp14_dscr_get();
    // p13-9 rule: monitor debug mode selected+enabled -> bit14 clear, bit15 set
    dscr = bit_clr(dscr, 14); // mode select: monitor debug mode is 0
    dscr = bit_set(dscr, 15); // enable is 1
    cp14_dscr_set(dscr);
}

// set match on <addr>
//
// for simplicity: for matching we use breakpoint 1 
// (bcr1 p13-17 and bvr1 p13-16) so dont't conflict 
// with single-stepping.   
void brkpt_match_set(uint32_t addr) {
    // staff_brkpt_match_set(addr);
    // Ensure debug enabled.
    brkpt_match_init();

    // Code BVR1 with word-aligned address.
    cp14_bvr1_set(addr & ~0b11u);

    // Code BCR1: match + enable + privilege + security config.
    uint32_t bcr = 0;
    bcr = bcr_common_enable(bcr);
    bcr = bcr_set_match(bcr);

    // Subword matching (bits[8:5]) exists, but we don’t need it for instruction breakpoints.
    // Leave as 0.

    cp14_bcr1_set(bcr);
}

// turn off match faults (clear bcr1)
void brkpt_match_stop(void) {
    // staff_brkpt_match_stop();
    cp14_bcr1_set(0);
}

// return the match addr (bvr1)
uint32_t brkpt_match_get(void) {
    // return staff_brkpt_match_get();
    uint32_t bcr = cp14_bcr1_get();
    if(bit_is_off(bcr, 0)) // add comment for why this check here
        return 0;

    return cp14_bvr1_get();
}


// set mismatch on <addr>
//
// for simplicity: for matching we use breakpoint 0. 
// so set bcr0 and bvr0.
void brkpt_mismatch_set(uint32_t addr) {
    // staff_brkpt_mismatch_set(addr);
    // Set BVR0 with word-aligned address.
    cp14_bvr0_set(addr & ~0b11u);

    // Set BCR0: mismatch + enable + privilege + security config.
    uint32_t bcr = 0;
    bcr = bcr_common_enable(bcr);
    bcr = bcr_set_mismatch(bcr);

    cp14_bcr0_set(bcr);
}

// this will mismatch on the first instruction at user level.
void brkpt_mismatch_start(void) {
    // 1. check DSCR: if not enabled, enable it.
    // 2. set brkpt_mismatch_set(0)
    // staff_brkpt_mismatch_start();

    // 1) ensure DSCR enabled
    brkpt_match_init();

    // 2) set mismatch against 0 so the very first user-mode instruction triggers
    // (common trick in this lab)
    brkpt_mismatch_set(0);
}

// turn off mismatching: clear bcr0
void brkpt_mismatch_stop(void) {
    // staff_brkpt_mismatch_stop();
    cp14_bcr0_set(0);
}

// was this a breakpoint fault? (either mismatch or match)
// check IFSR bits (p 3-66) to see it was a debug fault.
// check DSCR bits (13-11) to see if it was a breakpoint
int brkpt_fault_p(void) {
    // return staff_brkpt_fault_p();

    // Check IFSR first (analogous to DFSR rule on p13-11).
    uint32_t ifsr = cp15_ifsr_get();

    if(bit_is_on(ifsr, 10)) // find the doc page for this, i know it was in the readme
        return 0;

    uint32_t fs = bits_get(ifsr, 0, 3);
    if(fs != IFSR_FS_DEBUG_EVENT)
        return 0;

    // Now check DSCR reason code (p13-11: DSCR[5:2]).
    uint32_t dscr = cp14_dscr_get();
    uint32_t cause = bits_get(dscr, 2, 5);

    return cause == DSCR_CAUSE_BREAKPOINT;
}
