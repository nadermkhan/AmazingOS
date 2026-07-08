#pragma once

#include "types.hpp"

namespace kernel {

struct SyscallFrame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax; // Syscall number on entry, return value on exit
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

// Initialize Model Specific Registers (MSRs) for system calls
void syscall_init();

// Jump to user mode and begin Ring 3 execution
// entry_point: Code address to execute
// user_stack_top: Pointer to the top of the user stack
extern "C" __attribute__((sysv_abi)) void jump_to_user_mode(void (*entry_point)(), void* user_stack_top);

} // namespace kernel
