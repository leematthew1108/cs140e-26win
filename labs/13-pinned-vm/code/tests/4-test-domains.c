#include "rpi.h"
#include "pinned-vm.h"
#include "full-except.h"
#include "memmap.h"
#include "asm-helpers.h"

#define MB(x) ((x)*1024*1024)

// Standard segment mappings
enum {
    SEG_CODE = MB(0),
    SEG_HEAP = MB(1),
    SEG_STACK = STACK_ADDR - MB(1),
    SEG_INT_STACK = INT_STACK_ADDR - MB(1),
    SEG_BCM_0 = 0x20000000,
    SEG_BCM_1 = SEG_BCM_0 + MB(1),
    SEG_BCM_2 = SEG_BCM_0 + MB(2),
};

// Domain IDs
enum { 
    dom_kern = 1, 
    dom_test = 2   // Domain ID 'd' exclusively for our heap testing
};

// --- CP15 Fault Registers ---
// b4-44: Fault Address Register
cp_asm_get(cp15_fault_addr, p15, 0, c6, c0, 0)
// b4-43: Data Fault Status Register (DFSR)
cp_asm_get(dfsr, p15, 0, c5, c0, 0)
// IFSR from
cp_asm_get(ifsr, p15, 0, c5, c0, 1)

// --- Test State Variables ---
static volatile uint32_t expected_fault_addr;
static volatile uint32_t expected_pc;
static volatile int fault_count = 0;

// Helper to quickly restore permissions
static void enable_test_domain(void) {
    // Both kernel and test domain become clients (0b01)
    staff_domain_access_ctrl_set((DOM_client << (dom_kern*2)) | (DOM_client << (dom_test*2)));
}

// Helper to remove permissions for the test domain
static void disable_test_domain(void) {
    // Kernel remains client, test domain becomes no-access (0b00)
    staff_domain_access_ctrl_set((DOM_client << (dom_kern*2)) | (0b00 << (dom_test*2)));
}

// --- Exception Handlers ---
static void data_abort_handler(regs_t *r) {
    uint32_t fault_addr = cp15_fault_addr_get();
    uint32_t reason = dfsr_get();

    // PC is regs[15]
    trace("Data Abort Caught! pc=%x, fault_addr=%x, dfsr (reason)=%x\n", r->regs[15], fault_addr, reason);

    // Verify PC matches GET32 or PUT32
    assert(r->regs[15] == expected_pc);
    assert(fault_addr == expected_fault_addr);

    fault_count++;
    
    // Re-enable permissions so the instruction succeeds when we return
    enable_test_domain();
}

static void prefetch_abort_handler(regs_t *r) {
    uint32_t reason = ifsr_get();

    trace("Prefetch Abort Caught! pc=%x, ifsr (reason)=%x\n", r->regs[15], reason);

    // Verify PC matches the heap address we jumped to
    assert(r->regs[15] == expected_pc);

    fault_count++;
    
    // Re-enable permissions so the instruction can actually execute
    enable_test_domain();
}

void notmain(void) { 
    kmalloc_init_set_start((void*)SEG_HEAP, MB(1));

    // Install handlers for both data (load/store) and prefetch (jump) aborts
    full_except_install(0);
    full_except_set_data_abort(data_abort_handler);
    full_except_set_prefetch(prefetch_abort_handler);

    // Create the invalid page table
    void *null_pt = kmalloc_aligned(4096*4, 1<<14);
    
    // Define memory attributes
    pin_t dev  = pin_mk_global(dom_kern, perm_rw_priv, MEM_device);
    pin_t kern = pin_mk_global(dom_kern, perm_rw_priv, MEM_uncached);
    
    // Tag the HEAP with its own domain ID (dom_test)
    pin_t heap = pin_mk_global(dom_test, perm_rw_priv, MEM_uncached);

    // Map segments
    pin_mmu_sec(0, SEG_CODE, SEG_CODE, kern);
    pin_mmu_sec(1, SEG_HEAP, SEG_HEAP, heap); // Tagged with dom_test
    pin_mmu_sec(2, SEG_STACK, SEG_STACK, kern); 
    pin_mmu_sec(3, SEG_INT_STACK, SEG_INT_STACK, kern); 
    pin_mmu_sec(4, SEG_BCM_0, SEG_BCM_0, dev); 
    pin_mmu_sec(5, SEG_BCM_1, SEG_BCM_1, dev); 
    pin_mmu_sec(6, SEG_BCM_2, SEG_BCM_2, dev);

    // Setup context and turn on MMU
    enable_test_domain();
    staff_mmu_set_ctx(0, 1, null_pt);
    staff_mmu_enable();
    trace("MMU Enabled. Starting domain exception tests...\n");

    // ==========================================
    // Test 1: Load (GET32)
    // ==========================================
    trace("\n--- Testing Load (GET32) ---\n");
    expected_fault_addr = SEG_HEAP;
    expected_pc = (uint32_t)GET32; 
    
    disable_test_domain();
    uint32_t val = GET32(SEG_HEAP); 
    assert(fault_count == 1);
    trace("Load test passed! (Read %x)\n", val);

    // ==========================================
    // Test 2: Store (PUT32)
    // ==========================================
    trace("\n--- Testing Store (PUT32) ---\n");
    expected_fault_addr = SEG_HEAP;
    expected_pc = (uint32_t)PUT32; 
    
    disable_test_domain();
    PUT32(SEG_HEAP, 0x12345678);
    assert(fault_count == 2);
    trace("Store test passed! (Read back %x)\n", GET32(SEG_HEAP));

    // ==========================================
    // Test 3: Jump (Prefetch Abort)
    // ==========================================
    trace("\n--- Testing Jump (Prefetch Abort) ---\n");
    uint32_t jump_addr = SEG_HEAP + 0x100;
    
    // Write the ARM encoding for 'bx lr' (0xe12fff1e) into the heap location
    PUT32(jump_addr, 0xe12fff1e);
    
    expected_pc = jump_addr;
    
    disable_test_domain();
    BRANCHTO(jump_addr);
    assert(fault_count == 3);
    trace("Jump test passed!\n");

    // ==========================================
    trace("\nSUCCESS! All domain exceptions caught and handled.\n");
    clean_reboot();
}