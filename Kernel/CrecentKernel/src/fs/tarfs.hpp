#pragma once

#include "vfs.hpp"

namespace fs {

// Parse a USTAR tar archive in physical RAM and mount it to the VFS
// start: Start physical address of the tar module
// end: End physical address of the tar module
// Returns true on success
bool tarfs_init(uint64_t start, uint64_t end);

} // namespace fs
