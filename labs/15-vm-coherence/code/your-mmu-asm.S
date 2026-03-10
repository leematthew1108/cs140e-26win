@ Must carefully read B2 of the armv6 manual.  You cannot rely on "well it
@ worked on my test case": mistakes cause stale data, which may or may not
@ show up in your test case (likely not) despite being wildly broken.
@
@ Note: Rd is allegedly a read-only parameter to these instructions, but
@ the manual says SBZ (should be zero).  I think (and the linux source seems
@ to think) that this means we have to explicitly set it to 0.  Not setting
@ Rd=0 is an easy mistake to make.  Perhaps is worth building in (Linux does
@ not, but) --- the cost of an extra mov is negligible given our other 
@ overheads.
@
@ Alot of people put these instructions in inline assembly, wrapped up by 
@ function calls (e.g., cs107e's MMU code).  This practice is dangerous. 
@ For example, if you are flushing caches, you cannot be sure that the 
@ function return, etc does not then reload them, esp w.r.t. the BTB.  I 
@ think the only safe approach is to put directly in assembly so you are 
@ guaranteed no branch, indirect jump, load/store happens in between.
@
@ A lot of MMU code out there appears pretty broken b/c they don't do enough
@ flushing operations and/or rely on gcc code generation to not do the above.

#include "rpi-asm.h"
#include "armv6-coprocessor-asm.h"


@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ many page numbers are in 
@    armv6-coprocessor-asm.h
@ and the other armv6-*.h files

// Read CP15 Control Register 1 and return it.
// B4-40: mrc p15, 0, Rd, c1, c0, 0
MK_FN(cp15_ctrl_reg1_rd)
    mrc p15, 0, r0, c1, c0, 0  @ Read Control Register 1 into r0.
    bx lr                      @ Return. r0 holds the 32-bit return value.

// Write CP15 Control Register 1
// B4-40: mcr p15, 0, Rd, c1, c0, 0
MK_FN(cp15_ctrl_reg1_wr)
    mcr p15, 0, r0, c1, c0, 0
    bx lr

// just read the cache type: use C code to figure out if it is unified
// b6-13, b6-14
MK_FN(get_cache_type)
    // asm_not_implemented()

    // B6-13: Read Cache Type Register into r0
    mrc p15, 0, r0, c0, c0, 1

    bx lr

// void reset(void)
// clear the dcache, icache, tlbs
MK_FN(mmu_reset)
    //b staff_mmu_reset

    //asm_not_implemented()

    // Rd should be zero (staff comments at the top)
    mov r0, #0

    // 1. Invalidate caches (Instruction and Data)
    // We strictly INV and do NOT clean (which would write garbage back to memory).
    // B6-21 / armv6-coprocessor-asm.h
    INV_ALL_CACHES(r0)

    // 2. Invalidate TLBs (Instruction and Data)
    // B4-45 / armv6-coprocessor-asm.h
    INV_TLB(r0)

    // 3. Guarantee Completion
    // B2-22: DSB ensures completion of the cache and TLB maintenance operations 
    // before any subsequent instructions execute.
    DSB(r0)

    // 4. Guarantee Visibility
    // B2-22 / B2.7.3: PrefetchFlush causes all completed maintenance operations 
    // to become visible to the instruction fetcher.
    PREFETCH_FLUSH(r0)

    bx lr

// void cp15_domain_ctrl_wr(uint32_t dom_reg);
// NOTE:
//   - definitely need to do prefetch flush 
//   - I don't *think* we need the full cp15 sync: flush_btb, 
//     dsb, prefetch flush.
MK_FN(cp15_domain_ctrl_wr)
    @ b staff_cp15_domain_ctrl_wr
    @ asm_not_implemented()

    // B4-42: Write to Domain Access Control Register
    mcr p15, 0, r0, c3, c0, 0
    
    // B2-24: Changing domain permissions requires flushing the Prefetch Buffer
    mov r1, #0
    PREFETCH_FLUSH(r1)

    bx lr

// void cp15_set_procid_ttbr0(uint32_t proc_and_asid, fld_t *pt);
// sequence from b2-25
// Need to both update ASID and TTBR (translation table base register) without corruption
// r0 = proc_and_asid
// r1 = pt (page table pointer)
MK_FN(cp15_set_procid_ttbr0)
    @ b staff_cp15_set_procid_ttbr0

    @ asm_not_implemented()

    mov r2, #0

    // 0. DSB before Context ID change (Page 3-129)
    // make sure all memory accesses finish before changing asid to 0
    DSB(r2)

    // 1. Change ASID to 0 (Page B2-25)
    // this is the "ASID 0 hack"
    // switch to reserved ASID 0 to avoid polluting TLB during transition
    mcr p15, 0, r2, c13, c0, 1 

    // 2. PrefetchFlush (Page B2-25)
    // so that subsequent fetches don't use old ASID
    PREFETCH_FLUSH(r2)

    // 3. Clear TTBCR and TTBR1 to force using TTBR0 (Page B4-41) 
    // TTBCR to 0 means all translations are in TTBR0
    mcr p15, 0, r2, c2, c0, 2  @ TTBCR = 0
    mcr p15, 0, r2, c2, c0, 1  @ TTBR1 = 0

    // 4. Change Translation Table Base Register to pt in r1 (Page B2-25) 
    // r1 is the page table pointer passed into the function
    mcr p15, 0, r1, c2, c0, 0

    // 5. PrefetchFlush (Page B2-25) 
    // so that fetches use the new page table mapping
    PREFETCH_FLUSH(r2)

    // 6. Change ASID to new value in r0 (Page B2-25) 
    // r0 holds the proc_and_asid
    mcr p15, 0, r0, c13, c0, 1

    // 7. Flush BTB after changing TTBR and ASID (Page B2-24) 
    // cached branches rely on virtual addresses, need to invalidate after changing
    FLUSH_BTB(r2)

    // 8. IMB immediately after Context ID change (Page 3-129 and B2-24) 
    // need to do both DSB and prefetch flush
    DSB(r2)
    PREFETCH_FLUSH(r2)

    bx lr

// void mmu_disable_set_asm(cp15_ctrl_reg1_t c);
// additional detail for disable in B4-6 (armv6.pdf)
MK_FN(mmu_disable_set_asm)
    // b staff_mmu_disable_set_asm

    @ note: this piece of code is mapped into the same place in both
    @ MMU-enabled and MMU-disabled so BTB and Prefetch entries work identically
    @ to both MMU-enable and disable.   Without this requirement I believe is
    @ very hard to get right (perhaps impossible?) since we cannot flush
    @ the BTB and prefetch and switch MMU on/off atomically.  thus, would
    @ flush, but instructions could get fetched before we could disable,
    @ defeating the purpose.

    // asm_not_implemented()

    // r0 contains the CP15 Control Register 1 value to write
    mov r1, #0

    // 1. Clean and invalidate the data cache
    // Section 6.4.2 (Disable sequence, step 1): Must clean D-cache before MMU turns off so dirty entries write back to memory.
    CLEAN_INV_DCACHE(r1)
    
    // 2. Invalidate instruction cache
    // Lab test requirement for enable/disable symmetry
    INV_ICACHE(r1)

    // 3. Guarantee completion
    // B2-22: DSB ensures all cache maintenance finishes before MMU disables
    DSB(r1)

    // 4. Turn off the MMU
    // Section 6.4.2 (Disable sequence, step 2): Write r0 to Control Register 1
    mcr p15, 0, r0, c1, c0, 0

    // 5. Flush BTB
    // B2.7.5: Changing MMU state requires invalidating the Branch Target Buffer
    FLUSH_BTB(r1)

    // 6. Guarantee completion and visibility
    // B2.7.6 / B2-24: PrefetchFlush makes CP15 changes and BTB flush visible to instruction fetch
    DSB(r1)
    PREFETCH_FLUSH(r1)

    bx lr

// void mmu_enable_set_asm(cp15_ctrl_reg1_t c);
// high level sequence given in 6-9 of arm1176
MK_FN(mmu_enable_set_asm)
    @ b staff_mmu_enable_set_asm

    @ asm_not_implemented()

    // r0 contains the CP15 Control Register 1 value to write
    mov r1, #0

    // 1. Invalidate instruction cache
    // Section 6.4.1 (Enable sequence, step 3) & lab test requirement (refer to readme part 3)
    INV_ICACHE(r1)
    
    // 2. Guarantee completion
    // B2-22: DSB ensures cache invalidation completes
    DSB(r1)

    // 3. Turn on the MMU
    // Section 6.4.1 (Enable sequence, step 4): Write r0 to Control Register 1
    mcr p15, 0, r0, c1, c0, 0

    // 4. Flush BTB
    // B2.7.5: Changing MMU state requires invalidating the Branch Target Buffer
    FLUSH_BTB(r1)

    // 5. Guarantee completion and visibility
    // B2.7.6 / B2-24: PrefetchFlush makes CP15 changes and BTB flush visible to instruction fetch
    DSB(r1)
    PREFETCH_FLUSH(r1)

    bx lr

MK_FN(mmu_sync_pte_mods)
    @ b staff_mmu_sync_pte_mods

    @ asm_not_implemented()

    mov r0, #0

    // 1. Clean Data Cache
    // B2-23: Forces the updated PTE from the cache out to physical memory
    // so the hardware page table walker can see it.
    CLEAN_INV_DCACHE(r0)

    // 2. Guarantee visibility
    // B2-23: DSB ensures the D-cache clean is complete and visible.
    DSB(r0)

    // 3. Invalidate TLB
    // B2-23: Wipes the old, stale address translation.
    INV_TLB(r0)

    // 4. Invalidate Instruction Cache
    // Lab requirement: "be conservative and invalidate the icache too"
    INV_ICACHE(r0)

    // 5. Invalidate BTB
    // B2-23: Clears branch predictions that relied on the old memory mapping.
    FLUSH_BTB(r0)

    // 6. Guarantee completion
    // B2-23: DSB ensures the TLB, I-cache, and BTB invalidations are fully complete.
    DSB(r0)

    // 7. Flush pipeline
    // B2-23: PrefetchFlush forces the CPU to fetch subsequent instructions 
    // using the new memory mapping.
    PREFETCH_FLUSH(r0)

    bx lr

