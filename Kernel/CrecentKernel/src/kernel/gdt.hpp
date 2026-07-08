#pragma once

#include "types.hpp"

namespace kernel {

// Task State Segment structure (104 bytes, 64-bit mode)
struct __attribute__((packed)) TSS {
    uint32_t reserved0;
    uint64_t rsp0;      // Privilege stack pointers (ring 0)
    uint64_t rsp1;      // Ring 1 stack pointer
    uint64_t rsp2;      // Ring 2 stack pointer
    uint64_t reserved1;
    uint64_t ist[8];    // Interrupt Stack Table (IST 1..7, index 0 is reserved)
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
};

// GDT entry structure (8 bytes)
struct __attribute__((packed)) GdtEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
};

// 64-bit TSS descriptor (16 bytes, occupies 2 standard GDT entry slots)
struct __attribute__((packed)) GdtTssDescriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_highest;
    uint32_t reserved;
};

// GDT Pointer structure
struct __attribute__((packed)) GdtPointer {
    uint16_t limit;
    uint64_t base;
};

// Initialize the GDT, load GDT and segment registers, register the TSS, and configure the IST stack
void gdt_init();

} // namespace kernel
