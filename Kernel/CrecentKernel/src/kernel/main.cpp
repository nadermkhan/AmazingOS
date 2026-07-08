#include "../drivers/serial.hpp"
#include "../drivers/vga.hpp"
#include "../fs/vfs.hpp"
#include "../fs/tarfs.hpp"
#include "gdt.hpp"
#include "idt.hpp"
#include "../drivers/apic.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "heap.hpp"
#include "scheduler.hpp"
#include "syscall.hpp"

// Polymorphic class to verify C++ new/delete, constructors, and virtual tables
class TestClass {
public:
    int x;
    int y;
    TestClass() : x(42), y(100) {}
    virtual int get_sum() {
        return x + y;
    }
    virtual ~TestClass() {}
};

// Test thread A function
void thread_a_func(void* arg) {
    (void)arg;
    for (int i = 0; i < 30; ++i) {
        drivers::Vga::print("A");
        drivers::Serial::print("A");
        // Waste time to trigger preemption
        for (volatile int delay = 0; delay < 1000000; ) {
            delay = delay + 1;
        }
    }
    drivers::Serial::println("\n[THREAD A] Finished execution.");
}

// Test thread B function
void thread_b_func(void* arg) {
    (void)arg;
    for (int i = 0; i < 30; ++i) {
        drivers::Vga::print("B");
        drivers::Serial::print("B");
        // Waste time to trigger preemption
        for (volatile int delay = 0; delay < 1000000; ) {
            delay = delay + 1;
        }
    }
    drivers::Serial::println("\n[THREAD B] Finished execution.");
}

// Test thread C function (verifies exit cleanup)
void thread_c_func(void* arg) {
    (void)arg;
    drivers::Serial::println("\n[THREAD C] Started and now exiting immediately.");
}

// User-space system call helper using assembly syscall instructions
static inline uint64_t user_syscall3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ __volatile__ (
        "movq %1, %%rax\n\t"
        "movq %2, %%rdi\n\t"
        "movq %3, %%rsi\n\t"
        "movq %4, %%rdx\n\t"
        "syscall\n\t"
        "movq %%rax, %0"
        : "=r"(ret)
        : "r"(num), "r"(a1), "r"(a2), "r"(a3)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "r8", "r9", "r10", "memory"
    );
    return ret;
}

static inline void user_write(const char* msg, size_t len) {
    user_syscall3(1, 1, (uint64_t)msg, len);
}

static inline void user_yield() {
    user_syscall3(2, 0, 0, 0);
}

static inline void user_exit() {
    user_syscall3(3, 0, 0, 0);
}

// User-space thread entry point
void user_thread_entry() {
    char msg1[33];
    msg1[0] = 'H'; msg1[1] = 'e'; msg1[2] = 'l'; msg1[3] = 'l'; msg1[4] = 'o';
    msg1[5] = ' '; msg1[6] = 'f'; msg1[7] = 'r'; msg1[8] = 'o'; msg1[9] = 'm';
    msg1[10] = ' '; msg1[11] = 'U'; msg1[12] = 's'; msg1[13] = 'e'; msg1[14] = 'r';
    msg1[15] = ' '; msg1[16] = 'S'; msg1[17] = 'p'; msg1[18] = 'a'; msg1[19] = 'c';
    msg1[20] = 'e'; msg1[21] = ' '; msg1[22] = '('; msg1[23] = 'R'; msg1[24] = 'i';
    msg1[25] = 'n'; msg1[26] = 'g'; msg1[27] = ' '; msg1[28] = '3'; msg1[29] = ')';
    msg1[30] = '!'; msg1[31] = '\n'; msg1[32] = '\0';
    user_write(msg1, 32);

    user_yield();

    char msg2[28];
    msg2[0] = 'U'; msg2[1] = 's'; msg2[2] = 'e'; msg2[3] = 'r'; msg2[4] = ' ';
    msg2[5] = 's'; msg2[6] = 'p'; msg2[7] = 'a'; msg2[8] = 'c'; msg2[9] = 'e';
    msg2[10] = ' '; msg2[11] = 't'; msg2[12] = 'h'; msg2[13] = 'r'; msg2[14] = 'e';
    msg2[15] = 'a'; msg2[16] = 'd'; msg2[17] = ' '; msg2[18] = 'r'; msg2[19] = 'e';
    msg2[20] = 's'; msg2[21] = 'u'; msg2[22] = 'm'; msg2[23] = 'e'; msg2[24] = 'd';
    msg2[25] = '!'; msg2[26] = '\n'; msg2[27] = '\0';
    user_write(msg2, 27);

    user_exit();
}

// User-space security verification thread
void user_security_entry() {
    // 1. Attempt to call sys_write with kernel address 0x100000 (kernel code)
    uint64_t ret = user_syscall3(1, 1, 0x100000ULL, 10);

    char ok_msg[45];
    if (ret == (uint64_t)-1) {
        ok_msg[0] = '['; ok_msg[1] = 'S'; ok_msg[2] = 'E'; ok_msg[3] = 'C'; ok_msg[4] = 'U';
        ok_msg[5] = 'R'; ok_msg[6] = 'I'; ok_msg[7] = 'T'; ok_msg[8] = 'Y'; ok_msg[9] = ']';
        ok_msg[10] = ' '; ok_msg[11] = 'K'; ok_msg[12] = 'e'; ok_msg[13] = 'r'; ok_msg[14] = 'n';
        ok_msg[15] = 'e'; ok_msg[16] = 'l'; ok_msg[17] = ' '; ok_msg[18] = 'p'; ok_msg[19] = 'o';
        ok_msg[20] = 'i'; ok_msg[21] = 'n'; ok_msg[22] = 't'; ok_msg[23] = 'e'; ok_msg[24] = 'r';
        ok_msg[25] = ' '; ok_msg[26] = 'b'; ok_msg[27] = 'l'; ok_msg[28] = 'o'; ok_msg[29] = 'c';
        ok_msg[30] = 'k'; ok_msg[31] = 'e'; ok_msg[32] = 'd'; ok_msg[33] = ' '; ok_msg[34] = '-';
        ok_msg[35] = ' '; ok_msg[36] = 'S'; ok_msg[37] = 'U'; ok_msg[38] = 'C'; ok_msg[39] = 'C';
        ok_msg[40] = 'E'; ok_msg[41] = 'S'; ok_msg[42] = 'S'; ok_msg[43] = '\n'; ok_msg[44] = '\0';
        user_write(ok_msg, 44);
    } else {
        ok_msg[0] = '['; ok_msg[1] = 'S'; ok_msg[2] = 'E'; ok_msg[3] = 'C'; ok_msg[4] = 'U';
        ok_msg[5] = 'R'; ok_msg[6] = 'I'; ok_msg[7] = 'T'; ok_msg[8] = 'Y'; ok_msg[9] = ']';
        ok_msg[10] = ' '; ok_msg[11] = 'L'; ok_msg[12] = 'e'; ok_msg[13] = 'a'; ok_msg[14] = 'k';
        ok_msg[15] = 'e'; ok_msg[16] = 'd'; ok_msg[17] = ' '; ok_msg[18] = 'K'; ok_msg[19] = 'e';
        ok_msg[20] = 'r'; ok_msg[21] = 'n'; ok_msg[22] = 'e'; ok_msg[23] = 'l'; ok_msg[24] = ' ';
        ok_msg[25] = 'M'; ok_msg[26] = 'e'; ok_msg[27] = 'm'; ok_msg[28] = 'o'; ok_msg[29] = 'r';
        ok_msg[30] = 'y'; ok_msg[31] = '!'; ok_msg[32] = '\n'; ok_msg[33] = '\0';
        user_write(ok_msg, 33);
    }
    user_exit();
}

struct UserBootstrapArgs {
    void (*entry_func)();
    uint64_t code_virt;
    uint64_t stack_virt;
};

// Kernel thread that allocates User virtual memory and jumps to user space
void user_bootstrap_thread(void* arg) {
    UserBootstrapArgs* args = (UserBootstrapArgs*)arg;
    void (*entry_func)() = args->entry_func;
    uint64_t code_virt = args->code_virt;
    uint64_t stack_virt = args->stack_virt;
    delete args; // Clean up parameter structure on kernel slab heap

    // 1. Allocate physical pages for user space code and stack
    uint64_t code_phys = kernel::pmm_alloc_frame();
    uint64_t stack_phys = kernel::pmm_alloc_frame();

    // 3. Map pages with PRESENT, WRITABLE, and USER permissions
    kernel::vmm_map_page(code_virt, code_phys, kernel::VMM_FLAG_PRESENT | kernel::VMM_FLAG_WRITABLE | kernel::VMM_FLAG_USER);
    kernel::vmm_map_page(stack_virt, stack_phys, kernel::VMM_FLAG_PRESENT | kernel::VMM_FLAG_WRITABLE | kernel::VMM_FLAG_USER);

    // 4. Copy 2048 bytes of entry function code to prevent truncation
    char* src = (char*)entry_func;
    char* dest = (char*)code_phys;
    for (size_t i = 0; i < 2048; ++i) {
        dest[i] = src[i];
    }

    // 5. Jump to User Mode (Ring 3)
    kernel::jump_to_user_mode((void(*)())code_virt, (void*)(stack_virt + 4096));
}

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

    // 7. Initialize Slab Heap Allocator
    bool heap_ok = kernel::heap_init();
    if (serial_ok) {
        if (heap_ok) {
            drivers::Serial::println("[INIT] Slab Heap Allocator initialized.");
        } else {
            drivers::Serial::println("[ERROR] Slab Heap Allocator failed!");
        }
    }

    // Initialize Multitasking Scheduler
    kernel::scheduler_init();

    // Initialize System Calls (MSR registers SCE setup)
    kernel::syscall_init();

    // 8. Initialize VGA text-mode graphics console
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

    // Initialize TarFS if the module is found in the physical memory manager
    {
        uint64_t tar_start = 0, tar_end = 0;
        bool tar_found = kernel::pmm_get_module(0, &tar_start, &tar_end);
        if (tar_found) {
            fs::tarfs_init(tar_start, tar_end);
        } else {
            if (serial_ok) {
                drivers::Serial::println("[TarFS] Error: No Multiboot modules found!");
            }
        }
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

        // --- Heap Allocator Verification ---
        drivers::Vga::println("");
        drivers::Vga::println("[TEST] Starting Kernel Heap Allocator verification...");
        if (serial_ok) {
            drivers::Serial::println("[TEST] Starting Kernel Heap Allocator verification...");
        }

        // Test 1: Allocate small objects and verify slab sharing
        void* ptr1 = kernel::kmalloc(32);
        void* ptr2 = kernel::kmalloc(32);
        if (ptr1 && ptr2) {
            drivers::Vga::println("[TEST] kmalloc 32 bytes: OK");
            if (serial_ok) {
                drivers::Serial::println("[TEST] kmalloc 32 bytes: SUCCESS");
            }
            
            // Check if they share the same physical page frame (slab sharing)
            uint64_t page1 = (uint64_t)ptr1 & ~0xFFFULL;
            uint64_t page2 = (uint64_t)ptr2 & ~0xFFFULL;
            if (page1 == page2) {
                drivers::Vga::println("[TEST] Slab page sharing: OK");
                if (serial_ok) {
                    drivers::Serial::println("[TEST] Slab page sharing verified: SUCCESS");
                }
            } else {
                drivers::Vga::println("[TEST] Slab page sharing: FAILED");
            }
        } else {
            drivers::Vga::println("[TEST] kmalloc 32 bytes: FAILED");
        }

        // Test 2: Test C++ new and delete operator with a custom polymorphic class
        if (heap_ok) {
            TestClass* cpp_obj = new TestClass();
            if (cpp_obj) {
                drivers::Vga::println("[TEST] C++ 'new' operator allocation: OK");
                if (serial_ok) {
                    drivers::Serial::println("[TEST] C++ 'new' operator allocation: SUCCESS");
                }
                
                if (cpp_obj->x == 42 && cpp_obj->y == 100) {
                    drivers::Vga::println("[TEST] C++ constructor execution: OK");
                }
                
                int sum = cpp_obj->get_sum();
                if (sum == 142) {
                    drivers::Vga::println("[TEST] C++ virtual function dispatch: OK");
                    if (serial_ok) {
                        drivers::Serial::println("[TEST] C++ virtual function dispatch: SUCCESS");
                    }
                } else {
                    drivers::Vga::println("[TEST] C++ virtual function dispatch: FAILED");
                }

                delete cpp_obj;
                drivers::Vga::println("[TEST] C++ 'delete' operator execution: OK");
                if (serial_ok) {
                    drivers::Serial::println("[TEST] C++ 'delete' operator execution: SUCCESS");
                }
            } else {
                drivers::Vga::println("[TEST] C++ 'new' operator: FAILED");
            }
        }

        // Test 3: Large allocation (> 2048 bytes)
        void* large_ptr = kernel::kmalloc(8192); // 8KB
        if (large_ptr) {
            drivers::Vga::println("[TEST] kmalloc large allocation (8KB): OK");
            if (serial_ok) {
                drivers::Serial::println("[TEST] kmalloc large allocation (8KB): SUCCESS");
            }
            
            // Verify writing and reading from large mapping
            char* l_buf = (char*)large_ptr;
            l_buf[0] = 'H';
            l_buf[1] = 'E';
            l_buf[8191] = '!';
            if (l_buf[0] == 'H' && l_buf[8191] == '!') {
                drivers::Vga::println("[TEST] Large allocation write/read: OK");
                if (serial_ok) {
                    drivers::Serial::println("[TEST] Large allocation write/read verification: SUCCESS");
                }
            } else {
                drivers::Vga::println("[TEST] Large allocation write/read: FAILED");
            }
            
            kernel::kfree(large_ptr);
            drivers::Vga::println("[TEST] kfree large allocation: OK");
            if (serial_ok) {
                drivers::Serial::println("[TEST] kfree large allocation: SUCCESS");
            }
        } else {
            drivers::Vga::println("[TEST] kmalloc large allocation: FAILED");
        }

        kernel::kfree(ptr1);
        kernel::kfree(ptr2);
        drivers::Vga::println("[TEST] kfree small allocations: OK");
        if (serial_ok) {
            drivers::Serial::println("[TEST] kfree small allocations: SUCCESS");
        }

        // --- TarFS Verification ---
        uint64_t t_start = 0, t_end = 0;
        if (kernel::pmm_get_module(0, &t_start, &t_end)) {
            drivers::Vga::println("");
            drivers::Vga::println("[TEST] Starting VFS TarFS verification...");
            if (serial_ok) {
                drivers::Serial::println("[TEST] Starting VFS TarFS verification...");
            }

            fs::VFSNode* hello_node = fs::VFS::open("/tar/hello.txt");
            if (hello_node) {
                char buf[128];
                fs::File f = { hello_node, 0, 0 };
                ssize_t read_bytes = fs::VFS::read(&f, buf, sizeof(buf) - 1);
                if (read_bytes > 0) {
                    buf[read_bytes] = '\0';
                    drivers::Vga::print("Read /tar/hello.txt: \"");
                    drivers::Vga::print(buf);
                    drivers::Vga::println("\" - SUCCESS");

                    drivers::Serial::print("[TEST] Read /tar/hello.txt: \"");
                    drivers::Serial::print(buf);
                    drivers::Serial::println("\" - SUCCESS");
                }
            } else {
                drivers::Vga::println("[TEST] Failed to open /tar/hello.txt - FAILED");
                drivers::Serial::println("[TEST] Failed to open /tar/hello.txt - FAILED");
            }

            fs::VFSNode* info_node = fs::VFS::open("/tar/docs/info.txt");
            if (info_node) {
                char buf[128];
                fs::File f = { info_node, 0, 0 };
                ssize_t read_bytes = fs::VFS::read(&f, buf, sizeof(buf) - 1);
                if (read_bytes > 0) {
                    buf[read_bytes] = '\0';
                    drivers::Vga::print("Read /tar/docs/info.txt: \"");
                    drivers::Vga::print(buf);
                    drivers::Vga::println("\" - SUCCESS");

                    drivers::Serial::print("[TEST] Read /tar/docs/info.txt: \"");
                    drivers::Serial::print(buf);
                    drivers::Serial::println("\" - SUCCESS");
                }
            } else {
                drivers::Vga::println("[TEST] Failed to open /tar/docs/info.txt - FAILED");
                drivers::Serial::println("[TEST] Failed to open /tar/docs/info.txt - FAILED");
            }

            // Test non-existent file path
            fs::VFSNode* missing = fs::VFS::open("/tar/missing.txt");
            if (!missing) {
                drivers::Vga::println("Open non-existent file /tar/missing.txt returned null - SUCCESS");
                drivers::Serial::println("[TEST] Open non-existent file /tar/missing.txt returned null - SUCCESS");
            } else {
                drivers::Vga::println("Open /tar/missing.txt failed - FAILED");
            }
        }

        // --- Multitasking Verification ---
        drivers::Vga::println("");
        drivers::Vga::println("[TEST] Starting multitasking threads (preemptive scheduling)...");
        if (serial_ok) {
            drivers::Serial::println("[TEST] Starting multitasking threads (preemptive scheduling)...");
        }

        kernel::thread_create(thread_a_func, nullptr);
        kernel::thread_create(thread_b_func, nullptr);
        kernel::thread_create(thread_c_func, nullptr);

        // Spawn User Space Thread (Ring 3 transition & syscall print/yield test)
        UserBootstrapArgs* u_args1 = new UserBootstrapArgs{user_thread_entry, 0x400000000ULL, 0x400001000ULL};
        kernel::thread_create(user_bootstrap_thread, u_args1);

        // Spawn User Space Security verification thread (verifies kernel address protection)
        UserBootstrapArgs* u_args2 = new UserBootstrapArgs{user_security_entry, 0x500000000ULL, 0x500001000ULL};
        kernel::thread_create(user_bootstrap_thread, u_args2);

        // Initialize Local APIC Timer (periodic Vector 32 ticks)
        drivers::Apic::init_timer(0x80000);

        // Enable hardware interrupts
        drivers::Serial::println("[TEST] Enabling interrupts via 'sti' to start scheduling...");
        __asm__ __volatile__ ("sti");

        // Wait a few moments to allow threads A, B, and C to run and interleave
        for (volatile int delay = 0; delay < 50000000; ) {
            delay = delay + 1;
        }

        // Disable interrupts before triggering the final Page Fault crash
        __asm__ __volatile__ ("cli");
        drivers::Vga::println("\n[TEST] Multitasking complete.");
        if (serial_ok) {
            drivers::Serial::println("\n[TEST] Multitasking complete.");
        }

        // Trigger page fault by reading from the unmapped address to demonstrate exception routing
        drivers::Vga::println("");
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
