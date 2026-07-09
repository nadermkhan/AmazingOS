#include "elf.hpp"
#include "vmm.hpp"
#include "pmm.hpp"
#include "../fs/vfs.hpp"
#include "../drivers/serial.hpp"

namespace kernel {

// Helper: copy memory
static void local_memcpy(void* dest, const void* src, size_t count) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (size_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
}

// Helper: zero memory
static void local_memset(void* dest, int val, size_t count) {
    char* d = (char*)dest;
    for (size_t i = 0; i < count; ++i) {
        d[i] = (char)val;
    }
}

bool elf_load(const char* path, uint64_t& entry_point, uint64_t& stack_top, uint64_t& pml4_phys) {
    fs::VFSNode* node = fs::VFS::open(path);
    if (!node) {
        drivers::Serial::print("[ELF] Error: File not found: ");
        drivers::Serial::println(path);
        return false;
    }

    fs::File file = { node, 0, 0, 1 };

    // 1. Read Elf Header
    Elf64_Ehdr ehdr;
    if (fs::VFS::read(&file, &ehdr, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) {
        drivers::Serial::println("[ELF] Error: Failed to read ELF header!");
        return false;
    }

    // 2. Validate Header
    if (ehdr.e_ident[0] != ELF_MAG0 || ehdr.e_ident[1] != ELF_MAG1 ||
        ehdr.e_ident[2] != ELF_MAG2 || ehdr.e_ident[3] != ELF_MAG3) {
        drivers::Serial::println("[ELF] Error: Invalid ELF magic!");
        return false;
    }

    // 3. Create process address space
    uint64_t child_pml4 = vmm_create_user_address_space();
    if (!child_pml4) {
        drivers::Serial::println("[ELF] Error: Failed to allocate process PML4!");
        return false;
    }

    // 4. Load program segments
    for (int i = 0; i < ehdr.e_phnum; i++) {
        // Read program header
        Elf64_Phdr phdr;
        file.offset = ehdr.e_phoff + i * ehdr.e_phentsize;
        if (fs::VFS::read(&file, &phdr, sizeof(Elf64_Phdr)) != sizeof(Elf64_Phdr)) {
            drivers::Serial::println("[ELF] Error: Failed to read program header!");
            vmm_destroy_user_address_space(child_pml4);
            return false;
        }

        if (phdr.p_type != PT_LOAD) {
            continue;
        }

        // Map and load the segment page-by-page
        uint64_t start_virt = phdr.p_vaddr;
        uint64_t mem_size = phdr.p_memsz;
        uint64_t file_size = phdr.p_filesz;
        uint64_t file_offset = phdr.p_offset;

        // Calculate page boundaries
        uint64_t page_start = start_virt & ~0xFFFULL;
        uint64_t page_end = (start_virt + mem_size + 4095) & ~0xFFFULL;

        for (uint64_t page_virt = page_start; page_virt < page_end; page_virt += 4096) {
            uint64_t phys_frame = pmm_alloc_frame();
            if (!phys_frame) {
                drivers::Serial::println("[ELF] Error: Out of memory during segment allocation!");
                vmm_destroy_user_address_space(child_pml4);
                return false;
            }

            // Zero out physical memory first (covers BSS zeroing)
            local_memset((void*)phys_frame, 0, 4096);

            // Determine page flags
            uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
            if (phdr.p_flags & PF_W) {
                flags |= VMM_FLAG_WRITABLE;
            }
            if (!(phdr.p_flags & PF_X)) {
                flags |= VMM_FLAG_NO_EXECUTE; // W^X execution protection
            }

            // Map page
            if (!vmm_map_page_in_pml4(child_pml4, page_virt, phys_frame, flags)) {
                drivers::Serial::println("[ELF] Error: Failed to map segment page!");
                pmm_free_frame(phys_frame);
                vmm_destroy_user_address_space(child_pml4);
                return false;
            }

            // Copy file content if this page overlaps with the file segment
            int64_t copy_offset_in_segment = (int64_t)page_virt - (int64_t)start_virt;
            int64_t copy_start_in_page = 0;
            if (copy_offset_in_segment < 0) {
                copy_start_in_page = -copy_offset_in_segment;
                copy_offset_in_segment = 0;
            }

            if ((uint64_t)copy_offset_in_segment < file_size) {
                uint64_t bytes_to_copy = file_size - copy_offset_in_segment;
                if (bytes_to_copy > (uint64_t)(4096 - copy_start_in_page)) {
                    bytes_to_copy = 4096 - copy_start_in_page;
                }

                // Read data directly from TarFS node into the identity-mapped physical page
                file.offset = file_offset + copy_offset_in_segment;
                void* dest_ptr = (void*)(phys_frame + copy_start_in_page);
                fs::VFS::read(&file, dest_ptr, bytes_to_copy);
            }
        }
    }

    // 5. Map 100 user stack pages (400KB) at high virtual memory (0x7FFFFFF00000)
    uint64_t user_stack_base = 0x7FFFFFFF0000ULL - (100 * 4096);
    for (int i = 0; i < 100; i++) {
        uint64_t stack_phys = pmm_alloc_frame();
        if (!stack_phys) {
            drivers::Serial::println("[ELF] Error: Failed to allocate stack frame!");
            vmm_destroy_user_address_space(child_pml4);
            return false;
        }
        local_memset((void*)stack_phys, 0, 4096);
        
        // Stack pages are User + Writeable + No-Execute (DEP protection)
        uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER | VMM_FLAG_NO_EXECUTE;
        if (!vmm_map_page_in_pml4(child_pml4, user_stack_base + i * 4096, stack_phys, flags)) {
            drivers::Serial::println("[ELF] Error: Failed to map stack page!");
            pmm_free_frame(stack_phys);
            vmm_destroy_user_address_space(child_pml4);
            return false;
        }
    }

    entry_point = ehdr.e_entry;
    stack_top = 0x7FFFFFFF0000ULL;
    pml4_phys = child_pml4;

    return true;
}

} // namespace kernel
