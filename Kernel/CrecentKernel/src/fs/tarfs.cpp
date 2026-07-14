#include "tarfs.hpp"
#include "vfs.hpp"
#include "../kernel/heap.hpp"
#include "../drivers/serial.hpp"

namespace fs {

struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

static constexpr size_t MAX_TAR_NODES = 128;
static VFSNode* tar_nodes[MAX_TAR_NODES];
static size_t tar_node_count = 0;
static VFSNode tarfs_root_node;

namespace {

// Internal helper: string copy
void str_copy(char* dest, const char* src, size_t max_len) {
    if (!dest || !src) return;
    size_t i = 0;
    for (; i < max_len - 1 && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

// Internal helper: string equality
bool str_equal(const char* s1, const char* s2) {
    if (!s1 || !s2) return false;
    while (*s1 && *s2) {
        if (*s1 != *s2) return false;
        s1++;
        s2++;
    }
    return *s1 == *s2;
}

size_t str_len(const char* s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

bool str_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    size_t i = 0;
    while (prefix[i]) {
        if (str[i] != prefix[i]) return false;
        i++;
    }
    return true;
}


// Parse octal string to decimal size
size_t parse_octal(const char* str, size_t size) {
    size_t val = 0;
    for (size_t i = 0; i < size; ++i) {
        if (str[i] == '\0' || str[i] == ' ') break;
        if (str[i] >= '0' && str[i] <= '7') {
            val = val * 8 + (str[i] - '0');
        }
    }
    return val;
}

// Memory-backed TarFS file read implementation
ssize_t tarfs_read(VFSNode* node, size_t offset, void* buffer, size_t count) {
    if (!node || !buffer || !node->data) return -1;
    if (offset >= node->size) return 0;
    if (offset + count > node->size) {
        count = node->size - offset;
    }

    char* dest = (char*)buffer;
    const char* src = node->data + offset;
    for (size_t i = 0; i < count; ++i) {
        dest[i] = src[i];
    }
    return (ssize_t)count;
}

VFSNode* tarfs_finddir(VFSNode* node, const char* name);
int tarfs_readdir(VFSNode* node, size_t index, VFSNode* entry_out);

// Find directory entry in flat TarFS list
VFSNode* tarfs_finddir(VFSNode* node, const char* name) {
    (void)node;
    // Remove trailing slash if present in request name
    char clean_name[256];
    str_copy(clean_name, name, sizeof(clean_name));
    int len = 0;
    while (clean_name[len]) len++;
    if (len > 0 && clean_name[len - 1] == '/') {
        clean_name[len - 1] = '\0';
    }

    for (size_t i = 0; i < tar_node_count; ++i) {
        // Compare clean names (stripping trailing slashes from both)
        char node_clean[256];
        str_copy(node_clean, tar_nodes[i]->name, sizeof(node_clean));
        int n_len = 0;
        while (node_clean[n_len]) n_len++;
        if (n_len > 0 && node_clean[n_len - 1] == '/') {
            node_clean[n_len - 1] = '\0';
        }

        if (str_equal(node_clean, clean_name)) {
            return tar_nodes[i];
        }
    }

    // Check if it's an implicit directory (files exist under this prefix)
    bool is_implicit_dir = false;
    char prefix[258];
    str_copy(prefix, clean_name, sizeof(prefix) - 2);
    int p_len = 0;
    while (prefix[p_len]) p_len++;
    prefix[p_len++] = '/';
    prefix[p_len] = '\0';

    for (size_t i = 0; i < tar_node_count; ++i) {
        if (str_starts_with(tar_nodes[i]->name, prefix)) {
            is_implicit_dir = true;
            break;
        }
    }

    if (is_implicit_dir) {
        // Synthesize and register in tar_nodes so we don't leak on subsequent lookups
        VFSNode* synth = new VFSNode();
        str_copy(synth->name, clean_name, sizeof(synth->name));
        int s_len = 0;
        while (synth->name[s_len]) s_len++;
        synth->name[s_len++] = '/';
        synth->name[s_len] = '\0';

        synth->type = NodeType::DIRECTORY;
        synth->size = 0;
        synth->capacity = 0;
        synth->data = nullptr;
        synth->read = nullptr;
        synth->write = nullptr;
        synth->finddir = tarfs_finddir;
        synth->readdir = tarfs_readdir;

        if (tar_node_count < MAX_TAR_NODES) {
            tar_nodes[tar_node_count++] = synth;
        }
        return synth;
    }

    return nullptr;
}

int tarfs_readdir(VFSNode* node, size_t index, VFSNode* entry_out) {
    if (!node || node->type != NodeType::DIRECTORY) return -1;

    char dir_prefix[256];
    if (str_equal(node->name, "tar")) {
        dir_prefix[0] = '\0';
    } else {
        str_copy(dir_prefix, node->name, sizeof(dir_prefix));
        int len = 0;
        while (dir_prefix[len]) len++;
        if (len > 0 && dir_prefix[len - 1] != '/') {
            dir_prefix[len++] = '/';
            dir_prefix[len] = '\0';
        }
    }

    size_t prefix_len = 0;
    while (dir_prefix[prefix_len]) prefix_len++;

    size_t unique_count = 0;

    for (size_t i = 0; i < tar_node_count; ++i) {
        const char* name = tar_nodes[i]->name;
        if (str_equal(name, node->name)) continue;
        if (prefix_len > 0 && !str_starts_with(name, dir_prefix)) continue;

        const char* child_part = name + prefix_len;
        if (child_part[0] == '\0') continue;

        int slash_idx = -1;
        for (int j = 0; child_part[j] != '\0'; j++) {
            if (child_part[j] == '/') {
                slash_idx = j;
                break;
            }
        }

        char child_name[128];
        bool is_dir = false;

        if (slash_idx != -1) {
            is_dir = true;
            for (int j = 0; j < slash_idx; j++) {
                child_name[j] = child_part[j];
            }
            child_name[slash_idx] = '\0';

            bool duplicate = false;
            char full_child_prefix[270];
            str_copy(full_child_prefix, dir_prefix, sizeof(full_child_prefix));
            int fcp_len = prefix_len;
            for (int j = 0; j < slash_idx; j++) {
                full_child_prefix[fcp_len++] = child_part[j];
            }
            full_child_prefix[fcp_len++] = '/';
            full_child_prefix[fcp_len] = '\0';

            for (size_t j = 0; j < i; ++j) {
                if (str_starts_with(tar_nodes[j]->name, full_child_prefix)) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
        } else {
            str_copy(child_name, child_part, sizeof(child_name));
        }

        if (unique_count == index) {
            str_copy(entry_out->name, child_name, sizeof(entry_out->name));
            if (is_dir) {
                entry_out->type = NodeType::DIRECTORY;
                entry_out->size = 0;
                entry_out->capacity = 0;
                entry_out->data = nullptr;
                entry_out->read = nullptr;
                entry_out->write = nullptr;
                entry_out->finddir = tarfs_finddir;
                entry_out->readdir = tarfs_readdir;
            } else {
                entry_out->type = tar_nodes[i]->type;
                entry_out->size = tar_nodes[i]->size;
                entry_out->capacity = tar_nodes[i]->capacity;
                entry_out->data = tar_nodes[i]->data;
                entry_out->read = tar_nodes[i]->read;
                entry_out->write = tar_nodes[i]->write;
                entry_out->finddir = nullptr;
                entry_out->readdir = nullptr;
            }
            return 1;
        }
        unique_count++;
    }

    return 0;
}

} // namespace anonymous

bool tarfs_init(uint64_t start, uint64_t end) {
    if (start == 0 || end == 0 || start >= end) {
        return false;
    }

    // Set up the root mount node for TarFS
    str_copy(tarfs_root_node.name, "tar", sizeof(tarfs_root_node.name));
    tarfs_root_node.type = NodeType::DIRECTORY;
    tarfs_root_node.size = 0;
    tarfs_root_node.capacity = 0;
    tarfs_root_node.data = nullptr;
    tarfs_root_node.read = nullptr;
    tarfs_root_node.write = nullptr;
    tarfs_root_node.finddir = tarfs_finddir;
    tarfs_root_node.readdir = tarfs_readdir;

    uintptr_t curr = start;
    tar_node_count = 0;

    while (curr < end) {
        TarHeader* header = (TarHeader*)curr;
        
        // Double-null block indicates end of archive
        if (header->name[0] == '\0') {
            break;
        }

        size_t size = parse_octal(header->size, 12);
        
        // Normalize name: strip leading "./" or "/" prefixes
        const char* name_ptr = header->name;
        if (name_ptr[0] == '.' && name_ptr[1] == '/') {
            name_ptr += 2;
        }
        while (name_ptr[0] == '/') {
            name_ptr++;
        }

        // Skip directory markings or empty paths
        if (name_ptr[0] == '\0') {
            size_t aligned_size = (size + 511) & ~511ULL;
            curr += 512 + aligned_size;
            continue;
        }

        NodeType type = NodeType::FILE;
        if (header->typeflag == '5') {
            type = NodeType::DIRECTORY;
        }

        drivers::Serial::print("[TarFS] Parsing file: ");
        drivers::Serial::print(name_ptr);
        drivers::Serial::print(" (");
        // Print size in decimal
        char size_buf[32];
        size_t temp = size;
        int idx = 0;
        if (temp == 0) {
            size_buf[idx++] = '0';
        } else {
            char rev[32];
            int r_idx = 0;
            while (temp > 0) {
                rev[r_idx++] = '0' + (temp % 10);
                temp /= 10;
            }
            while (r_idx > 0) {
                size_buf[idx++] = rev[--r_idx];
            }
        }
        size_buf[idx] = '\0';
        drivers::Serial::print(size_buf);
        drivers::Serial::println(" bytes)");

        // Dynamically instantiate VFSNode on Slab Heap!
        VFSNode* node = new VFSNode();
        str_copy(node->name, name_ptr, sizeof(node->name));
        node->type = type;
        node->size = size;
        node->capacity = size;
        node->data = (char*)(curr + 512); // File content is loaded in the next 512-byte aligned block
        node->read = tarfs_read;
        node->write = nullptr; // Read-only filesystem
        node->finddir = nullptr;

        if (tar_node_count < MAX_TAR_NODES) {
            tar_nodes[tar_node_count++] = node;
        }

        // Advance past header + padded file contents
        size_t aligned_size = (size + 511) & ~511ULL;
        curr += 512 + aligned_size;
    }

    drivers::Serial::println("[INIT] TarFS read-only memory archive filesystem mounted at '/tar'.");

    // Register mount point /tar in the VFS
    return VFS::register_mount("/tar", &tarfs_root_node);
}

} // namespace fs
