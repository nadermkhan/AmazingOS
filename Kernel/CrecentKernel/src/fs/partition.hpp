#pragma once

#include "../kernel/types.hpp"

namespace fs {

struct MbrPartitionEntry {
    uint8_t  boot_indicator;
    uint8_t  start_chs[3];
    uint8_t  system_id;
    uint8_t  end_chs[3];
    uint32_t start_lba;
    uint32_t total_sectors;
} __attribute__((packed));

struct MbrSector {
    uint8_t  boot_code[446];
    MbrPartitionEntry partitions[4];
    uint16_t signature;
} __attribute__((packed));

struct GptHeader {
    uint8_t  signature[8]; // "EFI PART"
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved0;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entries_lba;
    uint32_t num_partition_entries;
    uint32_t partition_entry_size;
    uint32_t partition_entries_crc32;
} __attribute__((packed));

struct GptPartitionEntry {
    uint8_t  partition_type_guid[16];
    uint8_t  unique_partition_guid[16];
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t partition_name[36]; // UTF-16LE
} __attribute__((packed));

class Partition {
public:
    static uint64_t find_exfat_partition();
};

} // namespace fs
