#include "apic.hpp"
#include "serial.hpp"
#include "../kernel/vmm.hpp"

namespace drivers {

#define IOAPIC_BASE 0xFEC00000

static void ioapic_write(uint8_t reg, uint32_t val) {
    *(volatile uint32_t*)IOAPIC_BASE = reg;
    *(volatile uint32_t*)(IOAPIC_BASE + 0x10) = val;
}

uint32_t Apic::read_reg(uint32_t reg) {
    return *(volatile uint32_t*)(LAPIC_BASE + reg);
}

void Apic::write_reg(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(LAPIC_BASE + reg) = val;
}

void Apic::eoi() {
    write_reg(LAPIC_REG_EOI, 0);
}

void Apic::disable_pic() {
    // 1. Remap the master and slave PIC vector offsets (away from exception range)
    // Send ICW1 (Initialization Control Word 1)
    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    // Send ICW2 (Master vector offset = 0x20, Slave vector offset = 0x28)
    outb(0x21, 0x20);
    outb(0xA1, 0x28);

    // Send ICW3 (Cascade master/slave linkage)
    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    // Send ICW4 (Mode 8086)
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    // 2. Disable both master and slave PICs entirely by masking all 16 interrupts
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

bool Apic::init() {
    // 1. Verify that APIC is supported by the CPU via CPUID
    uint32_t eax, ebx, ecx, edx;
    __asm__ __volatile__ (
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );

    // Bit 9 of EDX represents APIC availability
    if (!(edx & (1 << 9))) {
        return false;
    }

    // 2. Map Local APIC (0xFEE00000) and I/O APIC (0xFEC00000) pages to ensure they are accessible
    kernel::vmm_map_page(0xFEE00000, 0xFEE00000, kernel::VMM_FLAG_PRESENT | kernel::VMM_FLAG_WRITABLE);
    kernel::vmm_map_page(0xFEC00000, 0xFEC00000, kernel::VMM_FLAG_PRESENT | kernel::VMM_FLAG_WRITABLE);

    // 3. Disable legacy PIC controller to prevent legacy interrupts
    disable_pic();

    // 4. Enable the Local APIC
    // Set bit 8 (APIC Software Enable) of the Spurious Interrupt Vector register (SVR)
    // We map the spurious interrupt vector to vector 0xFF
    write_reg(LAPIC_REG_SVR, read_reg(LAPIC_REG_SVR) | 0x100 | 0xFF);

    // 5. Set Task Priority Register (TPR) to 0 to enable all interrupts
    write_reg(LAPIC_REG_TPR, 0);

    return true;
}

void Apic::init_timer(uint32_t count) {
    // Set Divide Configuration Register to divide-by-16 (value 0x03)
    write_reg(0x3E0, 0x03);
    // Set LVT Timer Register: Vector 32 (timer), Periodic Mode (bit 17)
    write_reg(LAPIC_REG_LVT_TMR, 32 | (1 << 17));
    // Set Initial Count Register to trigger tick countdown
    write_reg(0x380, count);
}

void Apic::route_irq(uint8_t irq, uint8_t vector) {
    uint8_t reg_low = 0x10 + 2 * irq;
    uint8_t reg_high = 0x10 + 2 * irq + 1;
    
    // Set low 32-bits: vector number, fixed delivery, edge-triggered, active high, unmasked (0)
    ioapic_write(reg_low, vector);
    // Set high 32-bits: destination APIC ID 0
    ioapic_write(reg_high, 0);

    Serial::print("[IOAPIC] Routed IRQ ");
    char buf_irq[8];
    buf_irq[0] = '0' + (irq / 10);
    buf_irq[1] = '0' + (irq % 10);
    buf_irq[2] = '\0';
    Serial::print(buf_irq);
    Serial::print(" -> Vector ");
    char buf_vec[8];
    buf_vec[0] = '0' + (vector / 10);
    buf_vec[1] = '0' + (vector % 10);
    buf_vec[2] = '\0';
    Serial::println(buf_vec);
}

} // namespace drivers
