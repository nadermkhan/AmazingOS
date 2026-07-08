#pragma once

#include "types.hpp"

namespace kernel {

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

} // namespace kernel
