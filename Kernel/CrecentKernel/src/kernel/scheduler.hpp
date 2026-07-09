#pragma once

#include "types.hpp"

#include "../fs/vfs.hpp"

namespace kernel {

enum ThreadState {
    THREAD_RUNNABLE,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_TERMINATED
};

struct Thread {
    uint64_t rsp;          // Offset 0: Saved stack pointer
    uint64_t id;           // Unique Thread ID
    ThreadState state;     // Thread execution state
    void* stack_limit;     // Physical address of the allocated stack page (for cleanup)
    uint64_t rsp0;         // Top of kernel stack (for syscall RSP restore)
    Thread* next;          // Queue link
    Thread* all_next;      // Global list link

    static constexpr int MAX_FILE_DESCRIPTORS = 16;
    fs::File fd_pool[MAX_FILE_DESCRIPTORS];
    fs::File* fd_table[MAX_FILE_DESCRIPTORS];
    uint64_t pml4_phys;
    uint64_t waiting_for_pid; // PID of the child thread this thread is waiting on
    int exit_status;          // Stored exit code of this thread
    uint64_t parent_id;       // PID of the parent thread
};

extern "C" Thread* current_thread;

// Initialize the scheduler and register the main (boot) thread
void scheduler_init();

// Create a new kernel thread
// func: Entry point function of the thread
// arg: Argument passed to the entry point function
Thread* thread_create(void (*func)(void*), void* arg);

// Voluntarily yield CPU time and trigger Round-Robin context switch
void schedule();

// Terminate the current running thread and reclaim its resources
void thread_exit(int status = 0);

// Return the currently running thread descriptor
Thread* scheduler_get_current();

// Wait for a child process to terminate and reclaim its TCB
int scheduler_waitpid(uint64_t pid, int* wstatus);

// Enqueue a thread manually (for fork/exec child processes)
void scheduler_enqueue(Thread* t);

// Register and unregister threads in the global list
void scheduler_register_thread(Thread* t);
void scheduler_unregister_thread(Thread* t);

// Generate a unique thread ID
uint64_t scheduler_generate_id();

} // namespace kernel
