#include "pci.hpp"
#include "../drivers/serial.hpp"

namespace kernel {

// Port I/O helper functions
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ __volatile__ ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ __volatile__ ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static PCIDevice devices[32];
static size_t device_count = 0;

static void print_hex(uint32_t val, int digits) {
    const char* hex_chars = "0123456789ABCDEF";
    char buf[9];
    if (digits > 8) digits = 8;
    for (int i = 0; i < digits; i++) {
        buf[i] = hex_chars[(val >> ((digits - 1 - i) * 4)) & 0x0F];
    }
    buf[digits] = '\0';
    drivers::Serial::print(buf);
}

static void print_dec(uint32_t val) {
    if (val == 0) {
        drivers::Serial::print("0");
        return;
    }
    char buf[12];
    int idx = 0;
    while (val > 0) {
        buf[idx++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = 0; i < idx / 2; i++) {
        char temp = buf[i];
        buf[i] = buf[idx - 1 - i];
        buf[idx - 1 - i] = temp;
    }
    buf[idx] = '\0';
    drivers::Serial::print(buf);
}

uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)(((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    return inl(0xCFC);
}

void pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)(((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    outl(0xCFC, value);
}

uint16_t pci_read_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config_dword(bus, slot, func, offset);
    return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

void pci_write_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t address = (uint32_t)(((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    __asm__ __volatile__ ("outw %0, %1" : : "a"(value), "Nd"((uint16_t)(0xCFC + (offset & 2))));
}

void pci_init() {
    device_count = 0;
    drivers::Serial::println("[PCI] Scanning PCI bus...");

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            // Read vendor ID of function 0 to see if device exists
            uint16_t vendor_id = pci_read_config_word(bus, slot, 0, 0);
            if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
                continue;
            }

            // Determine if multi-function device
            uint8_t header_type = (uint8_t)(pci_read_config_word(bus, slot, 0, 0x0C) >> 8);
            int num_funcs = (header_type & 0x80) ? 8 : 1;

            for (int func = 0; func < num_funcs; func++) {
                vendor_id = pci_read_config_word(bus, slot, func, 0);
                if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
                    continue;
                }

                uint16_t device_id = pci_read_config_word(bus, slot, func, 2);
                uint16_t class_reg = pci_read_config_word(bus, slot, func, 0x0A);
                uint8_t class_code = (uint8_t)(class_reg >> 8);
                uint8_t subclass_code = (uint8_t)(class_reg & 0xFF);

                uint32_t bar0 = pci_read_config_dword(bus, slot, func, 0x10);
                uint32_t bar1 = pci_read_config_dword(bus, slot, func, 0x14);

                uint16_t irq_reg = pci_read_config_word(bus, slot, func, 0x3C);
                uint8_t irq = (uint8_t)(irq_reg & 0xFF);

                if (device_count < 32) {
                    devices[device_count] = {
                        (uint8_t)bus, (uint8_t)slot, (uint8_t)func,
                        vendor_id, device_id, class_code, subclass_code,
                        bar0, bar1, irq
                    };
                    device_count++;
                }

                drivers::Serial::print("  Found Device: Bus ");
                print_dec(bus);
                drivers::Serial::print(", Slot ");
                print_dec(slot);
                drivers::Serial::print(", Func ");
                print_dec(func);
                drivers::Serial::print(" -> Vendor: 0x");
                print_hex(vendor_id, 4);
                drivers::Serial::print(", Device: 0x");
                print_hex(device_id, 4);
                drivers::Serial::print(", Class: 0x");
                print_hex(class_code, 2);
                drivers::Serial::print(", Subclass: 0x");
                print_hex(subclass_code, 2);
                drivers::Serial::print(", BAR0: 0x");
                print_hex(bar0, 8);
                drivers::Serial::print(", IRQ: ");
                print_dec(irq);
                drivers::Serial::println("");
            }
        }
    }

    drivers::Serial::print("[PCI] Scan complete. Registered ");
    print_dec(device_count);
    drivers::Serial::println(" devices.");
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, PCIDevice& dev) {
    for (size_t i = 0; i < device_count; i++) {
        if (devices[i].vendor_id == vendor_id && devices[i].device_id == device_id) {
            dev = devices[i];
            return true;
        }
    }
    return false;
}

bool pci_find_class(uint8_t class_code, uint8_t subclass_code, PCIDevice& dev) {
    for (size_t i = 0; i < device_count; i++) {
        if (devices[i].class_code == class_code && devices[i].subclass_code == subclass_code) {
            dev = devices[i];
            return true;
        }
    }
    return false;
}

} // namespace kernel
