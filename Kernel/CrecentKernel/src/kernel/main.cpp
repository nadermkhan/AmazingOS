#include "../drivers/serial.hpp"
#include "../drivers/vga.hpp"
#include "../fs/vfs.hpp"
#include "gdt.hpp"
#include "idt.hpp"
#include "../drivers/apic.hpp"

// Define an empty dummy __main function to satisfy MinGW's global constructor initialization stub
extern "C" void __main() {}

// Static memory buffer representing the storage of the mock file
static char test_file_buffer[256];

extern "C" void kmain() {
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

    // 5. Initialize VGA text-mode graphics console
    drivers::Vga::init();
    drivers::Vga::set_color(drivers::VgaColor::LIGHT_CYAN, drivers::VgaColor::BLACK);
    drivers::Vga::println("Welcome to CrecentKernel!");
    drivers::Vga::set_color(drivers::VgaColor::WHITE, drivers::VgaColor::BLACK);
    drivers::Vga::println("x86_64 bare-metal environment initialized successfully.");
    drivers::Vga::println("");

    if (serial_ok) {
        drivers::Serial::println("[INIT] VGA text console initialized.");
    }

    // 6. Initialize Virtual File System
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

    // 7. Create and mount a mock file, write test data, and verify by reading back
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

    // 8. Interrupt & Exception Verification Testing
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
