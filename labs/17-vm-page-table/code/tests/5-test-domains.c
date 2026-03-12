#include "rpi.h"
#include "pt-vm.h"
#include "full-except.h"
#include "procmap.h"
#include "asm-helpers.h"
#include "armv6-coprocessor-asm.h"

#define MB(x) ((x)*1024*1024)

enum { 
    dom_test = 2   
};

cp_asm_get(cp15_fault_addr, p15, 0, c6, c0, 0)
cp_asm_get(dfsr, p15, 0, c5, c0, 0)
cp_asm_get(ifsr, p15, 0, c5, c0, 1)

// b4-44: Drain write buffer (Data Synchronization Barrier)
cp_asm_set(dsb, p15, 0, c7, c10, 4)

// b4-43: Invalidate Instruction Cache
cp_asm_set(inv_icache, p15, 0, c7, c5, 0)

// b4-45: Flush prefetch buffer (Instruction Synchronization Barrier)
cp_asm_set(prefetch_flush, p15, 0, c7, c5, 4)

static volatile uint32_t expected_fault_addr;
static volatile uint32_t expected_pc;
static volatile int fault_count = 0;

static void enable_test_domain(void) {
domain_access_ctrl_set((0b01 << (dom_kern*2)) | (0b01 << (dom_test*2)));
    mmu_sync_pte_mods();
    prefetch_flush_set(0);
}

static void disable_test_domain(void) {
    domain_access_ctrl_set((0b01 << (dom_kern*2)) | (0b00 << (dom_test*2)));
    mmu_sync_pte_mods();
    prefetch_flush_set(0);
}

static void data_abort_handler(regs_t *r) {
    uint32_t fault_addr = cp15_fault_addr_get();
    uint32_t reason = dfsr_get();

    trace("Data Abort Caught! pc=%x, fault_addr=%x, dfsr=%x\n", r->regs[15], fault_addr, reason);
    assert(r->regs[15] == expected_pc);
    assert(fault_addr == expected_fault_addr);
    
    fault_count++;
    enable_test_domain();
}

static void prefetch_abort_handler(regs_t *r) {
    uint32_t reason = ifsr_get();
    trace("Prefetch Abort Caught! pc=%x, ifsr=%x\n", r->regs[15], reason);
    
    // Check PC manually without asserting to avoid silent panics
    if(r->regs[15] != expected_pc) {
        trace("FATAL: PC mismatch! expected=%x, got=%x\n", expected_pc, r->regs[15]);
    }
    
    fault_count++;
    enable_test_domain();
}

void notmain(void) { 
    kmalloc_init_set_start((void*)MB(1), MB(1));

    // 1. Create default map and override the heap domain
    procmap_t p = procmap_default_mk(dom_kern);
    for(int i = 0; i < p.n; i++) {
        if(p.map[i].addr == MB(1)) { 
            p.map[i].dom = dom_test;
            p.dom_ids |= (1 << dom_test);
        }
    }

    // 2. Generate page table but do not enable MMU yet
    vm_pt_t *pt = vm_map_kernel(&p, 0);

    // 3. Override exception handlers
    full_except_set_data_abort(data_abort_handler);
    full_except_set_prefetch(prefetch_abort_handler);

    // 4. Set permissions and enable MMU
    enable_test_domain();
    vm_mmu_switch(pt, 0x140e, 1);
    vm_mmu_enable();
    
    trace("MMU Enabled. Starting domain exception tests...\n");

    // Use an address deep in the heap, far away from the allocated Page Table
    uint32_t safe_heap_addr = MB(1) + (1024 * 512); 

    // Test 1: Load (GET32)
    trace("\n--- Testing Load (GET32) ---\n");
    expected_fault_addr = safe_heap_addr;
    expected_pc = (uint32_t)GET32; 
    disable_test_domain();
    uint32_t val = GET32(safe_heap_addr); 
    assert(fault_count == 1);
    trace("Load test passed! (Read %x)\n", val);

    // Test 2: Store (PUT32)
    trace("\n--- Testing Store (PUT32) ---\n");
    expected_fault_addr = safe_heap_addr;
    expected_pc = (uint32_t)PUT32; 
    disable_test_domain();
    PUT32(safe_heap_addr, 0x12345678);
    assert(fault_count == 2);
    trace("Store test passed! (Read back %x)\n", GET32(safe_heap_addr));

    // Test 3: Jump (Prefetch Abort)
    trace("\n--- Testing Jump (Prefetch Abort) ---\n");
    uint32_t jump_addr = safe_heap_addr + 0x100;
    
    // Write the instruction to the data path
    PUT32(jump_addr, 0xe12fff1e); 
    
    // Clean caches/pipeline so CPU sees the instruction
    dsb_set(0);
    inv_icache_set(0);
    prefetch_flush_set(0);

    expected_pc = jump_addr;
    
    trace("  -> About to disable domain and jump...\n");
    disable_test_domain();
    
    void (*jump_fn)(void) = (void (*)(void))jump_addr;
    jump_fn();
    
    assert(fault_count == 3);
    trace("Jump test passed!\n");

    trace("\nSUCCESS! All domain exceptions caught and handled.\n");
    clean_reboot();
}