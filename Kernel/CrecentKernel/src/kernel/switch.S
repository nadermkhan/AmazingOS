.code64
.section .text
.global context_switch
.global thread_bootstrap
.extern thread_entry_stub

# extern "C" __attribute__((sysv_abi)) void context_switch(Thread* next, Thread* prev);
# next: RDI, prev: RSI
context_switch:
    # Save callee-saved registers of the current thread (prev)
    pushq %rbp
    pushq %rbx
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12

    # Save current stack pointer RSP to prev->rsp (prev is in RSI)
    movq %rsp, (%rsi)

    # Load new stack pointer RSP from next->rsp (next is in RDI)
    movq (%rdi), %rsp

    # Restore callee-saved registers of the next thread (next)
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    popq %rbx
    popq %rbp

    ret

# Assembly bootstrap entry point for newly created threads
# Callee-saved R12 contains func, R13 contains arg (written by thread_create)
thread_bootstrap:
    # Enable hardware interrupts inside the new thread context
    sti

    # Move arguments into System V ABI registers (1st in RDI, 2nd in RSI)
    movq %r12, %rdi
    movq %r13, %rsi

    # Call the C++ entry stub (which executes func(arg) and calls thread_exit)
    call thread_entry_stub

    # Fallback halt loop
.halt_loop:
    cli
    hlt
    jmp .halt_loop
