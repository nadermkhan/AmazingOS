#include "partition.hpp"
#include "../drivers/ahci.hpp"
#include "../drivers/serial.hpp"

namespace fs {

static const uint8_t EXFAT_GPT_GUID[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 
    0xE5, 0xB9, 
    0x33, 0x44, 
    0x87, 0xC0, 
    0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

uint64_t Partition::find_exfat_partition() {
    alignas(16) uint8_t sector[512];

    // Read Sector 0 (MBR)
    if (!drivers::Ahci::read_sectors(0, 1, sector)) {
        drivers::Serial::println("[PARTITION] Error: Failed to read sector 0 (MBR).");
        return 0;
    }

    MbrSector* mbr = (MbrSector*)sector;
    if (mbr->signature != 0xAA55) {
        drivers::Serial::println("[PARTITION] Warning: Invalid boot sector signature. Defaulting to sector 0.");
        return 0;
    }

    // Read Sector 1 (GPT Header)
    alignas(16) uint8_t gpt_sector[512];
    if (!drivers::Ahci::read_sectors(1, 1, gpt_sector)) {
        drivers::Serial::println("[PARTITION] Error: Failed to read sector 1 (GPT Header).");
        return 0;
    }

    GptHeader* gpt = (GptHeader*)gpt_sector;
    bool is_gpt = true;
    const char* gpt_sig = "EFI PART";
    for (int i = 0; i < 8; i++) {
        if (gpt->signature[i] != gpt_sig[i]) {
            is_gpt = false;
            break;
        }
    }

    if (is_gpt) {
        drivers::Serial::println("[PARTITION] GUID Partition Table (GPT) detected.");
        uint64_t entries_lba = gpt->partition_entries_lba;
        uint32_t num_entries = gpt->num_partition_entries;
        uint32_t entry_size = gpt->partition_entry_size;

        // Loop partition entries (usually 128 entries)
        alignas(16) uint8_t entry_buf[512];
        for (uint32_t i = 0; i < num_entries; i++) {
            uint32_t entries_per_sector = 512 / entry_size;
            uint32_t sector_offset = i / entries_per_sector;
            uint32_t entry_offset = (i % entries_per_sector) * entry_size;

            if (i % entries_per_sector == 0) {
                if (!drivers::Ahci::read_sectors(entries_lba + sector_offset, 1, entry_buf)) {
                    drivers::Serial::println("[PARTITION] Error: Failed to read GPT partition entry sector.");
                    return 0;
                }
            }

            GptPartitionEntry* entry = (GptPartitionEntry*)(entry_buf + entry_offset);
            
            // Check if entry GUID matches exFAT GUID
            bool match = true;
            for (int g = 0; g < 16; g++) {
                if (entry->partition_type_guid[g] != EXFAT_GPT_GUID[g]) {
                    match = false;
                    break;
                }
            }

            if (match) {
                drivers::Serial::print("[PARTITION] exFAT GPT Partition found at LBA 0x");
                // Print LBA address in hex
                const char* hex_chars = "0123456789ABCDEF";
                for (int h = 7; h >= 0; h--) {
                    char c[2] = { hex_chars[(entry->starting_lba >> (h * 8 + 4)) & 0xF], '\0' };
                    drivers::Serial::print(c);
                    c[0] = hex_chars[(entry->starting_lba >> (h * 8)) & 0xF];
                    drivers::Serial::print(c);
                }
                drivers::Serial::println("");
                return entry->starting_lba;
            }
        }
    } else {
        drivers::Serial::println("[PARTITION] Master Boot Record (MBR) layout detected.");
        // Look for partitions in MBR
        for (int i = 0; i < 4; i++) {
            MbrPartitionEntry& entry = mbr->partitions[i];
            // System ID 0x07 = exFAT / NTFS
            if (entry.system_id == 0x07) {
                drivers::Serial::print("[PARTITION] exFAT/NTFS MBR Partition found at LBA 0x");
                const char* hex_chars = "0123456789ABCDEF";
                uint64_t start = entry.start_lba;
                for (int h = 7; h >= 0; h--) {
                    char c[2] = { hex_chars[(start >> (h * 8 + 4)) & 0xF], '\0' };
                    drivers::Serial::print(c);
                    c[0] = hex_chars[(start >> (h * 8)) & 0xF];
                    drivers::Serial::print(c);
                }
                drivers::Serial::println("");
                return entry.start_lba;
            }
        }
    }

    drivers::Serial::println("[PARTITION] Warning: No partitions found matching exFAT. Defaulting to sector 0.");
    return 0;
}

} // namespace fs
