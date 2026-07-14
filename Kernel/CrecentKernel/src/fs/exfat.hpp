#pragma once

#include "../kernel/types.hpp"
#include "vfs.hpp"

namespace fs {

struct ExfatVbr {
    uint8_t  jmp_boot[3];
    uint8_t  fs_name[8]; // "EXFAT   "
    uint8_t  must_be_zero[53];
    uint64_t partition_offset;
    uint64_t volume_length;
    uint32_t fat_offset;
    uint32_t fat_length;
    uint32_t cluster_heap_offset;
    uint32_t cluster_count;
    uint32_t root_dir_cluster;
    uint32_t volume_serial_number;
    uint16_t fs_revision;
    uint16_t volume_flags;
    uint8_t  bytes_per_sector_shift;
    uint8_t  sectors_per_cluster_shift;
    uint8_t  num_fats;
    uint8_t  drive_select;
    uint8_t  percent_in_use;
    uint8_t  reserved[7];
    uint8_t  boot_code[390];
    uint16_t boot_signature;
} __attribute__((packed));

struct ExfatDirHeader {
    uint8_t  entry_type;
    // Entry-specific fields
    uint8_t  custom[31];
} __attribute__((packed));

struct ExfatFileEntry {
    uint8_t  entry_type; // 0x85
    uint8_t  secondary_count;
    uint16_t set_checksum;
    uint16_t file_attributes;
    uint16_t reserved1;
    uint32_t create_timestamp;
    uint32_t last_modified_timestamp;
    uint32_t last_accessed_timestamp;
    uint8_t  create_10ms_increment;
    uint8_t  last_modified_10ms_increment;
    uint8_t  create_utc_offset;
    uint8_t  last_modified_utc_offset;
    uint8_t  last_accessed_utc_offset;
    uint8_t  reserved2[7];
} __attribute__((packed));

struct ExfatStreamEntry {
    uint8_t  entry_type; // 0xC0
    uint8_t  flags;      // Bit 1: NoFatChain (1 = contiguous, 0 = FAT-chained)
    uint8_t  reserved1;
    uint8_t  name_length;
    uint16_t name_hash;
    uint16_t reserved2;
    uint64_t valid_data_length;
    uint32_t reserved3;
    uint32_t first_cluster;
    uint64_t data_length;
} __attribute__((packed));

struct ExfatNameEntry {
    uint8_t  entry_type; // 0xC1
    uint8_t  flags;
    uint16_t name_part[15]; // UTF-16LE characters
} __attribute__((packed));

class Exfat {
public:
    static bool init(uint64_t partition_lba);
    static VFSNode* get_root_node();

    // VFS Interface wrappers
    static ssize_t read(VFSNode* node, size_t offset, void* buffer, size_t count);
    static ssize_t write(VFSNode* node, size_t offset, const void* buffer, size_t count);
    static VFSNode* finddir(VFSNode* node, const char* name);
    static int readdir(VFSNode* node, size_t index, VFSNode* entry_out);
};

} // namespace fs
