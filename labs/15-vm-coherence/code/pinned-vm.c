// put your code here.
//
#include "rpi.h"
#include "libc/bit-support.h"

// has useful enums and helpers.
#include "vector-base.h"
#include "pinned-vm.h"
#include "mmu.h"
#include "procmap.h"

// generate the _get and _set methods.
// (see asm-helpers.h for the cp_asm macro 
// definition)
// arm1176.pdf: 3-149

// Generate the _get and _set methods for CP15 TLB Lockdown registers.
// From 3-153 in arm1176 pdf: Opcode_1=5, CRn=c15, Opcode_2=2
cp_asm_set(lockdown_index, p15, 5, c15, c4, 2)
cp_asm_get(lockdown_index, p15, 5, c15, c4, 2)

cp_asm_set(lockdown_va, p15, 5, c15, c5, 2)
cp_asm_get(lockdown_va, p15, 5, c15, c5, 2)

cp_asm_set(lockdown_pa, p15, 5, c15, c6, 2)
cp_asm_get(lockdown_pa, p15, 5, c15, c6, 2)

cp_asm_set(lockdown_attr, p15, 5, c15, c7, 2)
cp_asm_get(lockdown_attr, p15, 5, c15, c7, 2)

// B4-42: Domain Access Control Register (c3)
cp_asm_set(domain_ctrl, p15, 0, c3, c0, 0)

// 3-79: VA to PA translation (Privileged Read)
// more info on 3-82
cp_asm_set(va_to_pa_priv_read, p15, 0, c7, c8, 0)

// 3-79: Physical Address Register (PAR) to get the translation result
cp_asm_get(pa_reg, p15, 0, c7, c4, 0)

static void *null_pt = 0;

// // should we have a pinned version?
// void domain_access_ctrl_set(uint32_t d) {
//     staff_domain_access_ctrl_set(d);
// }

// fill this in based on the <1-test-basic-tutorial.c>
// NOTE: 
//    you'll need to allocate an invalid page table
void pin_mmu_init(uint32_t domain_reg) {
    // staff_pin_mmu_init(domain_reg);

    mmu_init();

    // from 1-test-basic-tutorial.c
    if(null_pt == 0) {
        null_pt = kmalloc_aligned(4096 * 4, 1 << 14);
    }

    domain_access_ctrl_set(domain_reg);
}

// do a manual translation in tlb:
//   1. store result in <result>
//   2. return 1 if entry exists, 0 otherwise.
//
// NOTE: mmu must be on (confusing).
int tlb_contains_va(uint32_t *result, uint32_t va) {
    assert(mmu_is_enabled());

    // 3-79
    assert(bits_get(va, 0,2) == 0);
    // return staff_tlb_contains_va(result, va);

    // trigger translation in current mode as a "privileged read"
    va_to_pa_priv_read_set(va);

    // get the result from the PAR first
    uint32_t par = pa_reg_get();

    // Check the fault bit (Bit 0 of the raw PAR). 
    // If it is 1, the translation failed.
    if (bit_is_on(par, 0)) {
        return 0; 
    }
    
    // If successful, mask out the attributes and combine with the VA offset
    *result = bits_clr(par, 0, 11) | bits_get(va, 0, 11);
    
    return 1;
}

// map <va>-><pa> at TLB index <idx> with attributes <e>
void pin_mmu_sec(unsigned idx,  
                uint32_t va, 
                uint32_t pa,
                pin_t e) {


    demand(idx < 8, lockdown index too large);
    // lower 20 bits should be 0.
    demand(bits_get(va, 0, 19) == 0, only handling 1MB sections);
    demand(bits_get(pa, 0, 19) == 0, only handling 1MB sections);

    debug("about to map %x->%x\n", va,pa);


    // delete this and do add your code below.
    // staff_pin_mmu_sec(idx, va, pa, e);
    // return;

    // these will hold the values you assign for the tlb entries.
    uint32_t x, va_ent, pa_ent, attr;
    // todo("assign these variables!\n");

    // put your code here.
    // unimplemented();

    // 1. Construct VA Entry (Register c5)
    // Bits [31:20] are the VA. We can just start with 'va' since the lower 20 bits are 0.
    va_ent = va;
    va_ent = bits_set(va_ent, 9, 9, e.G);     // Bit 9: Global bit
    va_ent = bits_set(va_ent, 0, 7, e.asid);  // Bits [7:0]: ASID

    // 2. Construct PA Entry (Register c6)
    // Bits [31:20] are the PA. Start with 'pa'.
    pa_ent = pa;
    pa_ent = bits_set(pa_ent, 6, 7, e.pagesize);   // Bits [7:6]: Size
    pa_ent = bits_set(pa_ent, 1, 3, e.AP_perm);   // Bits [3:1]: APX and AP 
    pa_ent = bit_set(pa_ent, 0);                  // Bit 0: Valid bit (set to 1)

    // 3. Construct Attributes Entry (Register c7)
    attr = 0;
    attr = bits_set(attr, 7, 10, e.dom);          // Bits [10:7]: Domain
    attr = bits_set(attr, 1, 5, e.mem_attr);      // Bits [5:1]: TEX, C, B

    // 4. Write to the registers
    lockdown_index_set(idx);
    lockdown_va_set(va_ent);
    lockdown_attr_set(attr); // do this before pa set
    lockdown_pa_set(pa_ent);

    // verification prints:
#if 1 
    if((x = lockdown_va_get()) != va_ent)
        panic("lockdown va: expected %x, have %x\n", va_ent,x);
    if((x = lockdown_pa_get()) != pa_ent)
        panic("lockdown pa: expected %x, have %x\n", pa_ent,x);
    if((x = lockdown_attr_get()) != attr)
        panic("lockdown attr: expected %x, have %x\n", attr,x);
#endif
}

// check that <va> is pinned.  
int pin_exists(uint32_t va, int verbose_p) {
    if(!mmu_is_enabled())
        panic("XXX: i think we can only check existence w/ mmu enabled\n");

    uint32_t r;
    if(tlb_contains_va(&r, va)) {
        assert(va == r);
        return 1;
    } else {
        if(verbose_p) 
            output("TLB should have %x: returned %x [reason=%b]\n", 
                va, r, bits_get(r,1,6));
        return 0;
    }
}

// look in test <1-test-basic.c> to see what to do.
// need to set the <asid> before turning VM on and 
// to switch processes.
void pin_set_context(uint32_t asid) {
    // put these back
    demand(asid > 0 && asid < 64, invalid asid);
    demand(null_pt, must setup null_pt --- look at tests);

    //staff_pin_set_context(asid);

    // Set address space id, page table, and pid.
    // hardcode PID to 0. (why?)
    mmu_set_ctx(0, asid, null_pt);
}

void pin_clear(unsigned idx)  {
    //staff_pin_clear(idx);
    
    // Set all all three registers at TLB idx to 0.
    lockdown_index_set(idx);
    lockdown_va_set(0);
    lockdown_attr_set(0);
    lockdown_pa_set(0);

    // Verify they are cleared (masking bit 0 to just check the valid bit)
    demand(bit_is_off(lockdown_pa_get(), 0), failed to clear pa valid bit);
}

void lockdown_print_entry(unsigned idx) {
    // panic("make sure to use <trace_nofn> not <trace>\n");
   trace_nofn("   idx=%d\n", idx);
    
    // Select the TLB entry to read
    lockdown_index_set(idx);
    
    // Read the raw register values
    uint32_t va_ent = lockdown_va_get();
    uint32_t pa_ent = lockdown_pa_get();
    uint32_t attr   = lockdown_attr_get();
    
    // Valid bit is bit 0 of the PA register
    unsigned v = bit_get(pa_ent, 0);

    if(!v) {
        trace_nofn("     \n"); // Print a blank line instead of [invalid entry] to match staff output
        return;
    }

    // --- 3-149: VA Register ---
    // The staff output indicates a shift by 12, not 20.
    uint32_t va   = va_ent >> 12; 
    uint32_t G    = bit_get(va_ent, 9);
    uint32_t asid = bits_get(va_ent, 0, 7);
    
    trace_nofn("     va_ent=%x: va=%x|G=%d|ASID=%d\n", va_ent, va, G, asid);

    // --- 3-150: PA Register ---
    // The staff output indicates a shift by 12, not 20.
    uint32_t pa    = pa_ent >> 12; 
    uint32_t nsa   = bit_get(pa_ent, 9);
    uint32_t nstid = bit_get(pa_ent, 11);
    uint32_t size  = bits_get(pa_ent, 6, 7); //
    
    // The README notes APX and AP are combined into 3 bits.
    uint32_t apx   = bits_get(pa_ent, 1, 3); 
    
    trace_nofn("     pa_ent=%x: pa=%x|nsa=%d|nstid=%d|size=%b|apx=%b|v=%d\n", pa_ent, pa, nsa, nstid, size, apx, v);

    // --- 3-151: Attributes Register ---
    // Extracting fields according to Figure 3-79
    uint32_t dom = bits_get(attr, 7, 10);
    uint32_t xn  = bit_get(attr, 6);
    uint32_t tex = bits_get(attr, 3, 5);
    uint32_t C   = bit_get(attr, 2);
    uint32_t B   = bit_get(attr, 1);
    
    trace_nofn("     attr=%x: dom=%d|xn=%d|tex=%b|C=%d|B=%d\n", attr, dom, xn, tex, C, B);
}

void lockdown_print_entries(const char *msg) {
    //staff_lockdown_print_entries(msg);
#if 1
    trace_nofn("-----  <%s> ----- \n", msg);
    trace_nofn("  pinned TLB lockdown entries:\n");

    for(int i = 0; i < 8; i++)
        lockdown_print_entry(i);
    trace_nofn("----- ---------------------------------- \n");
#endif
}

