#include "ac97.hpp"
#include "serial.hpp"
#include "../kernel/pci.hpp"
#include "../kernel/vmm.hpp"

namespace drivers {

struct BDL_Entry {
    uint32_t address;
    uint16_t length;
    uint16_t control;
} __attribute__((packed));

static uint32_t nambar = 0;
static uint32_t nabmbar = 0;
static bool initialized = false;
static bool active = false;

// 4096-byte alignment guarantees physical alignment and contiguity
static BDL_Entry bdl[32] __attribute__((aligned(4096)));
static int16_t dma_buffer[32768] __attribute__((aligned(4096)));

bool AC97::init() {
    kernel::PCIDevice dev;
    
    // Scan for AC97 class (0x04, 0x01) or specifically Intel 82801AA (0x8086, 0x2415)
    if (!kernel::pci_find_class(0x04, 0x01, dev)) {
        if (!kernel::pci_find_device(0x8086, 0x2415, dev)) {
            Serial::println("[AC97] PCI Scan: No AC97 Audio Controller found.");
            return false;
        }
    }
    
    Serial::print("[AC97] Audio Controller found at PCI bus ");
    
    // Read BAR0 (NAMBAR) and BAR1 (NABMBAR)
    uint32_t bar0 = kernel::pci_read_config_dword(dev.bus, dev.slot, dev.func, 0x10);
    uint32_t bar1 = kernel::pci_read_config_dword(dev.bus, dev.slot, dev.func, 0x14);
    
    if (!(bar0 & 1) || !(bar1 & 1)) {
        Serial::println("[AC97] Error: BARs are not I/O mapped.");
        return false;
    }
    
    nambar = bar0 & ~3;
    nabmbar = bar1 & ~3;
    
    // Enable Bus Mastering and I/O access in PCI Command Register
    uint16_t cmd = kernel::pci_read_config_word(dev.bus, dev.slot, dev.func, 0x04);
    cmd |= (1 << 2) | (1 << 0);
    kernel::pci_write_config_word(dev.bus, dev.slot, dev.func, 0x04, cmd);
    
    // Perform warm reset of mixer
    outw(nambar + 0x00, 0x0000);
    
    // Unmute and set master volume and PCM output to max volume (0x0000)
    outw(nambar + 0x02, 0x0000);
    outw(nambar + 0x18, 0x0000);
    
    // Zero out BDL and DMA buffers
    for (int i = 0; i < 32; ++i) {
        bdl[i].address = 0;
        bdl[i].length = 0;
        bdl[i].control = 0;
    }
    for (int i = 0; i < 32768; ++i) {
        dma_buffer[i] = 0;
    }
    
    // Configure double-buffer BDL entry pointers
    uint32_t dma_phys = (uint32_t)kernel::vmm_get_phys((uint64_t)dma_buffer);
    
    bdl[0].address = dma_phys;
    bdl[0].length = 16384;   // 32KB in 16-bit words
    bdl[0].control = 0x8000; // Interrupt on completion
    
    bdl[1].address = dma_phys + 32768; // Second 32KB slice
    bdl[1].length = 16384;
    bdl[1].control = 0x8000;
    
    // Program Buffer Descriptor Base Address register
    uint32_t bdl_phys = (uint32_t)kernel::vmm_get_phys((uint64_t)bdl);
    outl(nabmbar + 0x10, bdl_phys);
    
    // Set Last Valid Index (LVI) to 1 (we use entries 0 and 1)
    outb(nabmbar + 0x15, 1);
    
    initialized = true;
    Serial::println("[AC97] Initialization successful. Hardware mixer unmuted.");
    return true;
}

void AC97::play(uint32_t phys_addr, uint32_t size_bytes) {
    (void)phys_addr;
    (void)size_bytes;
    if (!initialized) return;
    
    // Start playback: Run/Pause register (PO_CR offset 0x18) bit 0 = 1, bit 2 = 1 (IOC enable)
    outb(nabmbar + 0x18, 0x05);
    active = true;
}

void AC97::stop() {
    if (!initialized) return;
    
    // Write 0 to PO_CR to halt DMA engine
    outb(nabmbar + 0x18, 0x00);
    
    // Reset registers
    outb(nabmbar + 0x18, 0x02);
    
    active = false;
}

bool AC97::is_active() {
    return active;
}

uint8_t AC97::get_civ() {
    if (!initialized) return 0;
    return inb(nabmbar + 0x14);
}

void AC97::set_lvi(uint8_t lvi) {
    if (!initialized) return;
    outb(nabmbar + 0x15, lvi);
}

void AC97::write_buffer(int buffer_idx, const int16_t* samples, int word_count) {
    if (buffer_idx < 0 || buffer_idx > 1) return;
    int start_offset = buffer_idx * 16384;
    for (int i = 0; i < word_count && i < 16384; ++i) {
        dma_buffer[start_offset + i] = samples[i];
    }
}

} // namespace drivers
