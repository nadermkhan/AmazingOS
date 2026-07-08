#pragma once

#include "../kernel/types.hpp"

namespace drivers {

// Memory-Mapped I/O Register Base Address for Local APIC
constexpr uintptr_t LAPIC_BASE = 0xFEE00000;

// LAPIC Register offsets
constexpr uint32_t LAPIC_REG_ID      = 0x20;   // Local APIC ID
constexpr uint32_t LAPIC_REG_VER     = 0x30;   // Local APIC Version
constexpr uint32_t LAPIC_REG_TPR     = 0x80;   // Task Priority Register
constexpr uint32_t LAPIC_REG_EOI     = 0x0B0;  // End Of Interrupt
constexpr uint32_t LAPIC_REG_LDR     = 0x0D0;  // Logical Destination Register
constexpr uint32_t LAPIC_REG_DFR     = 0x0E0;  // Destination Format Register
constexpr uint32_t LAPIC_REG_SVR     = 0x0F0;  // Spurious Interrupt Vector Register
constexpr uint32_t LAPIC_REG_ESR     = 0x280;  // Error Status Register
constexpr uint32_t LAPIC_REG_ICR_LOW = 0x300;  // Interrupt Command Register Low
constexpr uint32_t LAPIC_REG_ICR_HIGH= 0x310;  // Interrupt Command Register High
constexpr uint32_t LAPIC_REG_LVT_TMR = 0x320;  // LVT Timer Register
constexpr uint32_t LAPIC_REG_LVT_LINT0 = 0x350;// LVT LINT0
constexpr uint32_t LAPIC_REG_LVT_LINT1 = 0x360;// LVT LINT1
constexpr uint32_t LAPIC_REG_LVT_ERR   = 0x370;// LVT Error

class Apic {
public:
    // Initialize Local APIC and disable legacy PIC
    static bool init();

    // Read a Local APIC MMIO register
    static uint32_t read_reg(uint32_t reg);

    // Write a Local APIC MMIO register
    static void write_reg(uint32_t reg, uint32_t val);

    // Send End Of Interrupt signal to LAPIC
    static void eoi();

private:
    // Mask and disable the legacy 8259 PIC controller
    static void disable_pic();
};

} // namespace drivers
