#include "../drivers/serial.hpp"
#include "../drivers/vga.hpp"
#include "../fs/vfs.hpp"
#include "gdt.hpp"
#include "idt.hpp"
#include "../drivers/apic.hpp"
#include "pmm.hpp"
#include "vmm.hpp"

// Define an empty dummy __main function to satisfy MinGW's global constructor initialization stub
extern "C" void __main() {}

// Static memory buffer representing the storage of the mock file
static char test_file_buffer[256];

extern "C" __attribute__((sysv_abi)) void kmain(uint32_t magic, uint64_t maddr) {
    // 1. Initialize serial port COM1 first for logging
    bool serial_ok = drivers::Serial::init();
    
    if (serial_ok) {
        drivers::Serial::println("========================================");
        drivers::Serial::println("CrecentKernel COM1 Serial Log Initialized");
        drivers::Serial::println("========================================");
    }

    // 2. Initialize CPU Stacks and Descriptors (GDT & TSS)
    kernel::gdt_init();
    if (serial_ok) {
        drivers::Serial::println("[INIT] GDT and Task State Segment (TSS) loaded.");
    }

    // 3. Initialize Interrupt Descriptor Table (IDT)
    kernel::idt_init();
    if (serial_ok) {
        drivers::Serial::println("[INIT] IDT and assembly exception handlers loaded.");
    }

    // 4. Initialize Local APIC and disable legacy PIC
    bool apic_ok = drivers::Apic::init();
    if (serial_ok) {
        if (apic_ok) {
            drivers::Serial::println("[INIT] Local APIC enabled. Legacy PIC disabled.");
        } else {
            drivers::Serial::println("[WARNING] Local APIC not supported. Skipping.");
        }
    }

    // 5. Initialize Physical Memory Manager (PMM)
    bool pmm_ok = kernel::pmm_init(magic, maddr);
    if (serial_ok) {
        if (pmm_ok) {
            drivers::Serial::println("[INIT] Physical Memory Manager (PMM) initialized.");
        } else {
            drivers::Serial::println("[ERROR] Physical Memory Manager (PMM) failed!");
        }
    }

    // 6. Initialize Virtual Memory Manager (VMM)
    kernel::vmm_init();
    if (serial_ok) {
        drivers::Serial::println("[INIT] Virtual Memory Manager (VMM) initialized.");
    }

    // 7. Initialize VGA text-mode graphics console
    drivers::Vga::init();
    drivers::Vga::set_color(drivers::VgaColor::LIGHT_CYAN, drivers::VgaColor::BLACK);
    drivers::Vga::println("Welcome to CrecentKernel!");
    drivers::Vga::set_color(drivers::VgaColor::WHITE, drivers::VgaColor::BLACK);
    drivers::Vga::println("x86_64 bare-metal environment initialized successfully.");
    drivers::Vga::println("");

    if (serial_ok) {
        drivers::Serial::println("[INIT] VGA text console initialized.");
    }

    // 8. Initialize Virtual File System
    if (!fs::VFS::init()) {
        drivers::Vga::set_color(drivers::VgaColor::RED, drivers::VgaColor::BLACK);
        drivers::Vga::println("[ERROR] Failed to initialize Virtual File System!");
        if (serial_ok) {
            drivers::Serial::println("[ERROR] VFS initialization failed.");
        }
        goto halt;
    }
    
    drivers::Vga::println("[VFS] Virtual File System initialized.");
    if (serial_ok) {
        drivers::Serial::println("[INIT] VFS root directory '/' registered.");
    }

    // 9. Create and mount a mock file, write test data, and verify by reading back
    {
        const char* filepath = "/test.txt";
        drivers::Vga::print("[VFS] Creating mock file: ");
        drivers::Vga::println(filepath);
        if (serial_ok) {
            drivers::Serial::print("[VFS] Creating mock file: ");
            drivers::Serial::println(filepath);
        }

        fs::VFSNode* node = fs::VFS::create_file(filepath, test_file_buffer, sizeof(test_file_buffer));
        if (!node) {
            drivers::Vga::set_color(drivers::VgaColor::RED, drivers::VgaColor::BLACK);
            drivers::Vga::println("[ERROR] Failed to create mock file!");
            if (serial_ok) {
                drivers::Serial::println("[ERROR] Failed to create mock file.");
            }
            goto halt;
        }

        // Open the file descriptor
        fs::File file;
        file.node = node;
        file.offset = 0;
        file.flags = 0;

        // Count string length manually (no std::strlen)
        const char* test_data = "Hello from CrecentKernel VFS mock RAM drive!";
        size_t test_data_len = 0;
        while (test_data[test_data_len] != '\0') {
            test_data_len++;
        }

        // Write the data to file
        ssize_t written = fs::VFS::write(&file, test_data, test_data_len);
        if (written >= 0) {
            drivers::Vga::println("[VFS] Write verification: OK");
            if (serial_ok) {
                drivers::Serial::println("[VFS] Successfully wrote test string to file.");
            }
        } else {
            drivers::Vga::println("[VFS] Write verification: FAILED");
            if (serial_ok) {
                drivers::Serial::println("[VFS] Write operation failed.");
            }
            goto halt;
        }

        // Reset file descriptor offset to read back from the beginning
        file.offset = 0;
        char read_buffer[256];
        for (size_t i = 0; i < sizeof(read_buffer); ++i) {
            read_buffer[i] = '\0';
        }

        // Read the data back
        ssize_t bytes_read = fs::VFS::read(&file, read_buffer, sizeof(read_buffer) - 1);
        if (bytes_read >= 0) {
            drivers::Vga::print("[VFS] Read verification data: \"");
            drivers::Vga::print(read_buffer);
            drivers::Vga::println("\"");
            
            if (serial_ok) {
                drivers::Serial::print("[VFS] Verification read back: \"");
                drivers::Serial::print(read_buffer);
                drivers::Serial::println("\"");
            }
        } else {
            drivers::Vga::println("[ERROR] Failed to read from mock file!");
            if (serial_ok) {
                drivers::Serial::println("[ERROR] Read operation failed.");
            }
            goto halt;
        }
    }

    // 10. Interrupt Verification Testing
    drivers::Vga::println("");
    drivers::Vga::println("[TEST] Triggering Software Interrupt 0x80...");
    if (serial_ok) {
        drivers::Serial::println("[TEST] Triggering Software Interrupt 0x80...");
    }

    // Trigger vector 0x80 software interrupt
    __asm__ __volatile__ ("int $0x80");

    drivers::Vga::println("[TEST] Software Interrupt 0x80 returned successfully.");
    if (serial_ok) {
        drivers::Serial::println("[TEST] Software Interrupt 0x80 returned successfully.");
    }

    // 11. Memory Management Verification Testing
    drivers::Vga::println("");
    drivers::Vga::println("[TEST] Starting Memory Management verification...");
    if (serial_ok) {
        drivers::Serial::println("[TEST] Starting Memory Management verification...");
    }

    if (pmm_ok) {
        uint64_t phys_frame = kernel::pmm_alloc_frame();
        if (phys_frame == 0) {
            drivers::Vga::set_color(drivers::VgaColor::RED, drivers::VgaColor::BLACK);
            drivers::Vga::println("[TEST] PMM allocation: FAILED (No physical frames!)");
            goto exception_test;
        }
        drivers::Vga::println("[TEST] Allocated physical page frame successfully.");
        if (serial_ok) {
            drivers::Serial::println("[TEST] Allocated physical page frame successfully.");
        }

        // Map virtual page 0x100000000 to the allocated physical frame
        // Flags: Present (1) + Writable (2)
        uint64_t test_virt = 0x100000000ULL;
        bool map_ok = kernel::vmm_map_page(test_virt, phys_frame, kernel::VMM_FLAG_PRESENT | kernel::VMM_FLAG_WRITABLE);
        if (!map_ok) {
            drivers::Vga::set_color(drivers::VgaColor::RED, drivers::VgaColor::BLACK);
            drivers::Vga::println("[TEST] VMM map page: FAILED");
            goto exception_test;
        }
        drivers::Vga::println("[TEST] Mapped virtual page 0x100000000 successfully.");
        if (serial_ok) {
            drivers::Serial::println("[TEST] Mapped virtual page 0x100000000 successfully.");
        }

        // Verify VMM mapping status
        if (!kernel::vmm_is_mapped(test_virt)) {
            drivers::Vga::println("[TEST] vmm_is_mapped: FAILED");
            goto exception_test;
        }

        // Write signature string into the mapped virtual page
        char* test_ptr = (char*)test_virt;
        const char* sig = "CrecentMemoryVerify";
        size_t sig_len = 0;
        while (sig[sig_len] != '\0') {
            test_ptr[sig_len] = sig[sig_len];
            sig_len++;
        }
        test_ptr[sig_len] = '\0';

        drivers::Vga::println("[TEST] Write verification string: OK");

        // Read back and verify
        bool sig_match = true;
        for (size_t i = 0; i < sig_len; ++i) {
            if (test_ptr[i] != sig[i]) {
                sig_match = false;
                break;
            }
        }

        if (sig_match) {
            drivers::Vga::print("[TEST] Read verification data: \"");
            drivers::Vga::print(test_ptr);
            drivers::Vga::println("\" - OK");
            if (serial_ok) {
                drivers::Serial::print("[TEST] Verification read back: \"");
                drivers::Serial::print(test_ptr);
                drivers::Serial::println("\" - SUCCESS");
            }
        } else {
            drivers::Vga::set_color(drivers::VgaColor::RED, drivers::VgaColor::BLACK);
            drivers::Vga::println("[TEST] Read verification data: FAILED (data mismatch!)");
        }

        // Unmap the page
        kernel::vmm_unmap_page(test_virt);
        drivers::Vga::println("[TEST] Unmapped virtual page 0x100000000 successfully.");
        if (serial_ok) {
            drivers::Serial::println("[TEST] Unmapped virtual page 0x100000000 successfully.");
        }

        // Free the physical frame
        kernel::pmm_free_frame(phys_frame);
        drivers::Vga::println("[TEST] Freed physical page frame.");
        if (serial_ok) {
            drivers::Serial::println("[TEST] Freed physical page frame.");
        }

        // Trigger page fault by reading from the unmapped address to demonstrate exception routing
        drivers::Vga::println("[TEST] Reading unmapped address to trigger Page Fault (#PF)...");
        if (serial_ok) {
            drivers::Serial::println("[TEST] Reading unmapped address to trigger Page Fault (#PF)...");
        }

        // Force read (will trigger exception 14)
        volatile char fault = *test_ptr;
        (void)fault; // Prevent compiler unused warning
    }

exception_test:
    // Fallback CPU exception trigger
    drivers::Vga::println("");
    drivers::Vga::println("[TEST] Triggering CPU exception (Divide by Zero)...");
    if (serial_ok) {
        drivers::Serial::println("[TEST] Triggering CPU exception (Divide by Zero)...");
    }

    // Trigger vector 0 CPU exception (divide-by-zero) using inline assembly
    __asm__ __volatile__ (
        "mov $0, %%rbx\n\t"
        "div %%rbx"
        :
        :
        : "rax", "rdx", "rbx"
    );

    // This block should never be reached
    drivers::Vga::println("");
    drivers::Vga::set_color(drivers::VgaColor::LIGHT_GREEN, drivers::VgaColor::BLACK);
    drivers::Vga::println("Kernel tasks completed successfully. Halting CPU...");
    
    if (serial_ok) {
        drivers::Serial::println("[SUCCESS] CrecentKernel tasks completed successfully. CPU halting.");
    }

halt:
    while (true) {
        __asm__ __volatile__ (
            "cli\n\t"
            "hlt"
        );
    }
}
