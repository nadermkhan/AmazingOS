#pragma once

#include "types.hpp"

namespace kernel {

extern uint64_t active_pml4;

// Page Table Flags (x86_64 structure)
constexpr uint64_t VMM_FLAG_PRESENT    = 1ULL << 0;
constexpr uint64_t VMM_FLAG_WRITABLE   = 1ULL << 1;
constexpr uint64_t VMM_FLAG_USER       = 1ULL << 2;
constexpr uint64_t VMM_FLAG_WRITE_THR  = 1ULL << 3;
constexpr uint64_t VMM_FLAG_CACHE_DIS  = 1ULL << 4;
constexpr uint64_t VMM_FLAG_ACCESSED   = 1ULL << 5;
constexpr uint64_t VMM_FLAG_DIRTY      = 1ULL << 6;
constexpr uint64_t VMM_FLAG_HUGE       = 1ULL << 7;  // 2MB huge page (bit 7 set in PD/PDPT)
constexpr uint64_t VMM_FLAG_GLOBAL     = 1ULL << 8;
constexpr uint64_t VMM_FLAG_NO_EXECUTE  = 1ULL << 63;

// Initialize the Virtual Memory Manager (reads CR3 register)
void vmm_init();

// Separate PML4[256] (Direct Physical Map) from PML4[0] (identity map)
// Must be called after PMM is initialized to allocate a dedicated PDPT page
void vmm_separate_dpm();

// Map a 4KB virtual page to a physical frame
// virt: Virtual address (4KB aligned)
// phys: Physical address (4KB aligned)
// flags: Page table entry flags (VMM_FLAG_PRESENT, VMM_FLAG_WRITABLE, etc.)
bool vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

// Unmap a 4KB virtual page and flush it from Translation Lookaside Buffer (TLB)
// virt: Virtual address (4KB aligned)
bool vmm_unmap_page(uint64_t virt);

// Check if a virtual address is mapped in the page directory structure
bool vmm_is_mapped(uint64_t virt);

// Verify if a virtual address is mapped and has Ring 3 User access rights
bool vmm_is_user_mapped(uint64_t virt);

// Translate a virtual address to its corresponding physical address
// Returns 0 if the address is not mapped
uint64_t vmm_get_phys(uint64_t virt);

// Create a new user PML4 address space (copies kernel mappings, user space clean)
uint64_t vmm_create_user_address_space();

// Recursively destroy all user pages and page directories mapped in a PML4
void vmm_destroy_user_address_space(uint64_t pml4_phys);

// Map a virtual page into a specific PML4 directory
bool vmm_map_page_in_pml4(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

// Clone user space address mappings from parent to child PML4
void vmm_clone_user_space(uint64_t parent_pml4, uint64_t child_pml4);

// Dump page table entries and structure details for a given virtual address
void vmm_dump_page_table_details(uint64_t virt);

} // namespace kernel
