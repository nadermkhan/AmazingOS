#include "exfat.hpp"
#include "../drivers/ahci.hpp"
#include "../drivers/serial.hpp"
#include "../kernel/heap.hpp"

namespace fs {

static uint64_t partition_start_lba = 0;
static uint32_t bytes_per_sector = 512;
static uint32_t sectors_per_cluster = 8;
static uint32_t cluster_size = 4096;
static uint32_t fat_start_sector = 0;
static uint32_t cluster_heap_start_sector = 0;
static uint32_t root_dir_cluster = 0;
static uint32_t cluster_count = 0;

static VFSNode exfat_root_node;
static constexpr size_t MAX_EXFAT_NODES = 64;
static VFSNode* exfat_nodes[MAX_EXFAT_NODES];
static size_t exfat_node_count = 0;

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

struct ExfatFileInfo {
    uint32_t first_cluster;
    uint32_t flags; // Bit 1: NoFatChain (1 = contiguous, 0 = FAT-chained)
    uint64_t size;
};

// Helper: Get next cluster in the FAT chain
static uint32_t get_next_cluster(uint32_t cluster) {
    if (cluster < 2) return 0xFFFFFFFF;

    // Sector containing this cluster entry in the FAT
    uint32_t fat_sector = fat_start_sector + (cluster * 4) / bytes_per_sector;
    uint32_t offset = (cluster * 4) % bytes_per_sector;

    alignas(16) uint8_t sector_buf[512];
    if (!drivers::Ahci::read_sectors(fat_sector, 1, sector_buf)) {
        drivers::Serial::println("[exFAT] Error: Failed to read FAT sector.");
        return 0xFFFFFFFF;
    }

    return *(uint32_t*)(sector_buf + offset);
}

// Helper: Traverse to the N-th cluster in a file's chain
static uint32_t traverse_clusters(uint32_t start_cluster, uint32_t flags, uint32_t target_index) {
    uint32_t current = start_cluster;
    
    // Contiguous mapping bypass
    if (flags & 0x02) {
        return start_cluster + target_index;
    }

    for (uint32_t i = 0; i < target_index; i++) {
        current = get_next_cluster(current);
        if (current >= 0xFFFFFFF8) {
            return 0xFFFFFFFF;
        }
    }
    return current;
}

// VFS finddir implementation for exFAT
VFSNode* Exfat::finddir(VFSNode* node, const char* name) {
    if (!node || node->type != NodeType::DIRECTORY) return nullptr;

    // Split name into first component and remainder
    char component[128];
    int comp_len = 0;
    while (name[comp_len] && name[comp_len] != '/') {
        if (comp_len < 127) {
            component[comp_len] = name[comp_len];
        }
        comp_len++;
    }
    component[comp_len] = '\0';

    const char* remainder = name + comp_len;
    while (*remainder == '/') remainder++;

    ExfatFileInfo* info = (ExfatFileInfo*)node->data;
    uint32_t current_cluster = info->first_cluster;
    uint32_t flags = info->flags;

    alignas(16) uint8_t cluster_buf[4096];
    char file_name_buf[256];
    int file_name_len = 0;

    // File entry set state variables
    bool entry_set_active = false;
    uint16_t file_attributes = 0;
    uint32_t entry_first_cluster = 0;
    uint8_t  entry_flags = 0;
    uint64_t entry_data_length = 0;

    while (current_cluster < 0xFFFFFFF8) {
        uint64_t sector = cluster_heap_start_sector + (current_cluster - 2) * sectors_per_cluster;
        if (!drivers::Ahci::read_sectors(sector, sectors_per_cluster, cluster_buf)) {
            drivers::Serial::println("[exFAT] Error: Failed to read directory cluster.");
            return nullptr;
        }

        // Loop through 32-byte directory entries inside the cluster
        for (uint32_t offset = 0; offset < cluster_size; offset += 32) {
            ExfatDirHeader* header = (ExfatDirHeader*)(cluster_buf + offset);

            if (header->entry_type == 0x00) {
                // End of directory entries
                break;
            }

            if (header->entry_type == 0x85) { // File Directory Entry
                ExfatFileEntry* file = (ExfatFileEntry*)header;
                file_attributes = file->file_attributes;
                entry_set_active = true;
                file_name_len = 0;
                file_name_buf[0] = '\0';
                continue;
            }

            if (header->entry_type == 0xC0 && entry_set_active) { // Stream Extension Entry
                ExfatStreamEntry* stream = (ExfatStreamEntry*)header;
                entry_flags = stream->flags;
                entry_first_cluster = stream->first_cluster;
                entry_data_length = stream->data_length;
                continue;
            }

            if (header->entry_type == 0xC1 && entry_set_active) { // File Name Entry
                ExfatNameEntry* name_entry = (ExfatNameEntry*)header;
                for (int i = 0; i < 15; i++) {
                    uint16_t uchar = name_entry->name_part[i];
                    if (uchar == 0) break;
                    // Simple conversion from UTF-16 to ASCII
                    if (file_name_len < 254) {
                        file_name_buf[file_name_len++] = (char)(uchar & 0xFF);
                    }
                }
                file_name_buf[file_name_len] = '\0';

                // Check if this completes the entry block
                // Compare name with target component
                const char* s1 = file_name_buf;
                const char* s2 = component;
                bool name_match = true;
                while (*s1 && *s2) {
                    if (*s1 != *s2) {
                        name_match = false;
                        break;
                    }
                    s1++;
                    s2++;
                }
                if (*s1 != *s2) name_match = false;

                if (name_match) {
                    VFSNode* matched_node = nullptr;
                    // Check if node is already loaded
                    for (size_t n = 0; n < exfat_node_count; n++) {
                        if (exfat_nodes[n]->type == (file_attributes & 0x10 ? NodeType::DIRECTORY : NodeType::FILE)) {
                            // Match by name
                            const char* n1 = exfat_nodes[n]->name;
                            const char* n2 = file_name_buf;
                            bool match_loaded = true;
                            while (*n1 && *n2) {
                                if (*n1 != *n2) { match_loaded = false; break; }
                                n1++; n2++;
                            }
                            if (*n1 != *n2) match_loaded = false;
                            if (match_loaded) {
                                matched_node = exfat_nodes[n];
                                break;
                            }
                        }
                    }

                    if (!matched_node) {
                        // Create new VFSNode
                        if (exfat_node_count >= MAX_EXFAT_NODES) return nullptr;

                        VFSNode* new_node = new VFSNode();
                        int name_idx = 0;
                        for (; name_idx < file_name_len && name_idx < 127; name_idx++) {
                            new_node->name[name_idx] = file_name_buf[name_idx];
                        }
                        new_node->name[name_idx] = '\0';
                        new_node->type = (file_attributes & 0x10) ? NodeType::DIRECTORY : NodeType::FILE;
                        new_node->size = entry_data_length;
                        new_node->capacity = entry_data_length;

                        ExfatFileInfo* node_info = new ExfatFileInfo();
                        node_info->first_cluster = entry_first_cluster;
                        node_info->flags = entry_flags;
                        node_info->size = entry_data_length;
                        new_node->data = (char*)node_info;

                        new_node->read = Exfat::read;
                        new_node->write = Exfat::write;
                        new_node->finddir = Exfat::finddir;

                        exfat_nodes[exfat_node_count++] = new_node;
                        matched_node = new_node;
                    }

                    if (remainder[0] == '\0') {
                        return matched_node;
                    } else if (matched_node->type == NodeType::DIRECTORY) {
                        return Exfat::finddir(matched_node, remainder);
                    } else {
                        return nullptr;
                    }
                }
                continue;
            }

            // Unrecognized entry resets set active state
            if (!(header->entry_type & 0x80)) {
                entry_set_active = false;
            }
        }

        // Advance to next directory cluster
        if (flags & 0x02) {
            current_cluster++;
        } else {
            current_cluster = get_next_cluster(current_cluster);
        }
    }

    return nullptr;
}

// VFS read implementation for exFAT
ssize_t Exfat::read(VFSNode* node, size_t offset, void* buffer, size_t count) {
    if (!node || !buffer || !node->data) return -1;

    ExfatFileInfo* info = (ExfatFileInfo*)node->data;
    if (offset >= info->size) return 0;
    if (offset + count > info->size) {
        count = info->size - offset;
    }

    uint32_t cluster_index = offset / cluster_size;
    uint32_t cluster_offset = offset % cluster_size;

    uint32_t current_cluster = traverse_clusters(info->first_cluster, info->flags, cluster_index);
    if (current_cluster >= 0xFFFFFFF8) return -1;

    alignas(16) uint8_t cluster_buf[4096];
    size_t bytes_read = 0;

    while (bytes_read < count) {
        uint64_t sector = cluster_heap_start_sector + (current_cluster - 2) * sectors_per_cluster;
        if (!drivers::Ahci::read_sectors(sector, sectors_per_cluster, cluster_buf)) {
            drivers::Serial::println("[exFAT] Error: Failed to read file data cluster.");
            return -1;
        }

        size_t chunk = cluster_size - cluster_offset;
        if (chunk > count - bytes_read) {
            chunk = count - bytes_read;
        }

        char* dest = (char*)buffer + bytes_read;
        char* src = (char*)cluster_buf + cluster_offset;
        for (size_t i = 0; i < chunk; i++) {
            dest[i] = src[i];
        }

        bytes_read += chunk;
        cluster_offset = 0; // Reset offset for subsequent clusters

        if (bytes_read < count) {
            if (info->flags & 0x02) {
                current_cluster++;
            } else {
                current_cluster = get_next_cluster(current_cluster);
            }
            if (current_cluster >= 0xFFFFFFF8) break;
        }
    }

    return (ssize_t)bytes_read;
}

// VFS write implementation for exFAT
ssize_t Exfat::write(VFSNode* node, size_t offset, const void* buffer, size_t count) {
    if (!node || !buffer || !node->data) return -1;

    ExfatFileInfo* info = (ExfatFileInfo*)node->data;
    uint32_t cluster_index = offset / cluster_size;
    uint32_t cluster_offset = offset % cluster_size;

    uint32_t current_cluster = traverse_clusters(info->first_cluster, info->flags, cluster_index);
    if (current_cluster >= 0xFFFFFFF8) {
        drivers::Serial::println("[exFAT] Error: Write dynamic cluster expansion not supported in this revision.");
        return -1;
    }

    alignas(16) uint8_t cluster_buf[4096];
    size_t bytes_written = 0;

    while (bytes_written < count) {
        uint64_t sector = cluster_heap_start_sector + (current_cluster - 2) * sectors_per_cluster;

        // If performing partial cluster overwrite, read current contents first
        if (cluster_offset > 0 || (count - bytes_written) < cluster_size) {
            if (!drivers::Ahci::read_sectors(sector, sectors_per_cluster, cluster_buf)) {
                return -1;
            }
        }

        size_t chunk = cluster_size - cluster_offset;
        if (chunk > count - bytes_written) {
            chunk = count - bytes_written;
        }

        char* dest = (char*)cluster_buf + cluster_offset;
        const char* src = (const char*)buffer + bytes_written;
        for (size_t i = 0; i < chunk; i++) {
            dest[i] = src[i];
        }

        if (!drivers::Ahci::write_sectors(sector, sectors_per_cluster, cluster_buf)) {
            drivers::Serial::println("[exFAT] Error: Failed to write file data cluster.");
            return -1;
        }

        bytes_written += chunk;
        cluster_offset = 0;

        if (bytes_written < count) {
            if (info->flags & 0x02) {
                current_cluster++;
            } else {
                current_cluster = get_next_cluster(current_cluster);
            }
            if (current_cluster >= 0xFFFFFFF8) break;
        }
    }

    // Expand size locally if written beyond bounds
    if (offset + bytes_written > info->size) {
        info->size = offset + bytes_written;
        node->size = info->size;
        node->capacity = info->size;
    }

    return (ssize_t)bytes_written;
}

bool Exfat::init(uint64_t partition_lba) {
    partition_start_lba = partition_lba;
    
    // Read Partition boot sector (VBR)
    alignas(16) uint8_t vbr_buf[512];
    if (!drivers::Ahci::read_sectors(partition_start_lba, 1, vbr_buf)) {
        drivers::Serial::println("[exFAT] Error: Failed to read partition VBR.");
        return false;
    }

    ExfatVbr* vbr = (ExfatVbr*)vbr_buf;
    
    // Validate signature
    const char* fs_name = "EXFAT   ";
    for (int i = 0; i < 8; i++) {
        if (vbr->fs_name[i] != fs_name[i]) {
            drivers::Serial::println("[exFAT] Error: Invalid filesystem signature.");
            return false;
        }
    }

    bytes_per_sector = 1 << vbr->bytes_per_sector_shift;
    sectors_per_cluster = 1 << vbr->sectors_per_cluster_shift;
    cluster_size = sectors_per_cluster * bytes_per_sector;

    fat_start_sector = partition_start_lba + vbr->fat_offset;
    cluster_heap_start_sector = partition_start_lba + vbr->cluster_heap_offset;
    root_dir_cluster = vbr->root_dir_cluster;
    cluster_count = vbr->cluster_count;

    drivers::Serial::print("[exFAT] Filesystem mounted. Sector size: ");
    print_dec(bytes_per_sector);
    drivers::Serial::print(", Cluster size: ");
    print_dec(cluster_size);
    drivers::Serial::print(", Root Cluster: ");
    print_dec(root_dir_cluster);
    drivers::Serial::println("");

    // Initialize root mount node
    char root_name[] = "disk";
    int name_idx = 0;
    while (root_name[name_idx]) {
        exfat_root_node.name[name_idx] = root_name[name_idx];
        name_idx++;
    }
    exfat_root_node.name[name_idx] = '\0';
    exfat_root_node.type = NodeType::DIRECTORY;
    
    ExfatFileInfo* root_info = new ExfatFileInfo();
    root_info->first_cluster = root_dir_cluster;
    root_info->flags = 0x01; // FAT-chained directory base
    root_info->size = 0;
    exfat_root_node.data = (char*)root_info;

    exfat_root_node.read = Exfat::read;
    exfat_root_node.write = Exfat::write;
    exfat_root_node.finddir = Exfat::finddir;

    return true;
}

VFSNode* Exfat::get_root_node() {
    return &exfat_root_node;
}

} // namespace fs
