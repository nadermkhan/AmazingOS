#pragma once

#include "types.hpp"

namespace kernel {

// Initialize the Kernel Heap Allocator
bool heap_init();

// Allocate a block of memory from the kernel heap
// size: Size in bytes of the requested memory block
// Returns pointer to the allocated memory block, or nullptr if out of memory
void* kmalloc(size_t size);

// Free a block of memory previously allocated by kmalloc
// ptr: Pointer to the memory block to free
void kfree(void* ptr);

} // namespace kernel
