#include "rpi.h"
#include "pt-vm.h"
#include "procmap.h"

void notmain(void) {
    // Setup default OS map and turn on MMU
    procmap_t p = procmap_default_mk(dom_kern);
    vm_pt_t *pt = vm_map_kernel(&p, 1);
    
    trace("MMU enabled. Testing address translations...\n");

    // Test offsets inside the 1MB heap section (starts at 0x100000)
    uint32_t heap_base = 1024 * 1024;
    
    // Check translation at 4KB intervals across the entire 1MB section
    for (uint32_t offset = 0; offset < 1024 * 1024; offset += 4096) {
        uint32_t va = heap_base + offset;
        uint32_t pa = 0;
        
        vm_pte_t *pte = vm_xlate(&pa, pt, va);
        assert(pte);
        
        // For identity mapping, VA must exactly equal PA at all offsets
        if (va != pa) {
            panic("Translation failed: va=0x%x, pa=0x%x\n", va, pa);
        }
    }
    
    trace("SUCCESS: Identity mapping offsets resolve correctly (VA == PA).\n");
    clean_reboot();
}