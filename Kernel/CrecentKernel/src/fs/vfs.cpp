#include "vfs.hpp"
#include "exfat.hpp"
#include "../kernel/heap.hpp"
#include "../drivers/serial.hpp"

namespace fs {

// Static member definitions
VFSNode VFS::root_node;
VFSNode VFS::child_nodes[VFS::MAX_NODES];
size_t VFS::node_count = 0;

MountPoint VFS::mounts[VFS::MAX_MOUNTS];
size_t VFS::mount_count = 0;

namespace {

// Internal helper: string length
size_t str_len(const char* s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

// Internal helper: string prefix match
bool str_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    size_t i = 0;
    while (prefix[i]) {
        if (str[i] != prefix[i]) return false;
        i++;
    }
    return true;
}

// Internal helper: string equality check
bool str_equal(const char* s1, const char* s2) {
    if (!s1 || !s2) return false;
    while (*s1 && *s2) {
        if (*s1 != *s2) return false;
        s1++;
        s2++;
    }
    return *s1 == *s2;
}

// Internal helper: safe string copy
void str_copy(char* dest, const char* src, size_t max_len) {
    if (!dest || !src) return;
    size_t i = 0;
    for (; i < max_len - 1 && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

// Memory-backed VFSNode read implementation (legacy ramFS mock)
ssize_t ram_read(VFSNode* node, size_t offset, void* buffer, size_t count) {
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

// Memory-backed VFSNode write implementation (legacy ramFS mock)
ssize_t ram_write(VFSNode* node, size_t offset, const void* buffer, size_t count) {
    if (!node || !buffer || !node->data) return -1;
    if (offset >= node->capacity) return 0;
    if (offset + count > node->capacity) {
        count = node->capacity - offset;
    }

    const char* src = (const char*)buffer;
    char* dest = node->data + offset;
    for (size_t i = 0; i < count; ++i) {
        dest[i] = src[i];
    }

    // Expand the logical size of the node if written beyond the old size
    if (offset + count > node->size) {
        node->size = offset + count;
    }
    return (ssize_t)count;
}

// Root directory finddir implementation to locate children by name (legacy ramFS mock)
VFSNode* finddir_root(VFSNode* node, const char* name) {
    if (!node || node->type != NodeType::DIRECTORY) return nullptr;
    for (size_t i = 0; i < VFS::node_count; ++i) {
        if (str_equal(VFS::child_nodes[i].name, name)) {
            return &VFS::child_nodes[i];
        }
    }
    return nullptr;
}

} // namespace anonymous

bool VFS::init() {
    // Setup root directory node
    str_copy(root_node.name, "/", sizeof(root_node.name));
    root_node.type = NodeType::DIRECTORY;
    root_node.size = 0;
    root_node.capacity = 0;
    root_node.data = nullptr;
    root_node.read = nullptr;
    root_node.write = nullptr;
    root_node.finddir = finddir_root;

    node_count = 0;
    mount_count = 0;

    // Register legacy root mount "/"
    register_mount("/", &root_node);

    return true;
}

bool VFS::register_mount(const char* path, VFSNode* root) {
    if (!path || !root || mount_count >= MAX_MOUNTS) {
        return false;
    }
    MountPoint& m = mounts[mount_count++];
    str_copy(m.path, path, sizeof(m.path));
    m.path_len = str_len(m.path);
    m.root_node = root;
    return true;
}

const char* VFS::parse_filename(const char* path) {
    if (!path) return nullptr;
    if (path[0] == '/') {
        return path + 1;
    }
    return path;
}

VFSNode* VFS::open(const char* path) {
    if (!path || path[0] == '\0') return nullptr;

    // Check RAM Overlay first
    const char* rel_path = path;
    if (path[0] == '/') rel_path = path + 1;
    for (size_t i = 0; i < node_count; ++i) {
        if (str_equal(child_nodes[i].name, rel_path)) {
            return &child_nodes[i];
        }
    }

    // 1. Longest-prefix mount point match
    int matched_idx = -1;
    size_t longest_len = 0;

    for (size_t i = 0; i < mount_count; ++i) {
        if (str_starts_with(path, mounts[i].path)) {
            size_t len = mounts[i].path_len;
            
            // Check boundary: prefix must match path boundary (e.g. "/tar" shouldn't match "/target")
            // A match is valid if the prefix is "/" or prefix matches exactly or next character in path is '/'
            if (len == 1 || path[len] == '\0' || path[len] == '/') {
                if (len > longest_len) {
                    longest_len = len;
                    matched_idx = (int)i;
                }
            }
        }
    }

    if (matched_idx == -1) {
        return nullptr;
    }

    MountPoint& m = mounts[matched_idx];
    
    // Extract subpath relative to mount root
    const char* subpath = path + m.path_len;
    while (subpath[0] == '/') {
        subpath++; // Skip leading slashes
    }

    // If subpath is empty, we refer to the mounted root node itself
    if (subpath[0] == '\0') {
        return m.root_node;
    }

    // Dispatch lookup recursively to mounted root node's finddir
    if (m.root_node->finddir) {
        return m.root_node->finddir(m.root_node, subpath);
    }

    return nullptr;
}

VFSNode* VFS::create_file(const char* path, char* buffer_ptr, size_t capacity) {
    const char* filename = parse_filename(path);
    if (!filename || filename[0] == '\0' || node_count >= MAX_NODES) {
        return nullptr;
    }

    // Return existing if file already exists
    VFSNode* existing = open(path);
    if (existing) return existing;

    // Allocate from static pool (legacy ramFS mock)
    VFSNode* new_node = &child_nodes[node_count++];
    str_copy(new_node->name, filename, sizeof(new_node->name));
    new_node->type = NodeType::FILE;
    new_node->size = 0;
    new_node->capacity = capacity;
    new_node->data = buffer_ptr;
    new_node->read = ram_read;
    new_node->write = ram_write;
    new_node->finddir = nullptr;

    return new_node;
}

ssize_t VFS::read(File* file, void* buffer, size_t count) {
    if (!file || !file->node || !buffer) return -1;
    if (!file->node->read) return -1;

    ssize_t bytes = file->node->read(file->node, file->offset, buffer, count);
    if (bytes > 0) {
        file->offset += bytes;
    }
    return bytes;
}

ssize_t VFS::write(File* file, const void* buffer, size_t count) {
    if (!file || !file->node || !buffer) return -1;
    if (!file->node->write) return -1;

    // Check if we need to promote an exFAT file to RAM CoW overlay
    if (file->node->read == Exfat::read) {
        if (file->offset + count > file->node->size) {
            drivers::Serial::println("[VFS] Resizing exFAT file. Promoting to RAM Copy-on-Write overlay.");
            
            // 1. Allocate buffer for new capacity
            size_t new_capacity = file->offset + count;
            if (new_capacity < 4096) new_capacity = 4096;
            char* ram_buf = (char*)kernel::kmalloc(new_capacity);
            if (!ram_buf) {
                drivers::Serial::println("[VFS] Error: Failed to allocate CoW buffer.");
                return -1;
            }

            // 2. Read existing content from exFAT
            size_t old_size = file->node->size;
            if (old_size > 0) {
                ssize_t read_bytes = file->node->read(file->node, 0, ram_buf, old_size);
                if (read_bytes != (ssize_t)old_size) {
                    drivers::Serial::println("[VFS] Error: Failed to read old exFAT file contents.");
                    kernel::kfree(ram_buf);
                    return -1;
                }
            }

            // 3. Construct absolute path for the RAM overlay node: "/disk/" + node->name
            char vfs_path[256] = "/disk/";
            size_t o_idx = 6;
            size_t n_idx = 0;
            while (file->node->name[n_idx] && o_idx < 250) {
                vfs_path[o_idx++] = file->node->name[n_idx++];
            }
            vfs_path[o_idx] = '\0';

            // 4. Create RAM node in VFS (registers under the overlay path)
            VFSNode* ram_node = create_file(vfs_path, ram_buf, new_capacity);
            if (!ram_node) {
                drivers::Serial::println("[VFS] Error: Failed to register CoW node.");
                kernel::kfree(ram_buf);
                return -1;
            }
            ram_node->size = old_size;

            // 5. Update File handle to point to the new RAM node
            file->node = ram_node;
        }
    }

    ssize_t bytes = file->node->write(file->node, file->offset, buffer, count);
    if (bytes > 0) {
        file->offset += bytes;
    }
    return bytes;
}

} // namespace fs
