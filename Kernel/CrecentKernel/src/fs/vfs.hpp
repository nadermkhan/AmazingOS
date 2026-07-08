#pragma once

#include "../kernel/types.hpp"

namespace fs {

enum class NodeType : uint32_t {
    FILE = 1,
    DIRECTORY = 2,
};

struct VFSNode;

// VFS Node representing an inode (directory, file, etc.)
struct VFSNode {
    char name[64];
    NodeType type;
    size_t size;
    size_t capacity;
    char* data; // Pointer to mock memory block

    // Function pointers for standard filesystem operations
    ssize_t (*read)(VFSNode* node, size_t offset, void* buffer, size_t count);
    ssize_t (*write)(VFSNode* node, size_t offset, const void* buffer, size_t count);
    VFSNode* (*finddir)(VFSNode* node, const char* name);
};

// File descriptor structure (holds session info like offset)
struct File {
    VFSNode* node;
    size_t offset;
    uint32_t flags;
};

// Abstract representation of a FileSystem
struct FileSystem {
    const char* name;
    VFSNode* root;
};

class VFS {
public:
    // Initialize VFS and register root mount point
    static bool init();

    // Open a file by path (returns VFSNode)
    static VFSNode* open(const char* path);

    // Create a new mock file in the root directory
    static VFSNode* create_file(const char* path, char* buffer_ptr, size_t capacity);

    // Read count bytes from File into buffer
    static ssize_t read(File* file, void* buffer, size_t count);

    // Write count bytes from buffer into File
    static ssize_t write(File* file, const void* buffer, size_t count);

    // Static structures representing filesystem state
    static VFSNode root_node;
    static FileSystem root_fs;
    static constexpr size_t MAX_NODES = 16;
    static VFSNode child_nodes[MAX_NODES];
    static size_t node_count;

private:
    // Helper to extract filename from path (e.g. "/test.txt" -> "test.txt")
    static const char* parse_filename(const char* path);
};

} // namespace fs
