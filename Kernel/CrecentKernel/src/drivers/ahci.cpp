#include "ahci.hpp"
#include "../kernel/vmm.hpp"
#include "../kernel/pmm.hpp"
#include "serial.hpp"

namespace drivers {

int Ahci::active_port = -1;
static HBAMemory* hba = nullptr;
static uint64_t port_cmd_table_phys[32] = {0};

// Helper: Stop port command engine
static void port_stop(HBAPort* port) {
    port->cmd &= ~0x0001; // Clear ST (Start)
    port->cmd &= ~0x0010; // Clear FRE (FIS Receive Enable)

    // Wait until ST and CR (Command List Running) are cleared
    while (1) {
        __asm__ __volatile__ ("pause");
        if (port->cmd & 0x8000) continue; // CR running
        if (port->cmd & 0x0010) continue; // FRE running
        break;
    }
}

// Helper: Start port command engine
static void port_start(HBAPort* port) {
    // Wait until CR is cleared
    while (port->cmd & 0x8000) {
        __asm__ __volatile__ ("pause");
    }

    port->cmd |= 0x0010; // Set FRE
    port->cmd |= 0x0001; // Set ST
}

static void print_dec(uint32_t val) {
    if (val == 0) {
        Serial::print("0");
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
    Serial::print(buf);
}

bool Ahci::init() {
    kernel::PCIDevice dev;
    if (!kernel::pci_find_class(0x01, 0x06, dev)) {
        Serial::println("[AHCI] SATA AHCI controller not found!");
        return false;
    }

    Serial::print("[AHCI] SATA AHCI controller found: Bus ");
    print_dec(dev.bus);
    Serial::print(", Slot ");
    print_dec(dev.slot);
    Serial::println("");

    // AHCI registers are at BAR5 (offset 0x24)
    uint32_t bar5 = kernel::pci_read_config_dword(dev.bus, dev.slot, dev.func, 0x24);
    uint64_t mmio_phys = bar5 & ~0x7FFULL; // AHCI base address is 2KB aligned

    Serial::print("[AHCI] Mapping BAR5 registers at physical: 0x");
    const char* hex_chars = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        char c[2] = { hex_chars[(mmio_phys >> (i * 8 + 4)) & 0xF], '\0' };
        Serial::print(c);
        c[0] = hex_chars[(mmio_phys >> (i * 8)) & 0xF];
        Serial::print(c);
    }
    Serial::println("");

    // Map 4KB MMIO region
    hba = (HBAMemory*)mmio_phys;
    for (uint64_t offset = 0; offset < 4096; offset += 4096) {
        kernel::vmm_map_page(mmio_phys + offset, mmio_phys + offset, 
                             kernel::VMM_FLAG_PRESENT | kernel::VMM_FLAG_WRITABLE | kernel::VMM_FLAG_CACHE_DIS);
    }

    // Enable bus mastering
    uint16_t pci_cmd = kernel::pci_read_config_word(dev.bus, dev.slot, dev.func, 0x04);
    pci_cmd |= (1 << 2) | (1 << 1); // Enable Bus Mastering & Memory Space
    kernel::pci_write_config_word(dev.bus, dev.slot, dev.func, 0x04, pci_cmd);

    // Reset HBA controller
    hba->ghc |= (1ULL << 31); // AE (AHCI Enable)
    hba->ghc |= 1;            // HR (HBA Reset)
    while (hba->ghc & 1) {
        __asm__ __volatile__ ("pause");
    }
    hba->ghc |= (1ULL << 31); // Ensure AE is set

    // Discover active SATA drives
    active_port = -1;
    uint32_t pi = hba->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            HBAPort* port = &hba->ports[i];
            uint32_t ssts = port->ssts;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            uint8_t det = ssts & 0x0F;

            // DET == 3: device present and communication established
            // IPM == 1: active state
            if (det == 3 && ipm == 1) {
                active_port = i;
                Serial::print("[AHCI] Active SATA device found on Port ");
                print_dec(i);
                Serial::println("");
                break;
            }
        }
    }

    if (active_port == -1) {
        Serial::println("[AHCI] No active SATA drives connected!");
        return false;
    }

    // Configure the active port
    HBAPort* port = &hba->ports[active_port];
    port_stop(port);

    // Allocate 1 page (4096 bytes) for commands/FIS tables
    uint64_t mem_phys = kernel::pmm_alloc_frame();
    port_cmd_table_phys[active_port] = mem_phys;

    // Zero out the entire allocated page to clean garbage registers/flags
    char* mem_virt = phys_to_virt<char>(mem_phys);
    for (int i = 0; i < 4096; i++) mem_virt[i] = 0;

    // Split the page:
    // Offset 0: Command List (1024 bytes)
    // Offset 1024: Received FIS (256 bytes)
    // Offset 1280: Command Table (256 bytes)
    port->clb = (uint32_t)(mem_phys & 0xFFFFFFFF);
    port->clbu = (uint32_t)(mem_phys >> 32);

    uint64_t fis_phys = mem_phys + 1024;
    port->fb = (uint32_t)(fis_phys & 0xFFFFFFFF);
    port->fbu = (uint32_t)(fis_phys >> 32);

    // Configure Command Header 0
    AHCICommandHeader* cmd_header = phys_to_virt<AHCICommandHeader>(mem_phys);
    uint64_t ctba_phys = mem_phys + 1280;
    cmd_header[0].ctba = (uint32_t)(ctba_phys & 0xFFFFFFFF);
    cmd_header[0].ctbau = (uint32_t)(ctba_phys >> 32);
    cmd_header[0].prdtl = 1; // 1 PRDT entry

    // Start Port command engine
    port_start(port);

    Serial::println("[AHCI] HBA controller and SATA disk initialization complete.");
    return true;
}

bool Ahci::read_sectors(uint64_t lba, uint32_t count, void* buffer) {
    if (active_port == -1) return false;

    HBAPort* port = &hba->ports[active_port];
    port->is = 0xFFFFFFFF; // Clear pending interrupts

    uint64_t mem_phys = port_cmd_table_phys[active_port];
    AHCICommandHeader* cmd_header = phys_to_virt<AHCICommandHeader>(mem_phys);
    cmd_header[0].cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t); // FIS length in dwords
    cmd_header[0].w = 0; // Read from device
    cmd_header[0].prdtl = 1;

    // Get virtual pointer to command table
    uint64_t ctba_phys = mem_phys + 1280;
    AHCICommandTable* cmd_table = phys_to_virt<AHCICommandTable>(ctba_phys);
    
    // Clear command table
    char* table_bytes = (char*)cmd_table;
    for (size_t i = 0; i < sizeof(AHCICommandTable); i++) table_bytes[i] = 0;

    // Setup PRDT
    uint64_t buffer_phys = kernel::vmm_get_phys((uint64_t)buffer);
    cmd_table->prdt_entry[0].dba = (uint32_t)(buffer_phys & 0xFFFFFFFF);
    cmd_table->prdt_entry[0].dbau = (uint32_t)(buffer_phys >> 32);
    cmd_table->prdt_entry[0].dbc = (count * 512) - 1; // Byte count minus 1
    cmd_table->prdt_entry[0].i = 1; // Interrupt on completion

    // Setup Command FIS
    FIS_REG_H2D* fis = (FIS_REG_H2D*)(cmd_table->cfis);
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1; // Command
    fis->command = 0x25; // READ DMA EXT (LBA48)

    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->device = (1 << 6); // LBA mode

    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);

    fis->countl = (uint8_t)(count & 0xFF);
    fis->counth = (uint8_t)((count >> 8) & 0xFF);

    // Wait until port is not busy
    int spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) {
        spin++;
        __asm__ __volatile__ ("pause");
    }
    if (spin == 1000000) {
        Serial::println("[AHCI] Error: Port busy before read command issue.");
        return false;
    }

    // Issue command
    port->ci = 1;

    // Wait for completion
    while (1) {
        __asm__ __volatile__ ("pause");
        if ((port->ci & 1) == 0) break;
        if (port->is & (1 << 30)) { // Task File Error
            Serial::print("[AHCI] Error: SATA disk read transaction failed! lba: ");
            print_dec((uint32_t)lba);
            Serial::print(", count: ");
            print_dec(count);
            Serial::print(", buffer: 0x");
            // print hex manually since we don't want to bloat
            const char* hex = "0123456789ABCDEF";
            for (int h = 7; h >= 0; h--) {
                char c[2] = { hex[((uint64_t)buffer >> (h * 8 + 4)) & 0xF], '\0' };
                Serial::print(c);
                c[0] = hex[((uint64_t)buffer >> (h * 8)) & 0xF];
                Serial::print(c);
            }
            Serial::print(", phys: 0x");
            for (int h = 7; h >= 0; h--) {
                char c[2] = { hex[(buffer_phys >> (h * 8 + 4)) & 0xF], '\0' };
                Serial::print(c);
                c[0] = hex[(buffer_phys >> (h * 8)) & 0xF];
                Serial::print(c);
            }
            Serial::print(", port->is: 0x");
            for (int h = 3; h >= 0; h--) {
                char c[2] = { hex[(port->is >> (h * 8 + 4)) & 0xF], '\0' };
                Serial::print(c);
                c[0] = hex[(port->is >> (h * 8)) & 0xF];
                Serial::print(c);
            }
            Serial::print(", port->tfd: 0x");
            for (int h = 3; h >= 0; h--) {
                char c[2] = { hex[(port->tfd >> (h * 8 + 4)) & 0xF], '\0' };
                Serial::print(c);
                c[0] = hex[(port->tfd >> (h * 8)) & 0xF];
                Serial::print(c);
            }
            Serial::println("");
            return false;
        }
    }

    return true;
}

bool Ahci::write_sectors(uint64_t lba, uint32_t count, const void* buffer) {
    if (active_port == -1) return false;

    HBAPort* port = &hba->ports[active_port];
    port->is = 0xFFFFFFFF; // Clear pending interrupts

    uint64_t mem_phys = port_cmd_table_phys[active_port];
    AHCICommandHeader* cmd_header = phys_to_virt<AHCICommandHeader>(mem_phys);
    cmd_header[0].cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t); // FIS length in dwords
    cmd_header[0].w = 1; // Write to device
    cmd_header[0].prdtl = 1;

    // Get virtual pointer to command table
    uint64_t ctba_phys = mem_phys + 1280;
    AHCICommandTable* cmd_table = phys_to_virt<AHCICommandTable>(ctba_phys);
    
    // Clear command table
    char* table_bytes = (char*)cmd_table;
    for (size_t i = 0; i < sizeof(AHCICommandTable); i++) table_bytes[i] = 0;

    // Setup PRDT
    uint64_t buffer_phys = kernel::vmm_get_phys((uint64_t)buffer);
    cmd_table->prdt_entry[0].dba = (uint32_t)(buffer_phys & 0xFFFFFFFF);
    cmd_table->prdt_entry[0].dbau = (uint32_t)(buffer_phys >> 32);
    cmd_table->prdt_entry[0].dbc = (count * 512) - 1; // Byte count minus 1
    cmd_table->prdt_entry[0].i = 1; // Interrupt on completion

    // Setup Command FIS
    FIS_REG_H2D* fis = (FIS_REG_H2D*)(cmd_table->cfis);
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1; // Command
    fis->command = 0x35; // WRITE DMA EXT (LBA48)

    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->device = (1 << 6); // LBA mode

    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);

    fis->countl = (uint8_t)(count & 0xFF);
    fis->counth = (uint8_t)((count >> 8) & 0xFF);

    // Wait until port is not busy
    int spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) {
        spin++;
        __asm__ __volatile__ ("pause");
    }
    if (spin == 1000000) {
        Serial::println("[AHCI] Error: Port busy before write command issue.");
        return false;
    }

    // Issue command
    port->ci = 1;

    // Wait for completion
    while (1) {
        __asm__ __volatile__ ("pause");
        if ((port->ci & 1) == 0) break;
        if (port->is & (1 << 30)) { // Task File Error
            Serial::println("[AHCI] Error: SATA disk write transaction failed!");
            return false;
        }
    }

    return true;
}

} // namespace drivers
