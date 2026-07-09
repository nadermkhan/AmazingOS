#include "scheduler.hpp"
#include "heap.hpp"
#include "gdt.hpp"
#include "vmm.hpp"
#include "../drivers/serial.hpp"

extern "C" {
    kernel::Thread* current_thread = nullptr;
}

namespace kernel {

// Queue pointers for Runnable threads
static Thread* run_queue_head = nullptr;
static Thread* run_queue_tail = nullptr;

static Thread main_thread;
static uint64_t next_thread_id = 0;

// Shared pointers for asynchronous thread resource reclamation (reaper logic)
static void* stack_to_free = nullptr;
static Thread* thread_to_free = nullptr;

// Interrupt atomic lock structure
struct IntLock {
    bool enabled;
    void lock() {
        uint64_t rflags;
        // Push RFLAGS, save to register, disable interrupts (cli)
        __asm__ __volatile__ (
            "pushfq\n\t"
            "popq %0\n\t"
            "cli"
            : "=r"(rflags)
        );
        // Bit 9 of RFLAGS is the IF flag (Interrupt Enable Flag)
        enabled = (rflags & (1ULL << 9)) != 0;
    }
    void unlock() {
        if (enabled) {
            __asm__ __volatile__ ("sti");
        }
    }
};

// Queue operations
static void enqueue(Thread* t) {
    t->next = nullptr;
    if (!run_queue_head) {
        run_queue_head = t;
        run_queue_tail = t;
    } else {
        run_queue_tail->next = t;
        run_queue_tail = t;
    }
}

static Thread* dequeue() {
    if (!run_queue_head) {
        return nullptr;
    }
    Thread* t = run_queue_head;
    run_queue_head = run_queue_head->next;
    if (!run_queue_head) {
        run_queue_tail = nullptr;
    }
    t->next = nullptr;
    return t;
}

extern "C" char boot_stack_top[];

void scheduler_init() {
    main_thread.rsp = 0;
    main_thread.id = next_thread_id++;
    main_thread.state = THREAD_RUNNING;
    main_thread.stack_limit = nullptr;
    main_thread.rsp0 = (uint64_t)boot_stack_top;
    main_thread.next = nullptr;
    main_thread.pml4_phys = active_pml4;

    // Initialize file descriptor table for main thread
    for (int i = 0; i < Thread::MAX_FILE_DESCRIPTORS; ++i) {
        main_thread.fd_pool[i].node = nullptr;
        main_thread.fd_pool[i].offset = 0;
        main_thread.fd_pool[i].flags = 0;
        main_thread.fd_pool[i].ref_count = 0;
        main_thread.fd_table[i] = nullptr;
    }
    // Set standard FDs
    main_thread.fd_pool[0].node = (fs::VFSNode*)1;
    main_thread.fd_pool[0].ref_count = 1;
    main_thread.fd_table[0] = &main_thread.fd_pool[0];

    main_thread.fd_pool[1].node = (fs::VFSNode*)2;
    main_thread.fd_pool[1].ref_count = 1;
    main_thread.fd_table[1] = &main_thread.fd_pool[1];

    main_thread.fd_pool[2].node = (fs::VFSNode*)3;
    main_thread.fd_pool[2].ref_count = 1;
    main_thread.fd_table[2] = &main_thread.fd_pool[2];

    current_thread = &main_thread;

    // Load boot thread's kernel stack top into TSS for privilege transition exception safety
    gdt_update_tss_rsp0(main_thread.rsp0);

    drivers::Serial::println("[INIT] Multitasking scheduler initialized.");
}

extern "C" void thread_bootstrap(); // Assembly entry in switch.S
extern "C" __attribute__((sysv_abi)) void thread_entry_stub(void (*func)(void*), void* arg) {
    func(arg);
    thread_exit();
}

Thread* thread_create(void (*func)(void*), void* arg) {
    // 1. Allocate stack page from the heap
    void* stack_page = kmalloc(4096);
    if (!stack_page) return nullptr;

    // 2. Set up the initial stack frame.
    // Stack grows downwards, top is base + 4096.
    uint64_t* stack = (uint64_t*)((uintptr_t)stack_page + 4096);

    // Prepare context matched to context_switch pop ordering:
    // popq R12, popq R13, popq R14, popq R15, popq RBX, popq RBP, ret (pops RIP)
    *(--stack) = (uint64_t)&thread_bootstrap; // RIP
    *(--stack) = 0; // RBP
    *(--stack) = 0; // RBX
    *(--stack) = 0; // R15
    *(--stack) = 0; // R14
    *(--stack) = (uint64_t)arg;  // R13
    *(--stack) = (uint64_t)func; // R12 (R12 and R13 passed to thread_bootstrap)

    // 3. Instantiate the Thread TCB
    Thread* t = new Thread();
    t->rsp = (uint64_t)stack;
    t->id = next_thread_id++;
    t->state = THREAD_RUNNABLE;
    t->stack_limit = stack_page;
    t->rsp0 = (uint64_t)stack_page + 4096;
    t->next = nullptr;
    t->pml4_phys = current_thread ? current_thread->pml4_phys : active_pml4;

    // Initialize file descriptor table
    for (int i = 0; i < Thread::MAX_FILE_DESCRIPTORS; ++i) {
        t->fd_pool[i].node = nullptr;
        t->fd_pool[i].offset = 0;
        t->fd_pool[i].flags = 0;
        t->fd_pool[i].ref_count = 0;
        t->fd_table[i] = nullptr;
    }
    // Set standard FDs
    t->fd_pool[0].node = (fs::VFSNode*)1;
    t->fd_pool[0].ref_count = 1;
    t->fd_table[0] = &t->fd_pool[0];

    t->fd_pool[1].node = (fs::VFSNode*)2;
    t->fd_pool[1].ref_count = 1;
    t->fd_table[1] = &t->fd_pool[1];

    t->fd_pool[2].node = (fs::VFSNode*)3;
    t->fd_pool[2].ref_count = 1;
    t->fd_table[2] = &t->fd_pool[2];

    // 4. Add to run queue
    IntLock lock;
    lock.lock();
    enqueue(t);
    lock.unlock();

    return t;
}

extern "C" __attribute__((sysv_abi)) void context_switch(Thread* next, Thread* prev);

void schedule() {
    IntLock lock;
    lock.lock();

    // Free terminated thread structures asynchronously
    if (stack_to_free) {
        kfree(stack_to_free);
        stack_to_free = nullptr;
    }
    if (thread_to_free) {
        delete thread_to_free;
        thread_to_free = nullptr;
    }

    Thread* prev = current_thread;

    // If current thread was running, requeue it as runnable
    if (prev->state == THREAD_RUNNING) {
        prev->state = THREAD_RUNNABLE;
        enqueue(prev);
    }

    // Dequeue next runnable thread
    Thread* next = dequeue();
    if (!next) {
        // No other runnable threads, resume current thread
        if (prev->state == THREAD_RUNNABLE) {
            prev->state = THREAD_RUNNING;
        }
        lock.unlock();
        return;
    }

    next->state = THREAD_RUNNING;
    current_thread = next;

    // Switch address space if necessary (and flush TLB)
    uint64_t current_cr3;
    __asm__ __volatile__ ("mov %%cr3, %0" : "=r"(current_cr3));
    current_cr3 &= ~0xFFFULL;
    if (next->pml4_phys != current_cr3) {
        drivers::Serial::print("[SCHED] Switching CR3 PML4 base to: ");
        char hex_buf[19];
        hex_buf[0] = '0'; hex_buf[1] = 'x';
        const char* hex_chars = "0123456789ABCDEF";
        for (int x = 0; x < 16; ++x) {
            hex_buf[2 + x] = hex_chars[(next->pml4_phys >> ((15 - x) * 4)) & 0x0F];
        }
        hex_buf[18] = '\0';
        drivers::Serial::println(hex_buf);
        active_pml4 = next->pml4_phys;
        __asm__ __volatile__ ("mov %0, %%cr3" : : "r"(next->pml4_phys) : "memory");
    }

    // Load next thread's kernel stack top into TSS for privilege transition exception safety
    gdt_update_tss_rsp0(next->rsp0);

    // Perform context switch
    context_switch(next, prev);

    lock.unlock();
}

void thread_exit() {
    IntLock lock;
    lock.lock();

    // Free terminated thread structures asynchronously
    if (stack_to_free) {
        kfree(stack_to_free);
        stack_to_free = nullptr;
    }
    if (thread_to_free) {
        // Reclaim custom user process PML4 page tables
        if (thread_to_free->pml4_phys != main_thread.pml4_phys) {
            // vmm_destroy_user_address_space(thread_to_free->pml4_phys);
        }
        delete thread_to_free;
        thread_to_free = nullptr;
    }

    Thread* current = current_thread;
    current->state = THREAD_TERMINATED;

    // Retrieve next runnable thread
    Thread* next = dequeue();
    if (!next) {
        drivers::Serial::println("[SCHED] Error: All threads exited! Halting CPU.");
        while (true) {
            __asm__ __volatile__ ("cli\n\thlt");
        }
    }

    next->state = THREAD_RUNNING;
    current_thread = next;

    // Set terminated thread resources for cleanup by next scheduled thread
    stack_to_free = current->stack_limit;
    thread_to_free = current;

    // Switch address space if necessary (and flush TLB)
    uint64_t current_cr3;
    __asm__ __volatile__ ("mov %%cr3, %0" : "=r"(current_cr3));
    current_cr3 &= ~0xFFFULL;
    if (next->pml4_phys != current_cr3) {
        active_pml4 = next->pml4_phys;
        __asm__ __volatile__ ("mov %0, %%cr3" : : "r"(next->pml4_phys) : "memory");
    }

    // Load next thread's kernel stack top into TSS
    gdt_update_tss_rsp0(next->rsp0);

    // Dummy stack location to switch out of
    static uint64_t dummy_rsp;
    context_switch(next, (Thread*)&dummy_rsp);

    // This block is never reached
    lock.unlock();
}

Thread* scheduler_get_current() {
    return current_thread;
}

void scheduler_enqueue(Thread* t) {
    IntLock lock;
    lock.lock();
    enqueue(t);
    lock.unlock();
}

uint64_t scheduler_generate_id() {
    return next_thread_id++;
}

} // namespace kernel
