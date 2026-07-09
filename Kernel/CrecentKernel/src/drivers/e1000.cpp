#include "e1000.hpp"
#include "../kernel/pci.hpp"
#include "../kernel/vmm.hpp"
#include "../kernel/pmm.hpp"
#include "serial.hpp"

namespace drivers {

struct RxDescriptor {
    uint64_t address;     // Buffer physical address
    uint16_t length;      // Length of packet
    uint16_t checksum;    // Packet checksum
    uint8_t status;       // Descriptor status
    uint8_t errors;       // Descriptor errors
    uint16_t special;     // Special info
} __attribute__((packed));

struct TxDescriptor {
    uint64_t address;     // Buffer physical address
    uint16_t length;      // Length of packet data
    uint8_t cso;          // Checksum offset
    uint8_t cmd;          // Command byte
    uint8_t status;       // Descriptor status
    uint8_t css;          // Checksum start
    uint16_t special;     // Special info
} __attribute__((packed));

static uint64_t e1000_mmio_virt = 0;
static RxDescriptor* rx_ring = nullptr;
static TxDescriptor* tx_ring = nullptr;
static uint8_t mac_address[6] = {0};

static inline void e1000_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(e1000_mmio_virt + reg) = val;
}

static inline uint32_t e1000_read(uint32_t reg) {
    return *(volatile uint32_t*)(e1000_mmio_virt + reg);
}

void e1000_get_mac(uint8_t* mac) {
    for (int i = 0; i < 6; i++) mac[i] = mac_address[i];
}

bool e1000_init() {
    kernel::PCIDevice dev;
    if (!kernel::pci_find_device(0x8086, 0x100E, dev)) {
        Serial::println("[e1000] Intel e1000 network card not found!");
        return false;
    }

    Serial::println("[e1000] Intel e1000 network card found on PCI.");

    // 1. Enable MMIO & Bus Mastering
    uint16_t cmd = kernel::pci_read_config_word(dev.bus, dev.slot, dev.func, 0x04);
    cmd |= (1 << 1) | (1 << 2);
    kernel::pci_write_config_word(dev.bus, dev.slot, dev.func, 0x04, cmd);

    // 2. Map MMIO registers with cache disable
    uint64_t mmio_phys = dev.bar0 & ~0xFULL;
    e1000_mmio_virt = mmio_phys;
    for (uint64_t offset = 0; offset < 0x20000; offset += 4096) {
        kernel::vmm_map_page(mmio_phys + offset, mmio_phys + offset, 
                             kernel::VMM_FLAG_PRESENT | kernel::VMM_FLAG_WRITABLE | kernel::VMM_FLAG_CACHE_DIS);
    }

    // 3. Read MAC Address
    uint32_t ral = e1000_read(0x5400);
    uint32_t rah = e1000_read(0x5404);
    if (!(rah & (1ULL << 31))) {
        Serial::println("[e1000] Error: MAC address in RAL/RAH registers invalid!");
        return false;
    }
    mac_address[0] = (uint8_t)(ral & 0xFF);
    mac_address[1] = (uint8_t)((ral >> 8) & 0xFF);
    mac_address[2] = (uint8_t)((ral >> 16) & 0xFF);
    mac_address[3] = (uint8_t)((ral >> 24) & 0xFF);
    mac_address[4] = (uint8_t)(rah & 0xFF);
    mac_address[5] = (uint8_t)((rah >> 8) & 0xFF);

    Serial::print("[e1000] MAC Address: ");
    const char* hex_chars = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        char buf[3];
        buf[0] = hex_chars[(mac_address[i] >> 4) & 0x0F];
        buf[1] = hex_chars[mac_address[i] & 0x0F];
        buf[2] = '\0';
        Serial::print(buf);
        if (i < 5) Serial::print(":");
    }
    Serial::println("");

    // 4. Initialize Multicast Table Array
    for (int i = 0; i < 128; i++) {
        e1000_write(0x5200 + i * 4, 0);
    }

    // 5. Setup Rx Ring
    uint64_t rx_ring_phys = kernel::pmm_alloc_frame();
    rx_ring = phys_to_virt<RxDescriptor>(rx_ring_phys);
    for (int i = 0; i < 128; i++) {
        uint64_t rx_buf = kernel::pmm_alloc_frame();
        rx_ring[i].address = rx_buf;
        rx_ring[i].status = 0;
    }

    e1000_write(0x2800, (uint32_t)(rx_ring_phys & 0xFFFFFFFF));
    e1000_write(0x2804, (uint32_t)(rx_ring_phys >> 32));
    e1000_write(0x2808, 128 * sizeof(RxDescriptor));
    e1000_write(0x2810, 0);   // RDH
    e1000_write(0x2818, 127); // RDT

    // Enable Receiver (EN, SBP, UPE, MPE, BAM, size=2048)
    e1000_write(0x0100, (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 15));

    // 6. Setup Tx Ring
    uint64_t tx_ring_phys = kernel::pmm_alloc_frame();
    tx_ring = phys_to_virt<TxDescriptor>(tx_ring_phys);
    for (int i = 0; i < 128; i++) {
        uint64_t tx_buf = kernel::pmm_alloc_frame();
        tx_ring[i].address = tx_buf;
        tx_ring[i].status = 1; // Mark initially done
    }

    e1000_write(0x3800, (uint32_t)(tx_ring_phys & 0xFFFFFFFF));
    e1000_write(0x3804, (uint32_t)(tx_ring_phys >> 32));
    e1000_write(0x3808, 128 * sizeof(TxDescriptor));
    e1000_write(0x3810, 0); // TDH
    e1000_write(0x3818, 0); // TDT

    // Enable Transmitter (EN, PSP, COLD=0x40, CT=0x0F)
    e1000_write(0x0400, (1 << 1) | (1 << 3) | (0x0F << 4) | (0x40 << 12));

    // 7. Clear and Disable Interrupts (polling mode is used for high reliability)
    e1000_write(0x00D8, 0xFFFFFFFF); // IMC (Interrupt Mask Clear)

    Serial::println("[e1000] Intel e1000 Gigabit Ethernet card driver loaded successfully.");
    return true;
}

bool e1000_send_packet(const void* data, uint16_t length) {
    if (length > 1518) return false;

    uint32_t tail = e1000_read(0x3818); // TDT
    TxDescriptor* desc = &tx_ring[tail];

    // Copy packet into the DMA buffer
    char* buf_virt = phys_to_virt<char>(desc->address);
    for (uint16_t i = 0; i < length; i++) {
        buf_virt[i] = ((const char*)data)[i];
    }

    desc->length = length;
    desc->cmd = (1 << 0) | (1 << 1) | (1 << 3); // EOP (End of Packet), IFCS (Insert FCS), RS (Report Status)
    desc->status = 0;

    // Advance Tx Tail pointer
    uint32_t next_tail = (tail + 1) % 128;
    e1000_write(0x3818, next_tail);

    // Poll Tx Descriptor Done status bit (DD)
    while (!(desc->status & (1 << 0))) {
        __asm__ __volatile__ ("pause");
    }

    return true;
}

uint16_t e1000_recv_packet(void* dest, uint16_t max_len) {
    uint32_t tail = e1000_read(0x2818); // RDT
    uint32_t current = (tail + 1) % 128;

    RxDescriptor* desc = &rx_ring[current];
    if (!(desc->status & (1 << 0))) {
        // No packets in queue (DD bit not set)
        return 0;
    }

    uint16_t len = desc->length;
    if (len > max_len) len = max_len;

    // Copy packet out from the DMA buffer
    char* buf_virt = phys_to_virt<char>(desc->address);
    for (uint16_t i = 0; i < len; i++) {
        ((char*)dest)[i] = buf_virt[i];
    }

    // Clear descriptor status for hardware reuse
    desc->status = 0;

    // Set tail to the descriptor we just read, giving it back to the controller
    e1000_write(0x2818, current);

    return len;
}

} // namespace drivers
