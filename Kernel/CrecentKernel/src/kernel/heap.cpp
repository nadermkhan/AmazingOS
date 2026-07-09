#include "heap.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "../drivers/serial.hpp"

namespace kernel {

struct PageHeader {
    uint32_t magic;         // 0x51ABCA08 for Slab, 0xD12EC100 for Direct PMM Page
    uint32_t page_count;    // Total pages allocated (for Direct allocations)
    PageHeader* next;       // Link page directories in caches
    void* free_list_head;   // Head of free object list (for Slabs)
    uint32_t free_count;    // Number of free objects in this page (for Slabs)
    uint32_t total_count;   // Total objects fitting in this page (for Slabs)
    size_t object_size;     // Size of objects managed by this slab
};

struct KmemCache {
    size_t object_size;
    PageHeader* slabs;
};

// Power-of-two slab caches for small dynamic allocations
static KmemCache caches[] = {
    { 16, nullptr },
    { 32, nullptr },
    { 64, nullptr },
    { 128, nullptr },
    { 256, nullptr },
    { 512, nullptr },
    { 1024, nullptr },
    { 2048, nullptr }
};
constexpr size_t NUM_CACHES = sizeof(caches) / sizeof(caches[0]);

// Virtual address space tracking pointer for large allocations
// Starts at 4GB offset in the Direct Physical Map (0xFFFF800100000000)
// to utilize the shared kernel PDPT (PML4[256]) and prevent stack/heap page divergence.
static uint64_t next_large_virt = 0xFFFF800100000000ULL;

bool heap_init() {
    drivers::Serial::println("[INIT] Slab Heap Allocator initialized.");
    return true;
}

void* kmalloc(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    // 1. If allocation fits in slab size, route to Slab Cache
    if (size <= 2048) {
        KmemCache* cache = nullptr;
        for (size_t i = 0; i < NUM_CACHES; ++i) {
            if (caches[i].object_size >= size) {
                cache = &caches[i];
                break;
            }
        }

        if (!cache) return nullptr;

        // Search for a slab page with free slots
        PageHeader* slab = cache->slabs;
        while (slab) {
            if (slab->free_count > 0) {
                break;
            }
            slab = slab->next;
        }

        // If no free slab exists, allocate a new page frame from PMM
        if (!slab) {
            uint64_t phys_page = pmm_alloc_frame();
            if (!phys_page) return nullptr;

            slab = (PageHeader*)phys_page;
            slab->magic = 0x51ABCA08;
            slab->page_count = 1;
            slab->next = cache->slabs;
            slab->object_size = cache->object_size;

            // Link objects in the remaining space of the page
            size_t header_size = (sizeof(PageHeader) + 15) & ~15; // 16-byte align objects
            char* page_ptr = (char*)phys_page;
            size_t offset = header_size;
            void* prev = nullptr;
            uint32_t count = 0;

            while (offset + cache->object_size <= 4096) {
                void* obj = page_ptr + offset;
                *(void**)obj = prev;
                prev = obj;
                offset += cache->object_size;
                count++;
            }

            slab->free_list_head = prev;
            slab->free_count = count;
            slab->total_count = count;

            cache->slabs = slab;
        }

        // Pop the first free object from the slab list
        void* obj = slab->free_list_head;
        slab->free_list_head = *(void**)obj;
        slab->free_count--;

        return obj;
    }

    // 2. Large allocation: fallback to direct page allocations mapped to virtual space
    size_t total_needed = size + sizeof(PageHeader);
    size_t pages = (total_needed + 4095) / 4096;

    // Grab the next virtual base address
    uint64_t virt_start = next_large_virt;
    next_large_virt += pages * 4096;

    // Allocate physical frames from PMM and map them contiguously in VMM first
    for (size_t i = 0; i < pages; ++i) {
        uint64_t phys_page = pmm_alloc_frame();
        if (!phys_page) return nullptr;

        vmm_map_page(virt_start + i * 4096, phys_page, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);
    }

    // Now write PageHeader at the start of the mapped virtual address space
    PageHeader* header = (PageHeader*)virt_start;
    header->magic = 0xD12EC100;
    header->page_count = pages;
    header->object_size = size;
    header->next = nullptr;
    header->free_list_head = nullptr;
    header->free_count = 0;
    header->total_count = 0;

    // Return the virtual address offset past the header
    return (void*)(virt_start + sizeof(PageHeader));
}

static void print_hex(uint64_t val) {
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    const char* hex_chars = "0123456789ABCDEF";
    for (int i = 0; i < 16; ++i) {
        buf[2 + i] = hex_chars[(val >> ((15 - i) * 4)) & 0x0F];
    }
    buf[18] = '\0';
    drivers::Serial::print(buf);
}

void kfree(void* ptr) {
    if (!ptr) return;

    // Resolve PageHeader in O(1) constant time from 4KB boundary
    PageHeader* header = (PageHeader*)((uintptr_t)ptr & ~0xFFFULL);

    if (header->magic == 0x51ABCA08) {
        // Slab free: push object back onto free list
        *(void**)ptr = header->free_list_head;
        header->free_list_head = ptr;
        header->free_count++;
    } else if (header->magic == 0xD12EC100) {
        // Direct allocation free: retrieve pages, free to PMM, and unmap
        uint64_t virt_start = (uint64_t)header;
        size_t pages = header->page_count;

        for (size_t i = 0; i < pages; ++i) {
            uint64_t virt_page = virt_start + i * 4096;
            uint64_t phys_page = vmm_get_phys(virt_page);
            if (phys_page) {
                pmm_free_frame(phys_page);
            }
            vmm_unmap_page(virt_page);
        }
    } else {
        drivers::Serial::print("[HEAP] Error: Attempted kfree on invalid pointer. ptr: ");
        print_hex((uint64_t)ptr);
        drivers::Serial::print(" header: ");
        print_hex((uint64_t)header);
        drivers::Serial::print(" magic: ");
        print_hex(header->magic);
        drivers::Serial::println("");
    }
}

} // namespace kernel

// --- C++ Global Operators Overloads ---

void* operator new(size_t size) {
    return kernel::kmalloc(size);
}

void operator delete(void* ptr) noexcept {
    kernel::kfree(ptr);
}

void* operator new[](size_t size) {
    return kernel::kmalloc(size);
}

void operator delete[](void* ptr) noexcept {
    kernel::kfree(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    kernel::kfree(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    kernel::kfree(ptr);
}
