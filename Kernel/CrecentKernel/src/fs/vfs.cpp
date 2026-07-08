#include "vfs.hpp"

namespace fs {

// Static member definitions
VFSNode VFS::root_node;
FileSystem VFS::root_fs;
VFSNode VFS::child_nodes[VFS::MAX_NODES];
size_t VFS::node_count = 0;

namespace {

// Internal helper: string equality check (since std::strcmp is not available)
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

// Memory-backed VFSNode read implementation
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

// Memory-backed VFSNode write implementation
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

// Root directory finddir implementation to locate children by name
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

    // Register FS representation
    root_fs.name = "RootFS";
    root_fs.root = &root_node;

    node_count = 0;
    return true;
}

const char* VFS::parse_filename(const char* path) {
    if (!path) return nullptr;
    // For a minimal flat FS, we strip the leading slash
    if (path[0] == '/') {
        return path + 1;
    }
    return path;
}

VFSNode* VFS::open(const char* path) {
    if (!path) return nullptr;

    // Direct root access
    if (str_equal(path, "/")) {
        return &root_node;
    }

    // Parse filename relative to the root directory
    const char* filename = parse_filename(path);
    if (!filename || filename[0] == '\0') {
        return nullptr;
    }

    return root_node.finddir(&root_node, filename);
}

VFSNode* VFS::create_file(const char* path, char* buffer_ptr, size_t capacity) {
    const char* filename = parse_filename(path);
    if (!filename || filename[0] == '\0' || node_count >= MAX_NODES) {
        return nullptr;
    }

    // Return existing if file already exists
    VFSNode* existing = open(path);
    if (existing) return existing;

    // Allocate from static pool
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
    if (!file || !file->node || !file->node->read) return -1;
    ssize_t bytes_read = file->node->read(file->node, file->offset, buffer, count);
    if (bytes_read > 0) {
        file->offset += (size_t)bytes_read;
    }
    return bytes_read;
}

ssize_t VFS::write(File* file, const void* buffer, size_t count) {
    if (!file || !file->node || !file->node->write) return -1;
    ssize_t bytes_written = file->node->write(file->node, file->offset, buffer, count);
    if (bytes_written > 0) {
        file->offset += (size_t)bytes_written;
    }
    return bytes_written;
}

} // namespace fs
