#include "gdt.hpp"

namespace kernel {

// Stack allocated for Double Fault (#DF) handler (IST 1)
static char double_fault_ist_stack[16384] __attribute__((aligned(16)));

static TSS tss;

struct __attribute__((packed)) GdtTable {
    GdtEntry null_desc;
    GdtEntry kernel_code;
    GdtEntry kernel_data;
    GdtEntry user_data;
    GdtEntry user_code;
    GdtTssDescriptor tss_desc;
};

static GdtTable gdt;
static GdtPointer gdt_ptr;

// Helper to fill normal 8-byte GDT entries
static void make_gdt_entry(GdtEntry* entry, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity) {
    entry->limit_low = (uint16_t)(limit & 0xFFFF);
    entry->base_low = (uint16_t)(base & 0xFFFF);
    entry->base_middle = (uint8_t)((base >> 16) & 0xFF);
    entry->access = access;
    entry->granularity = (uint8_t)(((limit >> 16) & 0x0F) | (granularity & 0xF0));
    entry->base_high = (uint8_t)((base >> 24) & 0xFF);
}

// Helper to fill 16-byte TSS descriptors
static void make_gdt_tss_desc(GdtTssDescriptor* desc, uint64_t base, uint32_t limit, uint8_t access) {
    desc->limit_low = (uint16_t)(limit & 0xFFFF);
    desc->base_low = (uint16_t)(base & 0xFFFF);
    desc->base_middle = (uint8_t)((base >> 16) & 0xFF);
    desc->access = access;
    desc->granularity = (uint8_t)(((limit >> 16) & 0x0F) | 0x00); // System descriptor, no G bit
    desc->base_high = (uint8_t)((base >> 24) & 0xFF);
    desc->base_highest = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    desc->reserved = 0;
}

void gdt_init() {
    // 1. Initialize the TSS structure to 0
    char* tss_bytes = (char*)&tss;
    for (size_t i = 0; i < sizeof(TSS); ++i) {
        tss_bytes[i] = 0;
    }

    // Assign the top of the double fault stack to IST index 1
    // (Stack grows downwards on x86_64)
    tss.ist[1] = (uint64_t)double_fault_ist_stack + sizeof(double_fault_ist_stack);
    tss.io_map_base = sizeof(TSS); // Disable IO port mapping protection in TSS

    // 2. Initialize GDT entries
    // Entry 0: Null Descriptor (Required)
    make_gdt_entry(&gdt.null_desc, 0, 0, 0, 0);

    // Entry 1: Kernel Code. Access: Present (0x80) + Ring 0 (0x00) + Code/Data (0x10) + Exec (0x08) + Read (0x02) = 0x9A
    // Granularity: Long Mode Active flag (0x20)
    make_gdt_entry(&gdt.kernel_code, 0, 0xFFFFF, 0x9A, 0x20);

    // Entry 2: Kernel Data. Access: Present (0x80) + Ring 0 (0x00) + Code/Data (0x10) + Write (0x02) = 0x92
    // Granularity: Default (0x00)
    make_gdt_entry(&gdt.kernel_data, 0, 0xFFFFF, 0x92, 0x00);

    // Entry 3: User Data. Access: Present (0x80) + Ring 3 (0x60) + Code/Data (0x10) + Write (0x02) = 0xF2
    make_gdt_entry(&gdt.user_data, 0, 0xFFFFF, 0xF2, 0x00);

    // Entry 4: User Code. Access: Present (0x80) + Ring 3 (0x60) + Code/Data (0x10) + Exec (0x08) + Read (0x02) = 0xFA
    // Granularity: Long Mode Active flag (0x20)
    make_gdt_entry(&gdt.user_code, 0, 0xFFFFF, 0xFA, 0x20);

    // Entry 5 & 6: TSS Descriptor. Access: Present (0x80) + Available TSS (0x09) = 0x89
    uint64_t tss_base = (uint64_t)&tss;
    make_gdt_tss_desc(&gdt.tss_desc, tss_base, sizeof(TSS) - 1, 0x89);

    // 3. Load GDT using lgdt instruction
    gdt_ptr.limit = sizeof(GdtTable) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    __asm__ __volatile__ ("lgdt %0" : : "m"(gdt_ptr));

    // 4. Reload code segment register CS using far return simulation
    __asm__ __volatile__ (
        "pushq %0\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        :
        : "i"(0x08)
        : "rax", "memory"
    );

    // 5. Reload data segment registers (DS, ES, FS, GS, SS)
    __asm__ __volatile__ (
        "movw %0, %%ds\n\t"
        "movw %0, %%es\n\t"
        "movw %0, %%fs\n\t"
        "movw %0, %%gs\n\t"
        "movw %0, %%ss\n\t"
        :
        : "r"((uint16_t)0x10)
    );

    // 6. Load TSS selector into task register
    __asm__ __volatile__ (
        "ltr %0"
        :
        : "r"((uint16_t)0x28) // GDT Index 5 * 8 = 40 (0x28)
    );
}

} // namespace kernel
