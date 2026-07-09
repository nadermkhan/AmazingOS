.code64
.section .text
.global syscall_handler_entry
.global jump_to_user_mode
.extern syscall_dispatcher
.extern current_thread
.extern user_rsp_temp

# System call assembly handler entry (invoked by CPU on syscall instruction)
syscall_handler_entry:
    # 1. Save user RSP to temp variable (safe because interrupts are disabled)
    movq %rsp, user_rsp_temp

    # 2. Switch stack pointer RSP to the current thread's kernel stack
    movq current_thread, %rsp    # RSP points to Thread descriptor
    movq 32(%rsp), %rsp          # RSP = current_thread->rsp0 (offset 32 is rsp0)

    # 3. Construct interrupt stack frame on kernel stack
    pushq $0x1B                  # User SS (Selector 0x18 | RPL 3)
    pushq user_rsp_temp          # User RSP
    pushq %r11                   # User RFLAGS (saved in R11 by CPU)
    pushq $0x23                  # User CS (Selector 0x20 | RPL 3)
    pushq %rcx                   # User RIP (saved in RCX by CPU)

    # 4. Save general purpose registers to context frame
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    # 5. Call C++ dispatcher (1st arg is register frame pointer in RDI)
    movq %rsp, %rdi
    call syscall_dispatcher

    # 6. Restore general purpose registers
.global syscall_restore_registers
syscall_restore_registers:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    # Restore RAX slot (loads return value from syscall_dispatcher)
    popq %rax

    # 7. Restore RIP, RFLAGS, and RSP
    popq %rcx                    # Restore User RIP to RCX
    addq $8, %rsp                # Skip CS
    popq %r11                    # Restore User RFLAGS to R11
    popq %rsp                    # Restore User RSP (switches to user stack)

    # 8. Sanitize registers to prevent kernel data leakage to Ring 3
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %r8,  %r8
    xorq %r9,  %r9
    xorq %r10, %r10

    # 9. Return back to user space (Ring 3)
    sysretq

# extern "C" __attribute__((sysv_abi)) void jump_to_user_mode(void (*entry_point)(), void* user_stack_top);
# entry_point: RDI, user_stack_top: RSI
jump_to_user_mode:
    cli

    # Set data segment registers to User Data selector (0x1B)
    movw $0x1B, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    # Construct the stack frame for iretq far return simulation
    pushq $0x1B                  # User SS
    pushq %rsi                   # User RSP
    pushq $0x202                 # User RFLAGS (Interrupt Enable flag set)
    pushq $0x23                  # User CS
    pushq %rdi                   # User RIP

    # Transition to Ring 3
    iretq

.global fork_child_return
fork_child_return:
    jmp syscall_restore_registers
