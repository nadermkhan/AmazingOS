#pragma once

#include "types.hpp"

namespace kernel {

// Gate Descriptor structure (16 bytes, x86_64)
struct __attribute__((packed)) IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;          // Interrupt Stack Table index (0..7)
    uint8_t  type_attr;    // Type & Attributes (Type: 4 bits, DPL: 2 bits, Present: 1 bit)
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

// IDT Pointer structure
struct __attribute__((packed)) IdtPointer {
    uint16_t limit;
    uint64_t base;
};

// Interrupt Frame structure passed by assembly stub containing register states
struct __attribute__((packed)) InterruptFrame {
    // Saved general-purpose registers (pushed by isr_common_stub)
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;

    // Custom pushed variables
    uint64_t int_no;
    uint64_t error_code;

    // Registers pushed automatically by hardware on interrupt
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

// Initialize the IDT
void idt_init();

// Set a specific gate inside the IDT
void idt_set_gate(uint8_t vector, void* handler_ptr, uint8_t type_attr, uint8_t ist_index);

// C++ dispatcher called by assembly stubs
extern "C" __attribute__((sysv_abi)) void interrupt_handler(InterruptFrame* frame);

} // namespace kernel
