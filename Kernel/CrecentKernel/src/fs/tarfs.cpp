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

// Find directory entry in flat TarFS list
VFSNode* tarfs_finddir(VFSNode* node, const char* name) {
    (void)node;
    for (size_t i = 0; i < tar_node_count; ++i) {
        if (str_equal(tar_nodes[i]->name, name)) {
            return tar_nodes[i];
        }
    }
    return nullptr;
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
