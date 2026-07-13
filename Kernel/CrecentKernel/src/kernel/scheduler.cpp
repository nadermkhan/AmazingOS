#include "scheduler.hpp"
#include "heap.hpp"
#include "gdt.hpp"
#include "vmm.hpp"
#include "../drivers/serial.hpp"
#include "syscall.hpp"

extern "C" {
    kernel::Thread* current_thread = nullptr;
}

namespace kernel {

// Queue pointers for Runnable threads
static Thread* run_queue_head = nullptr;
static Thread* run_queue_tail = nullptr;

static Thread main_thread;
static Thread* all_threads_head = nullptr;
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
    
    // Scan for the first PRIORITY_HIGH thread in the run queue
    Thread* prev = nullptr;
    Thread* curr = run_queue_head;
    while (curr) {
        if (curr->priority == PRIORITY_HIGH) {
            // Remove curr from the queue
            if (prev) {
                prev->next = curr->next;
                if (!curr->next) {
                    run_queue_tail = prev;
                }
            } else {
                run_queue_head = curr->next;
                if (!run_queue_head) {
                    run_queue_tail = nullptr;
                }
            }
            curr->next = nullptr;
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }

    // Default to enqueued head if no high-priority thread is ready
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
    main_thread.waiting_for_pid = 0;
    main_thread.exit_status = 0;
    main_thread.parent_id = 0;
    main_thread.all_next = nullptr;
    main_thread.priority = PRIORITY_NORMAL;
    all_threads_head = &main_thread;

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
    t->waiting_for_pid = 0;
    t->exit_status = 0;
    t->parent_id = 0;
    t->priority = PRIORITY_NORMAL;

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
    t->all_next = all_threads_head;
    all_threads_head = t;
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
        // drivers::Serial::print("[SCHED] Switching CR3 PML4 base to: ");
        // char hex_buf[19];
        // hex_buf[0] = '0'; hex_buf[1] = 'x';
        // const char* hex_chars = "0123456789ABCDEF";
        // for (int x = 0; x < 16; ++x) {
        //     hex_buf[2 + x] = hex_chars[(next->pml4_phys >> ((15 - x) * 4)) & 0x0F];
        // }
        // hex_buf[18] = '\0';
        // drivers::Serial::println(hex_buf);
        active_pml4 = next->pml4_phys;
        __asm__ __volatile__ ("mov %0, %%cr3" : : "r"(next->pml4_phys) : "memory");
    }

    // Load next thread's kernel stack top into TSS for privilege transition exception safety
    gdt_update_tss_rsp0(next->rsp0);

    // Perform context switch
    context_switch(next, prev);

    lock.unlock();
}

void thread_exit(int status) {
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
    current->exit_status = status;
    current->state = THREAD_TERMINATED;

    // Scan all threads to wake up waiting parent or re-parent orphaned children
    Thread* prev_t = nullptr;
    Thread* curr_t = all_threads_head;
    while (curr_t) {
        Thread* next_t = curr_t->all_next;
        if (curr_t->state == THREAD_BLOCKED && curr_t->waiting_for_pid == current->id) {
            // Wake up parent
            curr_t->state = THREAD_RUNNABLE;
            curr_t->waiting_for_pid = 0;
            enqueue(curr_t);
        }
        
        if (curr_t->parent_id == current->id) {
            // Re-parent orphan to main thread (0)
            curr_t->parent_id = 0;
            if (curr_t->state == THREAD_TERMINATED) {
                // Reap orphan zombie immediately
                // Remove from all_threads list
                if (prev_t) {
                    prev_t->all_next = next_t;
                } else {
                    all_threads_head = next_t;
                }
                kfree(curr_t->stack_limit);
                delete curr_t;
                // Since we deleted curr_t, do not update prev_t
                curr_t = next_t;
                continue;
            }
        }
        
        prev_t = curr_t;
        curr_t = next_t;
    }

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

    // Set terminated thread resources for cleanup ONLY if parent is 0 (detached/kernel thread)
    if (current->parent_id == 0) {
        stack_to_free = current->stack_limit;
        thread_to_free = current;
    } else {
        stack_to_free = nullptr;
        thread_to_free = nullptr;
    }

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

int scheduler_waitpid(uint64_t pid, int* wstatus) {
    IntLock lock;
    lock.lock();

    drivers::Serial::print("[WAITPID] Parent ");
    char parent_id_str[16];
    int idx = 0;
    uint64_t temp = current_thread->id;
    if (temp == 0) parent_id_str[idx++] = '0';
    else {
        char rev[16];
        int r_idx = 0;
        while (temp > 0) { rev[r_idx++] = '0' + (temp % 10); temp /= 10; }
        while (r_idx > 0) parent_id_str[idx++] = rev[--r_idx];
    }
    parent_id_str[idx] = '\0';
    drivers::Serial::print(parent_id_str);
    
    drivers::Serial::print(" waiting for child ");
    char child_id_str[16];
    idx = 0;
    temp = pid;
    if (temp == 0) child_id_str[idx++] = '0';
    else {
        char rev[16];
        int r_idx = 0;
        while (temp > 0) { rev[r_idx++] = '0' + (temp % 10); temp /= 10; }
        while (r_idx > 0) child_id_str[idx++] = rev[--r_idx];
    }
    child_id_str[idx] = '\0';
    drivers::Serial::print(child_id_str);
    drivers::Serial::print(". Current active threads: ");
    
    Thread* t_debug = all_threads_head;
    while (t_debug) {
        char tid_str[16];
        idx = 0;
        temp = t_debug->id;
        if (temp == 0) tid_str[idx++] = '0';
        else {
            char rev[16];
            int r_idx = 0;
            while (temp > 0) { rev[r_idx++] = '0' + (temp % 10); temp /= 10; }
            while (r_idx > 0) tid_str[idx++] = rev[--r_idx];
        }
        tid_str[idx] = '\0';
        drivers::Serial::print(tid_str);
        
        if (t_debug->state == THREAD_RUNNING) drivers::Serial::print("(RUNNING) ");
        else if (t_debug->state == THREAD_RUNNABLE) drivers::Serial::print("(RUNNABLE) ");
        else if (t_debug->state == THREAD_BLOCKED) drivers::Serial::print("(BLOCKED) ");
        else if (t_debug->state == THREAD_TERMINATED) drivers::Serial::print("(TERMINATED) ");
        t_debug = t_debug->all_next;
    }
    drivers::Serial::println("");

    // 1. Find child thread
    Thread* child = nullptr;
    Thread* curr = all_threads_head;
    while (curr) {
        if (curr->id == pid) {
            child = curr;
            break;
        }
        curr = curr->all_next;
    }

    if (!child) {
        lock.unlock();
        return -1; // -ECHILD
    }

    // 2. Check if child is a zombie (already terminated)
    if (child->state == THREAD_TERMINATED) {
        int status = child->exit_status;
        if (wstatus) {
            *wstatus = status;
        }

        // Clean up child thread structures completely (reaping the zombie)
        if (all_threads_head == child) {
            all_threads_head = child->all_next;
        } else {
            Thread* p = all_threads_head;
            while (p && p->all_next != child) {
                p = p->all_next;
            }
            if (p) {
                p->all_next = child->all_next;
            }
        }

        kfree(child->stack_limit);
        delete child;

        lock.unlock();
        return (int)pid;
    }

    // 3. Child is still running, so we block the parent
    Thread* parent = current_thread;
    parent->state = THREAD_BLOCKED;
    parent->waiting_for_pid = pid;

    // Yield CPU control
    lock.unlock();
    schedule();
    lock.lock();

    // After waking up, the child is guaranteed to be terminated!
    child = nullptr;
    curr = all_threads_head;
    while (curr) {
        if (curr->id == pid) {
            child = curr;
            break;
        }
        curr = curr->all_next;
    }

    if (child && child->state == THREAD_TERMINATED) {
        int status = child->exit_status;
        if (wstatus) {
            *wstatus = status;
        }

        // Remove from all_threads list
        if (all_threads_head == child) {
            all_threads_head = child->all_next;
        } else {
            Thread* p = all_threads_head;
            while (p && p->all_next != child) {
                p = p->all_next;
            }
            if (p) {
                p->all_next = child->all_next;
            }
        }

        kfree(child->stack_limit);
        delete child;
    }

    lock.unlock();
    return (int)pid;
}

void scheduler_enqueue(Thread* t) {
    IntLock lock;
    lock.lock();
    enqueue(t);
    lock.unlock();
}

void scheduler_register_thread(Thread* t) {
    IntLock lock;
    lock.lock();
    t->all_next = all_threads_head;
    all_threads_head = t;
    lock.unlock();
}

void scheduler_unregister_thread(Thread* t) {
    IntLock lock;
    lock.lock();
    if (all_threads_head == t) {
        all_threads_head = t->all_next;
    } else {
        Thread* curr = all_threads_head;
        while (curr && curr->all_next != t) {
            curr = curr->all_next;
        }
        if (curr) {
            curr->all_next = t->all_next;
        }
    }
    lock.unlock();
}

uint64_t scheduler_generate_id() {
    return next_thread_id++;
}

static Thread* gui_thread_ptr = nullptr;
static uint64_t gui_thread_wake_tsc = 0;

void scheduler_register_gui_thread(Thread* t) {
    gui_thread_ptr = t;
    if (t) {
        t->priority = PRIORITY_HIGH;
    }
}

void scheduler_wake_gui_thread() {
    if (gui_thread_ptr && gui_thread_ptr->state == THREAD_BLOCKED) {
        gui_thread_ptr->state = THREAD_RUNNABLE;
        enqueue(gui_thread_ptr);
    }
}

void scheduler_set_gui_wake_tsc(uint64_t tsc) {
    gui_thread_wake_tsc = tsc;
}

uint64_t scheduler_get_gui_wake_tsc() {
    return gui_thread_wake_tsc;
}

} // namespace kernel
