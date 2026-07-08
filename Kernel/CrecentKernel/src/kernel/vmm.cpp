#include "vmm.hpp"
#include "pmm.hpp"

namespace kernel {

static uint64_t active_pml4 = 0;

void vmm_init() {
    uint64_t cr3_val;
    // Read CR3 control register
    __asm__ __volatile__ ("mov %%cr3, %0" : "=r"(cr3_val));
    // Clear lower 12 flags bits to retrieve physical base address of active PML4
    active_pml4 = cr3_val & ~0xFFFUL;
}

bool vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if ((virt & 0xFFFUL) || (phys & 0xFFFUL)) {
        return false; // Virtual and physical addresses must be 4KB aligned
    }

    uint64_t* pml4 = (uint64_t*)active_pml4;
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx   = (virt >> 21) & 0x1FF;
    size_t pt_idx   = (virt >> 12) & 0x1FF;

    // 1. PML4 -> PDPT
    if (!(pml4[pml4_idx] & VMM_FLAG_PRESENT)) {
        uint64_t new_pdpt = pmm_alloc_frame();
        if (!new_pdpt) return false;
        // Link PML4 entry to the new page directory pointer table with present + writable flags
        pml4[pml4_idx] = new_pdpt | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
    }
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFUL);

    // 2. PDPT -> PD
    if (!(pdpt[pdpt_idx] & VMM_FLAG_PRESENT)) {
        uint64_t new_pd = pmm_alloc_frame();
        if (!new_pd) return false;
        pdpt[pdpt_idx] = new_pd | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
    }
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFUL);

    // 3. PD -> PT
    if (!(pd[pd_idx] & VMM_FLAG_PRESENT)) {
        uint64_t new_pt = pmm_alloc_frame();
        if (!new_pt) return false;
        pd[pd_idx] = new_pt | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
    }
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFUL);

    // 4. PT -> Physical Page
    pt[pt_idx] = phys | flags;

    // Invalidate Translation Lookaside Buffer (TLB) entry for this virtual address
    __asm__ __volatile__ ("invlpg (%0)" : : "r"(virt) : "memory");

    return true;
}

bool vmm_unmap_page(uint64_t virt) {
    if (virt & 0xFFFUL) {
        return false;
    }

    uint64_t* pml4 = (uint64_t*)active_pml4;
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx   = (virt >> 21) & 0x1FF;
    size_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_FLAG_PRESENT)) return false;
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFUL);

    if (!(pdpt[pdpt_idx] & VMM_FLAG_PRESENT)) return false;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFUL);

    if (!(pd[pd_idx] & VMM_FLAG_PRESENT)) return false;
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFUL);

    if (!(pt[pt_idx] & VMM_FLAG_PRESENT)) return false;

    // Clear mapping entry
    pt[pt_idx] = 0;

    // Flush TLB entry
    __asm__ __volatile__ ("invlpg (%0)" : : "r"(virt) : "memory");

    return true;
}

bool vmm_is_mapped(uint64_t virt) {
    uint64_t* pml4 = (uint64_t*)active_pml4;
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx   = (virt >> 21) & 0x1FF;
    size_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_FLAG_PRESENT)) return false;
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFUL);

    if (!(pdpt[pdpt_idx] & VMM_FLAG_PRESENT)) return false;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFUL);

    if (!(pd[pd_idx] & VMM_FLAG_PRESENT)) return false;
    // Check if it is a 2MB huge page (which we configured in GDT/Bootstrap tables)
    if (pd[pd_idx] & VMM_FLAG_HUGE) return true;

    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFUL);
    return (pt[pt_idx] & VMM_FLAG_PRESENT) != 0;
}

} // namespace kernel
