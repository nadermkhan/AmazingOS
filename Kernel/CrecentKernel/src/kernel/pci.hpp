#pragma once

#include "types.hpp"

namespace kernel {

struct PCIDevice {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass_code;
    uint32_t bar0;
    uint32_t bar1;
    uint8_t irq;
};

void pci_init();
bool pci_find_device(uint16_t vendor_id, uint16_t device_id, PCIDevice& dev);
bool pci_find_class(uint8_t class_code, uint8_t subclass_code, PCIDevice& dev);

uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
uint16_t pci_read_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);

} // namespace kernel
