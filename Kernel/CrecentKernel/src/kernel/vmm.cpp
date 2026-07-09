#include "vmm.hpp"
#include "pmm.hpp"

namespace kernel {

constexpr uint64_t VMM_PHYS_ADDR_MASK = 0x000FFFFFFFFFF000ULL;

uint64_t active_pml4 = 0;

void vmm_init() {
    uint64_t cr3_val;
    // Read CR3 control register
    __asm__ __volatile__ ("mov %%cr3, %0" : "=r"(cr3_val));
    // Clear lower 12 flags bits to retrieve physical base address of active PML4
    active_pml4 = cr3_val & ~0xFFFULL;

    // Set USER privilege flag on PML4 entry 0 to allow Ring 3 privilege resolution under 512GB
    phys_to_virt<uint64_t>(active_pml4)[0] |= VMM_FLAG_USER;
}

bool vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    return vmm_map_page_in_pml4(active_pml4, virt, phys, flags);
}

bool vmm_unmap_page(uint64_t virt) {
    if (virt & 0xFFFULL) {
        return false;
    }

    uint64_t* pml4 = phys_to_virt<uint64_t>(active_pml4);
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx   = (virt >> 21) & 0x1FF;
    size_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_FLAG_PRESENT)) return false;
    uint64_t* pdpt = phys_to_virt<uint64_t>(pml4[pml4_idx] & VMM_PHYS_ADDR_MASK);

    if (!(pdpt[pdpt_idx] & VMM_FLAG_PRESENT)) return false;
    uint64_t* pd = phys_to_virt<uint64_t>(pdpt[pdpt_idx] & VMM_PHYS_ADDR_MASK);

    if (!(pd[pd_idx] & VMM_FLAG_PRESENT)) return false;
    uint64_t* pt = phys_to_virt<uint64_t>(pd[pd_idx] & VMM_PHYS_ADDR_MASK);

    if (!(pt[pt_idx] & VMM_FLAG_PRESENT)) return false;

    // Clear mapping entry
    pt[pt_idx] = 0;

    // Flush TLB entry
    __asm__ __volatile__ ("invlpg (%0)" : : "r"(virt) : "memory");

    return true;
}

bool vmm_is_mapped(uint64_t virt) {
    uint64_t* pml4 = phys_to_virt<uint64_t>(active_pml4);
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx   = (virt >> 21) & 0x1FF;
    size_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_FLAG_PRESENT)) return false;
    uint64_t* pdpt = phys_to_virt<uint64_t>(pml4[pml4_idx] & VMM_PHYS_ADDR_MASK);

    if (!(pdpt[pdpt_idx] & VMM_FLAG_PRESENT)) return false;
    uint64_t* pd = phys_to_virt<uint64_t>(pdpt[pdpt_idx] & VMM_PHYS_ADDR_MASK);

    if (!(pd[pd_idx] & VMM_FLAG_PRESENT)) return false;
    // Check if it is a 2MB huge page (which we configured in GDT/Bootstrap tables)
    if (pd[pd_idx] & VMM_FLAG_HUGE) return true;

    uint64_t* pt = phys_to_virt<uint64_t>(pd[pd_idx] & VMM_PHYS_ADDR_MASK);
    return (pt[pt_idx] & VMM_FLAG_PRESENT) != 0;
}

uint64_t vmm_get_phys(uint64_t virt) {
    uint64_t* pml4 = phys_to_virt<uint64_t>(active_pml4);
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx   = (virt >> 21) & 0x1FF;
    size_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_FLAG_PRESENT)) return 0;
    uint64_t* pdpt = phys_to_virt<uint64_t>(pml4[pml4_idx] & VMM_PHYS_ADDR_MASK);

    if (!(pdpt[pdpt_idx] & VMM_FLAG_PRESENT)) return 0;
    uint64_t* pd = phys_to_virt<uint64_t>(pdpt[pdpt_idx] & VMM_PHYS_ADDR_MASK);

    if (!(pd[pd_idx] & VMM_FLAG_PRESENT)) return 0;
    if (pd[pd_idx] & VMM_FLAG_HUGE) {
        return (pd[pd_idx] & ~0x1FFFFFULL) + (virt & 0x1FFFFFULL);
    }

    uint64_t* pt = phys_to_virt<uint64_t>(pd[pd_idx] & VMM_PHYS_ADDR_MASK);
    if (!(pt[pt_idx] & VMM_FLAG_PRESENT)) return 0;

    return (pt[pt_idx] & VMM_PHYS_ADDR_MASK) + (virt & 0xFFFULL);
}

bool vmm_is_user_mapped(uint64_t virt) {
    uint64_t* pml4 = phys_to_virt<uint64_t>(active_pml4);
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx   = (virt >> 21) & 0x1FF;
    size_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_FLAG_PRESENT)) return false;
    if (!(pml4[pml4_idx] & VMM_FLAG_USER)) return false;

    uint64_t* pdpt = phys_to_virt<uint64_t>(pml4[pml4_idx] & VMM_PHYS_ADDR_MASK);
    if (!(pdpt[pdpt_idx] & VMM_FLAG_PRESENT)) return false;
    if (!(pdpt[pdpt_idx] & VMM_FLAG_USER)) return false;

    uint64_t* pd = phys_to_virt<uint64_t>(pdpt[pdpt_idx] & VMM_PHYS_ADDR_MASK);
    if (!(pd[pd_idx] & VMM_FLAG_PRESENT)) return false;
    if (!(pd[pd_idx] & VMM_FLAG_USER)) return false;

    if (pd[pd_idx] & VMM_FLAG_HUGE) return true;

    uint64_t* pt = phys_to_virt<uint64_t>(pd[pd_idx] & VMM_PHYS_ADDR_MASK);
    if (!(pt[pt_idx] & VMM_FLAG_PRESENT)) return false;
    return (pt[pt_idx] & VMM_FLAG_USER) != 0;
}

void vmm_copy_kernel_identity_mappings(uint64_t child_pml4) {
    uint64_t* parent_pml4_ptr = phys_to_virt<uint64_t>(active_pml4);
    uint64_t* child_pml4_ptr = phys_to_virt<uint64_t>(child_pml4);
    
    if (!(parent_pml4_ptr[0] & VMM_FLAG_PRESENT)) return;
    
    // Allocate child's PDPT for entry 0 if not present
    if (!(child_pml4_ptr[0] & VMM_FLAG_PRESENT)) {
        uint64_t child_pdpt_phys = pmm_alloc_frame();
        for (int i = 0; i < 512; i++) phys_to_virt<uint64_t>(child_pdpt_phys)[i] = 0;
        child_pml4_ptr[0] = child_pdpt_phys | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
    }
    
    uint64_t* parent_pdpt = phys_to_virt<uint64_t>(parent_pml4_ptr[0] & VMM_PHYS_ADDR_MASK);
    uint64_t* child_pdpt = phys_to_virt<uint64_t>(child_pml4_ptr[0] & VMM_PHYS_ADDR_MASK);
    
    // Copy the kernel space PDPT entries (0 to 15, representing 0 to 16GB)
    // This maps all kernel code, stacks, heap, and physical framebuffers.
    // Index 16 (16GB) onwards is dedicated to user space mappings, cloned separately.
    for (int i = 0; i < 16; i++) {
        child_pdpt[i] = parent_pdpt[i];
    }
}

uint64_t vmm_create_user_address_space() {
    uint64_t child_pml4 = pmm_alloc_frame();
    if (!child_pml4) return 0;
    
    uint64_t* dest = phys_to_virt<uint64_t>(child_pml4);
    uint64_t* src = phys_to_virt<uint64_t>(active_pml4);
    
    // Clear user space mappings (PML4 entries 0 to 255)
    for (int i = 0; i < 256; i++) {
        dest[i] = 0;
    }
    
    // Copy kernel mappings (PML4 entries 256 to 511)
    for (int i = 256; i < 512; i++) {
        dest[i] = src[i];
    }
    
    // Copy kernel identity mappings in PML4 entry 0
    vmm_copy_kernel_identity_mappings(child_pml4);
    
    return child_pml4;
}

void vmm_destroy_user_address_space(uint64_t pml4_phys) {
    if (!pml4_phys) return;
    
    uint64_t* pml4 = phys_to_virt<uint64_t>(pml4_phys);
    
    // Free user mappings across all user PML4 entries (0 to 255)
    for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++) {
        if (pml4[pml4_idx] & VMM_FLAG_PRESENT) {
            uint64_t* pdpt = phys_to_virt<uint64_t>(pml4[pml4_idx] & VMM_PHYS_ADDR_MASK);
            for (int i = 0; i < 512; i++) {
                if (pdpt[i] & VMM_FLAG_PRESENT) {
                    if (pdpt[i] & VMM_FLAG_HUGE) continue;
                    uint64_t* pd = phys_to_virt<uint64_t>(pdpt[i] & VMM_PHYS_ADDR_MASK);
                    for (int j = 0; j < 512; j++) {
                        if (pd[j] & VMM_FLAG_PRESENT) {
                            if (pd[j] & VMM_FLAG_HUGE) continue;
                            uint64_t* pt = phys_to_virt<uint64_t>(pd[j] & VMM_PHYS_ADDR_MASK);
                            for (int k = 0; k < 512; k++) {
                                if (pt[k] & VMM_FLAG_PRESENT) {
                                    // Only free physical frames that belong to USER space!
                                    if (pt[k] & VMM_FLAG_USER) {
                                        uint64_t phys_page = pt[k] & VMM_PHYS_ADDR_MASK;
                                        pmm_free_frame(phys_page);
                                    }
                                }
                            }
                            pmm_free_frame((uint64_t)pt);
                        }
                    }
                    pmm_free_frame((uint64_t)pd);
                }
            }
            pmm_free_frame((uint64_t)pdpt);
        }
    }
    
    // Free the PML4 base directory page frame itself
    pmm_free_frame(pml4_phys);
}

bool vmm_map_page_in_pml4(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    if ((virt & 0xFFFULL) || (phys & 0xFFFULL)) {
        return false;
    }

    uint64_t* pml4 = phys_to_virt<uint64_t>(pml4_phys);
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx   = (virt >> 21) & 0x1FF;
    size_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_FLAG_PRESENT)) {
        uint64_t new_pdpt = pmm_alloc_frame();
        if (!new_pdpt) return false;
        // Clean new PDPT frame
        for (int i = 0; i < 512; i++) phys_to_virt<uint64_t>(new_pdpt)[i] = 0;
        pml4[pml4_idx] = new_pdpt | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
    }
    uint64_t* pdpt = phys_to_virt<uint64_t>(pml4[pml4_idx] & VMM_PHYS_ADDR_MASK);

    if (!(pdpt[pdpt_idx] & VMM_FLAG_PRESENT)) {
        uint64_t new_pd = pmm_alloc_frame();
        if (!new_pd) return false;
        // Clean new PD frame
        for (int i = 0; i < 512; i++) phys_to_virt<uint64_t>(new_pd)[i] = 0;
        pdpt[pdpt_idx] = new_pd | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
    }
    uint64_t* pd = phys_to_virt<uint64_t>(pdpt[pdpt_idx] & VMM_PHYS_ADDR_MASK);

    if (!(pd[pd_idx] & VMM_FLAG_PRESENT)) {
        uint64_t new_pt = pmm_alloc_frame();
        if (!new_pt) return false;
        // Clean new PT frame
        for (int i = 0; i < 512; i++) phys_to_virt<uint64_t>(new_pt)[i] = 0;
        pd[pd_idx] = new_pt | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
    }
    uint64_t* pt = phys_to_virt<uint64_t>(pd[pd_idx] & VMM_PHYS_ADDR_MASK);

    pt[pt_idx] = phys | flags;

    // Only issue invlpg if we are mapping into the active address space
    uint64_t active;
    __asm__ __volatile__ ("mov %%cr3, %0" : "=r"(active));
    active &= VMM_PHYS_ADDR_MASK;
    if (active == pml4_phys) {
        __asm__ __volatile__ ("invlpg (%0)" : : "r"(virt) : "memory");
    }

    return true;
}

void vmm_clone_user_space(uint64_t parent_pml4, uint64_t child_pml4) {
    uint64_t* parent_pml4_ptr = phys_to_virt<uint64_t>(parent_pml4);
    
    for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++) {
        if (!(parent_pml4_ptr[pml4_idx] & VMM_FLAG_PRESENT)) continue;
        
        uint64_t* parent_pdpt = phys_to_virt<uint64_t>(parent_pml4_ptr[pml4_idx] & VMM_PHYS_ADDR_MASK);
        for (int i = 0; i < 512; i++) {
            if (parent_pdpt[i] & VMM_FLAG_PRESENT) {
                if (parent_pdpt[i] & VMM_FLAG_HUGE) continue;
                uint64_t* parent_pd = phys_to_virt<uint64_t>(parent_pdpt[i] & VMM_PHYS_ADDR_MASK);
                for (int j = 0; j < 512; j++) {
                    if (parent_pd[j] & VMM_FLAG_PRESENT) {
                        if (parent_pd[j] & VMM_FLAG_HUGE) continue;
                        uint64_t* parent_pt = phys_to_virt<uint64_t>(parent_pd[j] & VMM_PHYS_ADDR_MASK);
                        for (int k = 0; k < 512; k++) {
                            if (parent_pt[k] & VMM_FLAG_PRESENT) {
                                uint64_t parent_phys = parent_pt[k] & VMM_PHYS_ADDR_MASK;
                                uint64_t flags = parent_pt[k] & 0xFFFULL;
                                
                                // Only clone user mappings (which have VMM_FLAG_USER set)
                                if (flags & VMM_FLAG_USER) {
                                    uint64_t virt = ((uint64_t)pml4_idx << 39) | ((uint64_t)i << 30) | ((uint64_t)j << 21) | ((uint64_t)k << 12);
                                    uint64_t child_phys = pmm_alloc_frame();
                                    if (child_phys) {
                                        char* dest = phys_to_virt<char>(child_phys);
                                        char* src = phys_to_virt<char>(parent_phys);
                                        for (size_t size = 0; size < 4096; ++size) {
                                            dest[size] = src[size];
                                        }
                                        vmm_map_page_in_pml4(child_pml4, virt, child_phys, flags);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace kernel
