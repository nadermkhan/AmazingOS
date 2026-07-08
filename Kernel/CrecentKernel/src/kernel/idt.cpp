#include "idt.hpp"
#include "../drivers/vga.hpp"
#include "../drivers/serial.hpp"

// Table of 256 handler pointers generated in assembly
extern "C" void* isr_stub_table[256];

namespace kernel {

static IdtEntry idt[256];
static IdtPointer idt_ptr;

static const char* exception_messages[32] = {
    "Division By Zero (#DE)",
    "Debug (#DB)",
    "Non-Maskable Interrupt (#NMI)",
    "Breakpoint (#BP)",
    "Into Detected Overflow (#OF)",
    "Out of Bounds (#BR)",
    "Invalid Opcode (#UD)",
    "No Coprocessor (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Bad TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack Fault (#SS)",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Unknown Interrupt Exception",
    "Coprocessor Fault (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Floating-Point (#XM)",
    "Virtualization Exception (#VE)",
    "Control Protection Exception (#CP)",
    "Reserved Exception",
    "Reserved Exception",
    "Reserved Exception",
    "Reserved Exception",
    "Reserved Exception",
    "Reserved Exception",
    "Hypervisor Injection Exception",
    "VMM Communication Exception (#VC)",
    "Security Exception (#SX)",
    "Reserved Exception"
};

// Helper: prints a 64-bit value in hexadecimal representation to VGA
static void vga_print_hex(uint64_t val) {
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    const char* hex_chars = "0123456789ABCDEF";
    for (int i = 0; i < 16; ++i) {
        buf[2 + i] = hex_chars[(val >> ((15 - i) * 4)) & 0x0F];
    }
    buf[18] = '\0';
    drivers::Vga::print(buf);
}

// Helper: prints a 64-bit value in hexadecimal representation to Serial
static void serial_print_hex(uint64_t val) {
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

// Helper: prints a full register trace to VGA and Serial on crash
static void dump_registers(InterruptFrame* frame) {
    // 1. Print to VGA Display
    drivers::Vga::print("RIP: "); vga_print_hex(frame->rip); drivers::Vga::print("  CS:  "); vga_print_hex(frame->cs); drivers::Vga::println("");
    drivers::Vga::print("RSP: "); vga_print_hex(frame->rsp); drivers::Vga::print("  SS:  "); vga_print_hex(frame->ss); drivers::Vga::println("");
    drivers::Vga::print("RAX: "); vga_print_hex(frame->rax); drivers::Vga::print("  RBX: "); vga_print_hex(frame->rbx); drivers::Vga::println("");
    drivers::Vga::print("RCX: "); vga_print_hex(frame->rcx); drivers::Vga::print("  RDX: "); vga_print_hex(frame->rdx); drivers::Vga::println("");
    drivers::Vga::print("RSI: "); vga_print_hex(frame->rsi); drivers::Vga::print("  RDI: "); vga_print_hex(frame->rdi); drivers::Vga::println("");
    drivers::Vga::print("RBP: "); vga_print_hex(frame->rbp); drivers::Vga::print("  R8:  "); vga_print_hex(frame->r8);  drivers::Vga::println("");
    drivers::Vga::print("R9:  "); vga_print_hex(frame->r9);  drivers::Vga::print("  R10: "); vga_print_hex(frame->r10); drivers::Vga::println("");
    drivers::Vga::print("R11: "); vga_print_hex(frame->r11); drivers::Vga::print("  R12: "); vga_print_hex(frame->r12); drivers::Vga::println("");
    drivers::Vga::print("R13: "); vga_print_hex(frame->r13); drivers::Vga::print("  R14: "); vga_print_hex(frame->r14); drivers::Vga::println("");
    drivers::Vga::print("R15: "); vga_print_hex(frame->r15); drivers::Vga::print("  RFL: "); vga_print_hex(frame->rflags); drivers::Vga::println("");
    drivers::Vga::print("ERR: "); vga_print_hex(frame->error_code); drivers::Vga::print("  VEC: "); vga_print_hex(frame->int_no); drivers::Vga::println("");

    // 2. Print to Serial COM1 Port
    drivers::Serial::print("RIP: "); serial_print_hex(frame->rip); drivers::Serial::print("  CS:  "); serial_print_hex(frame->cs); drivers::Serial::println("");
    drivers::Serial::print("RSP: "); serial_print_hex(frame->rsp); drivers::Serial::print("  SS:  "); serial_print_hex(frame->ss); drivers::Serial::println("");
    drivers::Serial::print("RAX: "); serial_print_hex(frame->rax); drivers::Serial::print("  RBX: "); serial_print_hex(frame->rbx); drivers::Serial::println("");
    drivers::Serial::print("RCX: "); serial_print_hex(frame->rcx); drivers::Serial::print("  RDX: "); serial_print_hex(frame->rdx); drivers::Serial::println("");
    drivers::Serial::print("RSI: "); serial_print_hex(frame->rsi); drivers::Serial::print("  RDI: "); serial_print_hex(frame->rdi); drivers::Serial::println("");
    drivers::Serial::print("RBP: "); serial_print_hex(frame->rbp); drivers::Serial::print("  R8:  "); serial_print_hex(frame->r8);  drivers::Serial::println("");
    drivers::Serial::print("R9:  "); serial_print_hex(frame->r9);  drivers::Serial::print("  R10: "); serial_print_hex(frame->r10); drivers::Serial::println("");
    drivers::Serial::print("R11: "); serial_print_hex(frame->r11); drivers::Serial::print("  R12: "); serial_print_hex(frame->r12); drivers::Serial::println("");
    drivers::Serial::print("R13: "); serial_print_hex(frame->r13); drivers::Serial::print("  R14: "); serial_print_hex(frame->r14); drivers::Serial::println("");
    drivers::Serial::print("R15: "); serial_print_hex(frame->r15); drivers::Serial::print("  RFL: "); serial_print_hex(frame->rflags); drivers::Serial::println("");
    drivers::Serial::print("ERR: "); serial_print_hex(frame->error_code); drivers::Serial::print("  VEC: "); serial_print_hex(frame->int_no); drivers::Serial::println("");
}

void idt_set_gate(uint8_t vector, void* handler_ptr, uint8_t type_attr, uint8_t ist_index) {
    uint64_t addr = (uint64_t)handler_ptr;
    idt[vector].offset_low = (uint16_t)(addr & 0xFFFF);
    idt[vector].selector = 0x08; // Code segment descriptor in GDT
    idt[vector].ist = ist_index & 0x07;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_middle = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vector].reserved = 0;
}

void idt_init() {
    // Zero out the IDT table
    char* idt_bytes = (char*)idt;
    for (size_t i = 0; i < sizeof(idt); ++i) {
        idt_bytes[i] = 0;
    }

    // Register all 256 assembly handlers
    for (int i = 0; i < 256; ++i) {
        // Attribute 0x8E: Present (1), Ring 0 (00), Interrupt Gate type (0xE)
        // Double Fault (vector 8) gets dedicated IST stack index 1
        uint8_t ist_index = (i == 8) ? 1 : 0;
        idt_set_gate((uint8_t)i, isr_stub_table[i], 0x8E, ist_index);
    }

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;

    // Load IDT into CPU using lidt instruction
    __asm__ __volatile__ ("lidt %0" : : "m"(idt_ptr));
}

// C++ dispatcher called by the assembly entry stub
extern "C" __attribute__((sysv_abi)) void interrupt_handler(InterruptFrame* frame) {
    if (frame->int_no < 32) {
        // Exception caught! Disable interrupts, print debug details, and halt CPU
        __asm__ __volatile__ ("cli");

        drivers::Vga::set_color(drivers::VgaColor::RED, drivers::VgaColor::BLACK);
        drivers::Vga::println("****************************************");
        drivers::Vga::print(" !!! KERNEL EXCEPTION PANIC: ");
        drivers::Vga::println(exception_messages[frame->int_no]);
        drivers::Vga::println("****************************************");

        drivers::Serial::println("****************************************");
        drivers::Serial::print(" !!! KERNEL EXCEPTION PANIC: ");
        drivers::Serial::println(exception_messages[frame->int_no]);
        drivers::Serial::println("****************************************");

        // Specific handling for Page Faults to report CR2 register (faulting virtual address)
        if (frame->int_no == 14) {
            uint64_t cr2_val;
            __asm__ __volatile__ ("mov %%cr2, %0" : "=r"(cr2_val));
            drivers::Vga::print("Faulting Virtual Address (CR2): ");
            vga_print_hex(cr2_val);
            drivers::Vga::println("");

            drivers::Serial::print("Faulting Virtual Address (CR2): ");
            serial_print_hex(cr2_val);
            drivers::Serial::println("");
        }

        dump_registers(frame);
        
        // Loop forever executing Hlt
        while (true) {
            __asm__ __volatile__ ("hlt");
        }
    } else {
        // Log interrupt (IRQs and software interrupts)
        drivers::Serial::print("[INT] Captured Interrupt Vector: ");
        serial_print_hex(frame->int_no);
        drivers::Serial::println("");

        drivers::Vga::print("[INT] Interrupt Vector triggered (");
        vga_print_hex(frame->int_no);
        drivers::Vga::println(")");
    }
}

} // namespace kernel
