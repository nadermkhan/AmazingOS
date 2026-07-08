#pragma once

#include "types.hpp"

namespace kernel {

// Initialize the Physical Memory Manager using Multiboot metadata
// magic: Multiboot magic value passed by bootloader
// maddr: Physical base address of the Multiboot information structure
bool pmm_init(uint32_t magic, uint64_t maddr);

// Allocate a 4KB physical page frame
// Returns the physical address of the allocated frame, or 0 if out of memory
uint64_t pmm_alloc_frame();

// Free a previously allocated 4KB physical page frame
// frame: Physical address of the frame to free
void pmm_free_frame(uint64_t frame);

// Return the total number of free page frames in the PMM pool
size_t pmm_get_free_frames_count();

// Retrieve the physical memory bounds of a loaded Multiboot module
// index: Index of the module (0 for first module)
// start: Output parameter for the start address
// end: Output parameter for the end address
// Returns true if module exists, false otherwise
bool pmm_get_module(uint32_t index, uint64_t* start, uint64_t* end);

} // namespace kernel
