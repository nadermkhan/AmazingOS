#include "syscall.hpp"
#include "vmm.hpp"
#include "scheduler.hpp"
#include "../drivers/serial.hpp"
#include "../drivers/vga.hpp"

extern "C" {
    uint64_t user_rsp_temp = 0;
    void syscall_handler_entry(); // Declared at namespace scope
}

namespace kernel {

// MSR register offsets
constexpr uint32_t MSR_EFER   = 0xC0000080;
constexpr uint32_t MSR_STAR   = 0xC0000081;
constexpr uint32_t MSR_LSTAR  = 0xC0000082;
constexpr uint32_t MSR_SFMASK = 0xC0000084;

static void write_msr(uint32_t msr, uint64_t val) {
    uint32_t low = (uint32_t)val;
    uint32_t high = (uint32_t)(val >> 32);
    __asm__ __volatile__ ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ __volatile__ ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void syscall_init() {
    // 1. Enable SCE (System Call Extension) bit 0 in Extended Feature Enable Register (EFER)
    uint64_t efer = read_msr(MSR_EFER);
    write_msr(MSR_EFER, efer | 1);

    // 2. Configure STAR MSR:
    // Bits 32-47: Kernel Segment selector base (0x08 -> Kernel Code selector, 0x10 Kernel Data selector)
    // Bits 48-63: User Segment selector base (0x10 selector index -> User CS = 0x23, User SS = 0x1B)
    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    write_msr(MSR_STAR, star);

    // 3. Configure LSTAR MSR to point to assembly system call entrypoint
    write_msr(MSR_LSTAR, (uint64_t)&syscall_handler_entry);

    // 4. Configure SFMASK MSR (bit 9 is IF, Interrupt flag).
    // Masking it clears IF on entry, disabling hardware interrupts during syscall startup.
    write_msr(MSR_SFMASK, 0x200);

    drivers::Serial::println("[INIT] System Call (SCE) MSRs configured successfully.");
}

// Walk page table to verify pointer safety
bool syscall_validate_buffer(const void* ptr, size_t size) {
    uint64_t addr = (uint64_t)ptr;
    uint64_t end = addr + size;

    uint64_t start_page = addr & ~0xFFFULL;
    uint64_t end_page = (end + 4095) & ~0xFFFULL;

    for (uint64_t page = start_page; page < end_page; page += 4096) {
        if (!vmm_is_user_mapped(page)) {
            return false; // Page is either not present or is a kernel-space page!
        }
    }
    return true;
}

extern "C" __attribute__((sysv_abi)) void syscall_dispatcher(SyscallFrame* frame) {
    // Read syscall number from RAX
    uint64_t syscall_num = frame->rax;

    switch (syscall_num) {
        case 1: { // sys_write
            uint64_t fd = frame->rdi;
            const char* buffer = (const char*)frame->rsi;
            size_t count = frame->rdx;

            // Security Validation: prevent kernel pointer memory leaks
            if (!syscall_validate_buffer(buffer, count)) {
                drivers::Serial::println("[SYSCALL] Security violation: sys_write passed invalid user buffer pointer!");
                frame->rax = (uint64_t)-1; // Return error
                break;
            }

            if (fd == 1) { // stdout / VGA console
                // Print each character as a null-terminated string to match driver APIs
                char temp[2] = {0, 0};
                for (size_t i = 0; i < count; ++i) {
                    temp[0] = buffer[i];
                    drivers::Vga::print(temp);
                    drivers::Serial::print(temp);
                }
                frame->rax = count; // Return characters written
            } else {
                frame->rax = (uint64_t)-1;
            }
            break;
        }

        case 2: { // sys_yield
            // Cooperatively yield processor control to next ready thread
            schedule();
            frame->rax = 0;
            break;
        }

        case 3: { // sys_exit
            // Terminate the current user thread
            thread_exit();
            // This case never returns
            break;
        }

        default: {
            drivers::Serial::print("[SYSCALL] Error: Invalid syscall index: ");
            // Print index in decimal
            char buf[32];
            uint64_t temp = syscall_num;
            int idx = 0;
            if (temp == 0) buf[idx++] = '0';
            else {
                char rev[32];
                int r_idx = 0;
                while (temp > 0) {
                    rev[r_idx++] = '0' + (temp % 10);
                    temp /= 10;
                }
                while (r_idx > 0) buf[idx++] = rev[--r_idx];
            }
            buf[idx] = '\0';
            drivers::Serial::println(buf);

            frame->rax = (uint64_t)-1;
            break;
        }
    }
}

} // namespace kernel
