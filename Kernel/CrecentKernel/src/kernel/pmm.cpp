#include "pmm.hpp"
#include "../drivers/serial.hpp"

extern "C" char _kernel_start[];
extern "C" char _kernel_end[];

namespace kernel {

static uint64_t free_list_head = 0;
static size_t free_frames_count = 0;

struct Mboot1MmapEntry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

struct Mboot2Tag {
    uint32_t type;
    uint32_t size;
};

struct Mboot2MmapEntry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

static void register_region(uint64_t base, uint64_t len, uint64_t guard_start, uint64_t guard_end) {
    uint64_t kern_start = (uint64_t)_kernel_start;
    uint64_t kern_end = (uint64_t)_kernel_end;

    // Align base up to 4KB page boundary
    uint64_t start = (base + 0xFFFULL) & ~0xFFFULL;
    // Align end down to 4KB page boundary
    uint64_t end = (base + len) & ~0xFFFULL;

    for (uint64_t frame = start; frame < end; frame += 4096) {
        // Guard low memory region below 1MB (BIOS data, bootloader, stack)
        if (frame < 0x100000UL) {
            continue;
        }
        // Guard kernel space
        if (frame >= kern_start && frame < kern_end) {
            continue;
        }
        // Guard Multiboot Information structure space
        if (frame >= guard_start && frame < guard_end) {
            continue;
        }

        // Push frame onto free list
        pmm_free_frame(frame);
    }
}

static void parse_multiboot1(uint64_t maddr) {
    uint32_t flags = *(uint32_t*)maddr;
    if (!(flags & (1 << 6))) {
        drivers::Serial::println("[PMM] Warning: Multiboot1 flags lack memory map info.");
        return;
    }

    uint32_t mmap_len = *(uint32_t*)(maddr + 44);
    uint32_t mmap_addr = *(uint32_t*)(maddr + 48);

    uintptr_t curr = mmap_addr;
    uintptr_t mmap_end = mmap_addr + mmap_len;

    while (curr < mmap_end) {
        Mboot1MmapEntry* entry = (Mboot1MmapEntry*)curr;
        if (entry->type == 1) { // Type 1 is available usable RAM
            // Guard MBI region (estimated 4KB starting at maddr)
            register_region(entry->addr, entry->len, maddr, maddr + 4096);
        }
        curr += entry->size + 4;
    }
}

static void parse_multiboot2(uint64_t maddr) {
    uint32_t total_size = *(uint32_t*)maddr;
    uint64_t mbi_end = maddr + total_size;

    uintptr_t curr = maddr + 8; // Skip size and reserved fields
    while (curr < mbi_end) {
        Mboot2Tag* tag = (Mboot2Tag*)curr;
        if (tag->type == 0 && tag->size == 8) {
            break; // End tag
        }

        if (tag->type == 6) { // Memory map tag
            uint32_t entry_size = *(uint32_t*)(curr + 8);
            uintptr_t entry_ptr = curr + 16;
            uintptr_t tag_end = curr + tag->size;

            while (entry_ptr < tag_end) {
                Mboot2MmapEntry* entry = (Mboot2MmapEntry*)entry_ptr;
                if (entry->type == 1) { // Usable RAM
                    register_region(entry->base_addr, entry->length, maddr, mbi_end);
                }
                entry_ptr += entry_size;
            }
        }
        // Tags are padded to 8-byte boundaries
        curr += (tag->size + 7) & ~7;
    }
}

bool pmm_init(uint32_t magic, uint64_t maddr) {
    free_list_head = 0;
    free_frames_count = 0;

    if (magic == 0x2badb002) {
        drivers::Serial::println("[PMM] Parsing Multiboot1 memory map...");
        parse_multiboot1(maddr);
    } else if (magic == 0x36d76289) {
        drivers::Serial::println("[PMM] Parsing Multiboot2 memory map...");
        parse_multiboot2(maddr);
    } else {
        drivers::Serial::println("[PMM] Error: Invalid multiboot magic signature!");
        return false;
    }

    if (free_frames_count == 0) {
        drivers::Serial::println("[PMM] Error: No free physical memory pages found!");
        return false;
    }

    return true;
}

uint64_t pmm_alloc_frame() {
    if (free_list_head == 0) {
        return 0; // Out of memory
    }
    uint64_t frame = free_list_head;
    
    // Pop the frame from the free list
    free_list_head = *(uint64_t*)frame;
    free_frames_count--;

    // Zero out the frame for security and clean allocations
    char* bytes = (char*)frame;
    for (size_t i = 0; i < 4096; ++i) {
        bytes[i] = 0;
    }

    return frame;
}

void pmm_free_frame(uint64_t frame) {
    // Assert 4KB aligned address
    if (frame & 0xFFFUL) return;

    // Push the frame back onto the free list
    *(uint64_t*)frame = free_list_head;
    free_list_head = frame;
    free_frames_count++;
}

size_t pmm_get_free_frames_count() {
    return free_frames_count;
}

} // namespace kernel
