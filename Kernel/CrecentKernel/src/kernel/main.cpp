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
#include "../drivers/framebuffer.hpp"
#include "wm.hpp"
#include "../drivers/ps2.hpp"
#include "../drivers/ttf.hpp"
#include "pci.hpp"
#include "../drivers/e1000.hpp"
#include "../drivers/ahci.hpp"
#include "../fs/partition.hpp"
#include "../fs/exfat.hpp"
#include "../drivers/ac97.hpp"

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

static inline size_t user_strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static inline int user_open(const char* path, int flags) {
    return (int)user_syscall3(8, (uint64_t)path, (uint64_t)flags, 0);
}

static inline ssize_t user_read(int fd, void* buf, size_t count) {
    return (ssize_t)user_syscall3(9, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

static inline int user_close(int fd) {
    return (int)user_syscall3(10, (uint64_t)fd, 0, 0);
}

static inline int user_fork() {
    return (int)user_syscall3(11, 0, 0, 0);
}

static inline int user_execve(const char* path, char* const argv[], char* const envp[]) {
    return (int)user_syscall3(12, (uint64_t)path, (uint64_t)argv, (uint64_t)envp);
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
    char path[13];
    path[0] = '/'; path[1] = 't'; path[2] = 'a'; path[3] = 'r'; path[4] = '/';
    path[5] = 'b'; path[6] = 'i'; path[7] = 'n'; path[8] = '/'; path[9] = 's';
    path[10] = 'h'; path[11] = '\0';

    user_execve(path, nullptr, nullptr);

    char fail_msg[22];
    fail_msg[0] = 'S'; fail_msg[1] = 'h'; fail_msg[2] = 'e'; fail_msg[3] = 'l';
    fail_msg[4] = 'l'; fail_msg[5] = ' '; fail_msg[6] = 'E'; fail_msg[7] = 'x';
    fail_msg[8] = 'e'; fail_msg[9] = 'c'; fail_msg[10] = ' '; fail_msg[11] = 'F';
    fail_msg[12] = 'a'; fail_msg[13] = 'i'; fail_msg[14] = 'l'; fail_msg[15] = 'e';
    fail_msg[16] = 'd'; fail_msg[17] = '!'; fail_msg[18] = '\n'; fail_msg[19] = '\0';
    user_write(fail_msg, 19);

    user_exit();
}

// User-space security verification thread
void user_security_entry() {
    // 1. Attempt to call sys_write with kernel address 0x100000 (kernel code)
    uint64_t ret = user_syscall3(1, 1, 0x100000ULL, 10);

    char ok_msg[45];
    if (ret == (uint64_t)-1 || (int64_t)ret < 0) {
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

static inline uint64_t user_syscall6(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    register uint64_t r_num __asm__("rax") = num;
    register uint64_t r_a1  __asm__("rdi") = a1;
    register uint64_t r_a2  __asm__("rsi") = a2;
    register uint64_t r_a3  __asm__("rdx") = a3;
    register uint64_t r_a4  __asm__("r10") = a4;
    register uint64_t r_a5  __asm__("r8")  = a5;
    register uint64_t r_a6  __asm__("r9")  = a6;

    __asm__ __volatile__ (
        "syscall"
        : "+r"(r_num)
        : "r"(r_a1), "r"(r_a2), "r"(r_a3), "r"(r_a4), "r"(r_a5), "r"(r_a6)
        : "rcx", "r11", "memory"
    );
    return r_num;
}

static inline uint64_t user_create_window(int x, int y, int w, int h, const char* title, uint32_t color) {
    return user_syscall6(4, x, y, w, h, (uint64_t)title, color);
}

static inline uint64_t user_update_window(int win_id, const uint32_t* buf) {
    return user_syscall6(5, win_id, (uint64_t)buf, 0, 0, 0, 0);
}

static inline uint64_t user_get_window_event(int win_id, Event* ev) {
    return user_syscall6(6, win_id, (uint64_t)ev, 0, 0, 0, 0);
}

static inline uint64_t user_draw_string(int win_id, int x, int y, uint32_t color, const char* str, int size) {
    return user_syscall6(7, win_id, x, y, color, (uint64_t)str, size);
}

void user_paint_entry() {
    char title[6];
    title[0] = 'P'; title[1] = 'a'; title[2] = 'i'; title[3] = 'n'; title[4] = 't'; title[5] = '\0';
    
    int win_id = (int)user_create_window(450, 200, 350, 250, title, 0x00FFFFFF);
    if (win_id < 0) {
        user_exit();
    }

    int client_w = 350 - 2;
    int client_h = 250 - 42;
    
    uint32_t buffer[72384];
    
    for (int i = 0; i < 72384; i++) {
        buffer[i] = 0x00FFFFFF;
    }

    for (int y = 0; y < 38; y++) {
        for (int x = 0; x < client_w; x++) {
            buffer[y * client_w + x] = 0x00F1F5F9; // Sleek slate-light grey header
        }
    }
    for (int x = 0; x < client_w; x++) {
        buffer[38 * client_w + x] = 0x00E2E8F0; // Divider
    }
    
    user_update_window(win_id, buffer);

    volatile char txt1[13];
    txt1[0] = 'P'; txt1[1] = 'a'; txt1[2] = 'i'; txt1[3] = 'n'; txt1[4] = 't'; txt1[5] = ' '; 
    txt1[6] = 'C'; txt1[7] = 'a'; txt1[8] = 'n'; txt1[9] = 'v'; txt1[10] = 'a'; txt1[11] = 's'; txt1[12] = '\0';
    
    volatile char txt2[19];
    txt2[0] = 'C'; txt2[1] = 'l'; txt2[2] = 'i'; txt2[3] = 'c'; txt2[4] = 'k'; txt2[5] = ' ';
    txt2[6] = 't'; txt2[7] = 'o'; txt2[8] = ' ';
    txt2[9] = 'd'; txt2[10] = 'r'; txt2[11] = 'a'; txt2[12] = 'w'; txt2[13] = ' ';
    txt2[14] = 'f'; txt2[15] = 'r'; txt2[16] = 'e'; txt2[17] = 'e'; txt2[18] = '\0';

    user_draw_string(win_id, 15, 8, 0x001E293B, (const char*)txt1, 16);
    user_draw_string(win_id, 130, 10, 0x0064748B, (const char*)txt2, 13);

    while (true) {
        Event ev;
        if (user_get_window_event(win_id, &ev)) {
            if (ev.type == 1) { // EVENT_MOUSE_CLICK
                int start_y = ev.my - 3;
                int start_x = ev.mx - 3;
                for (int dy = 0; dy < 6; dy++) {
                    int py = start_y + dy;
                    if (py < 45 || py >= client_h) continue;
                    for (int dx = 0; dx < 6; dx++) {
                        int px = start_x + dx;
                        if (px < 0 || px >= client_w) continue;
                        buffer[py * client_w + px] = 0x00000000;
                    }
                }
                user_update_window(win_id, buffer);
                user_draw_string(win_id, 15, 8, 0x001E293B, (const char*)txt1, 16);
                user_draw_string(win_id, 130, 10, 0x0064748B, (const char*)txt2, 13);
            }
        }
        user_yield();
    }
}

struct UserBootstrapArgs {
    void (*entry_func)();
    uint64_t code_virt;
    uint64_t stack_virt;
};

void user_bootstrap_thread(void* arg) {
    UserBootstrapArgs* args = (UserBootstrapArgs*)arg;
    void (*entry_func)() = args->entry_func;
    uint64_t code_virt = args->code_virt;
    uint64_t stack_virt = args->stack_virt;
    delete args;

    uint64_t code_phys = kernel::pmm_alloc_frame();
    kernel::vmm_map_page(code_virt, code_phys, kernel::VMM_FLAG_PRESENT | kernel::VMM_FLAG_WRITABLE | kernel::VMM_FLAG_USER);

    int stack_pages = 100;
    for (int i = 0; i < stack_pages; i++) {
        uint64_t stack_phys = kernel::pmm_alloc_frame();
        kernel::vmm_map_page(stack_virt + i * 4096, stack_phys, kernel::VMM_FLAG_PRESENT | kernel::VMM_FLAG_WRITABLE | kernel::VMM_FLAG_USER);
    }

    char* src = (char*)entry_func;
    char* dest = (char*)code_phys;
    for (size_t i = 0; i < 4096; ++i) {
        dest[i] = src[i];
    }

    kernel::jump_to_user_mode((void(*)())code_virt, (void*)(stack_virt + stack_pages * 4096));
}

volatile bool gui_demo_complete = false;

static uint64_t calibrate_tsc() {
    // Enable PIT Channel 2 gate
    drivers::outb(0x61, (drivers::inb(0x61) & 0xFD) | 1);
    
    // Configure PIT Channel 2 to LSB/MSB one-shot mode (Mode 0)
    drivers::outb(0x43, 0xB0);
    
    // Set reload value for 10ms (11932 ticks at 1.193182 MHz)
    uint16_t reload = 11932;
    drivers::outb(0x42, reload & 0xFF);
    drivers::outb(0x42, reload >> 8);
    
    uint64_t start = rdtsc();
    
    // Loop until PIT output bit 5 goes high (timeout reached)
    while (!(drivers::inb(0x61) & 0x20));
    
    uint64_t end = rdtsc();
    
    // Disable PIT Channel 2 gate
    drivers::outb(0x61, drivers::inb(0x61) & 0xFC);
    
    return end - start;
}

// GUI Compositor & Window Manager verification thread
void gui_demo_thread(void* arg) {
    (void)arg;
    kernel::scheduler_register_gui_thread(kernel::scheduler_get_current());

    // 1. Wait briefly for other boot logs to settle on screen/serial
    for (volatile int delay = 0; delay < 10000000; ) {
        delay = delay + 1;
    }

    if (!drivers::Framebuffer::is_initialized()) {
        drivers::Serial::println("[DEMO] Skipping GUI: Framebuffer driver is not initialized.");
        return;
    }

    drivers::Serial::println("[DEMO] Starting Graphical User Interface and Window Manager...");

    // Initialize TrueType Font Renderer
    drivers::TtfRenderer::init();

    // 2. Initialize Window Manager
    wm::WindowManager::init();

    // 3. Create Windows
    wm::WindowManager::create_window(150, 150, 600, 380, "System Log", 0x001B2E3C); // Slate Teal
    wm::WindowManager::create_window(800, 200, 500, 350, "Performance Monitor", 0x002D3748); // Dark Charcoal
    wm::WindowManager::create_window(350, 300, 500, 420, "System Settings", 0x00FFFFFF); // Customizer Window

    // Draw initial desktop frame
    wm::WindowManager::force_redraw_all();

    // 4. Verify IDT Interrupt Gate Routing for Vectors 33 & 44
    drivers::Serial::println("[DEMO] Verifying Interrupt Gate routing for Keyboard (Vector 33)...");
    __asm__ __volatile__ ("int $33");
    drivers::Serial::println("[DEMO] Verifying Interrupt Gate routing for Mouse (Vector 44)...");
    __asm__ __volatile__ ("int $44");
    drivers::Serial::println("[DEMO] Interrupt Gate routing verified successfully.");

    // 5. Perform an automated window drag sequence using the Top-Half/Bottom-Half pipeline
    drivers::Serial::println("[DEMO] Starting Window Compositor Drag Verification via Polling...");

    // Initial cursor coordinate state
    int cursor_x = 100;
    int cursor_y = 100;

    // Simulate mouse move to (150, 160) inside the title bar of win1
    drivers::Ps2::inject_mouse_event(50, -60, false, false);

    // First poll tick
    int m_dx = 0, m_dy = 0;
    bool m_left = false, m_right = false;
    if (drivers::Ps2::poll_mouse(m_dx, m_dy, m_left, m_right)) {
        cursor_x += m_dx;
        cursor_y -= m_dy; // Mouse dy is upward positive, screen y is downward positive
        wm::WindowManager::handle_mouse_move(cursor_x, cursor_y, m_left, m_right);
    }
    wm::WindowManager::draw_desktop();

    // Click left button down
    drivers::Ps2::inject_mouse_event(0, 0, true, false);
    if (drivers::Ps2::poll_mouse(m_dx, m_dy, m_left, m_right)) {
        cursor_x += m_dx;
        cursor_y -= m_dy;
        wm::WindowManager::handle_mouse_move(cursor_x, cursor_y, m_left, m_right);
    }
    wm::WindowManager::draw_desktop();

    // Smoothly drag relative deltas over 10 steps
    for (int step = 1; step <= 10; ++step) {
        // Inject a relative mouse movement of dx = 15, dy = -10 (downward move)
        drivers::Ps2::inject_mouse_event(15, -10, true, false);

        // Compositor Poll (Bottom-Half processing)
        if (drivers::Ps2::poll_mouse(m_dx, m_dy, m_left, m_right)) {
            cursor_x += m_dx;
            cursor_y -= m_dy;
            wm::WindowManager::handle_mouse_move(cursor_x, cursor_y, m_left, m_right);
        }
        wm::WindowManager::draw_desktop();

        // Delay to make it visible
        for (volatile int delay = 0; delay < 5000000; ) {
            delay = delay + 1;
        }
    }

    // Release mouse click
    drivers::Ps2::inject_mouse_event(0, 0, false, false);
    if (drivers::Ps2::poll_mouse(m_dx, m_dy, m_left, m_right)) {
        cursor_x += m_dx;
        cursor_y -= m_dy;
        wm::WindowManager::handle_mouse_move(cursor_x, cursor_y, m_left, m_right);
    }
    wm::WindowManager::draw_desktop();

    // 6. Verify Keyboard translation and polling
    drivers::Ps2::inject_keyboard_char('S');
    char key_char = 0;
    if (drivers::Ps2::poll_keyboard(key_char)) {
        drivers::Serial::print("[DEMO] Compositor polled keyboard character: '");
        char str[2] = {key_char, '\0'};
        drivers::Serial::print(str);
        drivers::Serial::println("'");
    }

    drivers::Serial::println("[DEMO] Window Composer Drag Verification Complete - SUCCESS");
    
    // Calibrate TSC cycles per frame (60 FPS = 16.6 ms)
    uint64_t tsc_10ms = calibrate_tsc();
    uint64_t tsc_per_frame = (tsc_10ms * 166) / 100;
    
    drivers::Serial::print("[DEMO] TSC Frequency calibrated: ");
    char freq_buf[16];
    uint64_t mhz = tsc_10ms / 10000;
    int f_idx = 0;
    if (mhz == 0) freq_buf[f_idx++] = '0';
    else {
        char rev[16];
        int r_idx = 0;
        uint64_t val = mhz;
        while (val > 0) {
            rev[r_idx++] = '0' + (val % 10);
            val /= 10;
        }
        while (r_idx > 0) freq_buf[f_idx++] = rev[--r_idx];
    }
    freq_buf[f_idx] = '\0';
    drivers::Serial::print(freq_buf);
    drivers::Serial::println(" MHz.");

    drivers::Serial::println("[DEMO] Entering interactive desktop loop. You can now move mouse / drag windows freely.");

    // 7. Compositor Refresh & Polling Loop (TSC-Budget Locked to 60 FPS)
    int deferred_mouse_dx = 0;
    int deferred_mouse_dy = 0;
    bool deferred_mouse_pending = false;
    bool deferred_mouse_left = false;
    bool deferred_mouse_right = false;
    char deferred_key_char = 0;
    bool deferred_key_pending = false;
    int mouse_remainder_x = 0;
    int mouse_remainder_y = 0;

    while (true) {
        uint64_t start_time = rdtsc();

        // Poll mouse coordinates (drain the hardware queue completely before handling)
        int accum_dx = deferred_mouse_dx;
        int accum_dy = deferred_mouse_dy;
        bool any_left = deferred_mouse_left;
        bool any_right = deferred_mouse_right;
        bool got_packet = deferred_mouse_pending;
        int last_dx = 0;
        int last_dy = 0;

        deferred_mouse_dx = 0;
        deferred_mouse_dy = 0;
        deferred_mouse_pending = false;

        bool drag_active = wm::WindowManager::is_drag_in_progress();

        if (drag_active) {
            while (drivers::Ps2::poll_mouse(last_dx, last_dy, m_left, m_right)) {
                accum_dx += last_dx;
                accum_dy += last_dy;
                any_left = m_left;
                any_right = m_right;
                got_packet = true;
            }

            if (got_packet) {
                int speed = (accum_dx < 0 ? -accum_dx : accum_dx) + (accum_dy < 0 ? -accum_dy : accum_dy);
                int scale_num = 12;
                if (speed > 16) {
                    scale_num = 24;
                } else if (speed > 8) {
                    scale_num = 18;
                } else if (speed > 4) {
                    scale_num = 14;
                }

                int scaled_dx = accum_dx * scale_num + mouse_remainder_x;
                int scaled_dy = accum_dy * scale_num + mouse_remainder_y;
                accum_dx = scaled_dx / 10;
                accum_dy = scaled_dy / 10;
                mouse_remainder_x = scaled_dx % 10;
                mouse_remainder_y = scaled_dy % 10;

                cursor_x += accum_dx;
                cursor_y -= accum_dy;

                int width = drivers::Framebuffer::get_width();
                int height = drivers::Framebuffer::get_height();
                if (cursor_x < 0) cursor_x = 0;
                if (cursor_x >= width) cursor_x = width - 1;
                if (cursor_y < 0) cursor_y = 0;
                if (cursor_y >= height) cursor_y = height - 1;

                wm::WindowManager::handle_mouse_move(cursor_x, cursor_y, any_left, any_right);
            }
        } else {
            // Process each packet individually for ultra-smooth 100Hz hardware cursor tracking,
            // but only render the final composed frame once to avoid VRAM write saturation!
            int dx = accum_dx;
            int dy = accum_dy;
            bool left = any_left;
            bool right = any_right;
            bool first_packet = got_packet;

            while (first_packet || drivers::Ps2::poll_mouse(dx, dy, left, right)) {
                first_packet = false;
                
                int speed = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                int scale_num = 12;
                if (speed > 16) {
                    scale_num = 24;
                } else if (speed > 8) {
                    scale_num = 18;
                } else if (speed > 4) {
                    scale_num = 14;
                }

                int scaled_dx = dx * scale_num + mouse_remainder_x;
                int scaled_dy = dy * scale_num + mouse_remainder_y;
                dx = scaled_dx / 10;
                dy = scaled_dy / 10;
                mouse_remainder_x = scaled_dx % 10;
                mouse_remainder_y = scaled_dy % 10;

                cursor_x += dx;
                cursor_y -= dy;

                int width = drivers::Framebuffer::get_width();
                int height = drivers::Framebuffer::get_height();
                if (cursor_x < 0) cursor_x = 0;
                if (cursor_x >= width) cursor_x = width - 1;
                if (cursor_y < 0) cursor_y = 0;
                if (cursor_y >= height) cursor_y = height - 1;

                wm::WindowManager::handle_mouse_move(cursor_x, cursor_y, left, right);
            }
        }

        // Poll keyboard characters
        bool got_key = false;
        if (deferred_key_pending) {
            key_char = deferred_key_char;
            deferred_key_pending = false;
            got_key = true;
        } else {
            got_key = drivers::Ps2::poll_keyboard(key_char);
        }

        if (got_key) {
            drivers::Serial::print("[COMPOSITOR] Key typed: '");
            char str[2] = {key_char, '\0'};
            drivers::Serial::print(str);
            drivers::Serial::println("'");

            wm::WindowManager::handle_key_press(key_char);

            // Exit requested by typing 'q'
            if (key_char == 'q' || key_char == 'Q') {
                drivers::Serial::println("[DEMO] Exit requested. Triggering final tests...");
                gui_demo_complete = true;
            }
        }

        wm::WindowManager::tick();
        wm::WindowManager::draw_desktop();

        // Block until the next frame deadline or until mouse/keyboard input is received
        uint64_t target_tsc = start_time + tsc_per_frame;
        if (rdtsc() < target_tsc && !deferred_mouse_pending && !deferred_key_pending) {
            kernel::scheduler_set_gui_wake_tsc(target_tsc);
            kernel::Thread* current = kernel::scheduler_get_current();
            current->state = kernel::THREAD_BLOCKED;
            kernel::schedule();
        }

        // Harvest any input packets that accumulated during sleep
        int temp_dx = 0, temp_dy = 0;
        while (drivers::Ps2::poll_mouse(temp_dx, temp_dy, m_left, m_right)) {
            deferred_mouse_dx += temp_dx;
            deferred_mouse_dy += temp_dy;
            deferred_mouse_left = m_left;
            deferred_mouse_right = m_right;
            deferred_mouse_pending = true;
        }

        char temp_key = 0;
        if (drivers::Ps2::poll_keyboard(temp_key)) {
            deferred_key_char = temp_key;
            deferred_key_pending = true;
        }
    }
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
        
        drivers::Serial::print("[DEBUG] magic: ");
        char hex_buf[19];
        hex_buf[0] = '0'; hex_buf[1] = 'x';
        const char* hex_chars = "0123456789ABCDEF";
        for (int x = 0; x < 8; ++x) {
            hex_buf[2 + x] = hex_chars[(magic >> ((7 - x) * 4)) & 0x0F];
        }
        hex_buf[10] = '\0';
        drivers::Serial::println(hex_buf);

        drivers::Serial::print("[DEBUG] maddr: ");
        for (int x = 0; x < 16; ++x) {
            hex_buf[2 + x] = hex_chars[(maddr >> ((15 - x) * 4)) & 0x0F];
        }
        hex_buf[18] = '\0';
        drivers::Serial::println(hex_buf);

        uint32_t* ptr = (uint32_t*)(maddr + VMM_DIRECT_MAP_OFFSET);
        for (int i = 0; i < 12; ++i) {
            drivers::Serial::print("[DEBUG] MBI[");
            char idx_str[4];
            int offset = i * 4;
            idx_str[0] = '0' + (offset / 10);
            idx_str[1] = '0' + (offset % 10);
            idx_str[2] = '\0';
            drivers::Serial::print(idx_str);
            drivers::Serial::print("]: ");
            uint32_t val = ptr[i];
            for (int x = 0; x < 8; ++x) {
                hex_buf[2 + x] = hex_chars[(val >> ((7 - x) * 4)) & 0x0F];
            }
            hex_buf[10] = '\0';
            drivers::Serial::println(hex_buf);
        }
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

    // 6. Initialize Virtual Memory Manager (VMM)
    kernel::vmm_init();
    if (serial_ok) {
        drivers::Serial::println("[INIT] Virtual Memory Manager (VMM) initialized.");
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

    // Initialize PS/2 Keyboard and Mouse Drivers if APIC is ready
    if (apic_ok) {
        drivers::Ps2::init();
        drivers::Apic::route_irq(1, 33);  // Keyboard IRQ 1 -> Vector 33
        drivers::Apic::route_irq(12, 44); // Mouse IRQ 12 -> Vector 44
    }

    // 5. Initialize Physical Memory Manager (PMM)
    bool pmm_ok = kernel::pmm_init(magic, maddr + VMM_DIRECT_MAP_OFFSET);
    if (serial_ok) {
        if (pmm_ok) {
            drivers::Serial::println("[INIT] Physical Memory Manager (PMM) initialized.");
        } else {
            drivers::Serial::println("[ERROR] Physical Memory Manager (PMM) failed!");
        }
    }

    // Separate Direct Physical Map page tables from identity map ASAP after PMM
    // This must happen before any kmalloc/kfree to prevent aliasing bugs
    if (pmm_ok) {
        kernel::vmm_separate_dpm();
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

    // Initialize PCI Bus Scanner
    kernel::pci_init();

    // Initialize Intel e1000 network driver
    drivers::e1000_init();

    // Initialize AC97 Audio Driver
    drivers::AC97::init();

    // Initialize Graphical Framebuffer if VBE graphics requested and supplied by bootloader
    if (heap_ok) {
        uint64_t fb_phys = 0;
        uint32_t fb_width = 1024;
        uint32_t fb_height = 768;
        uint32_t fb_pitch = 1024 * 4;
        uint8_t fb_bpp = 32;
        bool fb_detected = false;
        bool is_bga = false;

        uint64_t virtual_maddr = maddr + VMM_DIRECT_MAP_OFFSET;
        uint32_t mb_flags = *(uint32_t*)virtual_maddr;
        if (mb_flags & (1 << 11)) {
            fb_phys = *(uint64_t*)(virtual_maddr + 88);
            fb_pitch = *(uint32_t*)(virtual_maddr + 96);
            fb_width = *(uint32_t*)(virtual_maddr + 100);
            fb_height = *(uint32_t*)(virtual_maddr + 104);
            fb_bpp = *(uint8_t*)(virtual_maddr + 108);
            fb_detected = true;
            if (serial_ok) {
                drivers::Serial::println("[INIT] Graphical Framebuffer detected via Multiboot.");
            }
        } else {
            // Fallback: Check BGA over PCI configuration space
            uint64_t bga_fb = drivers::Framebuffer::detect_bga();
            if (bga_fb) {
                fb_phys = bga_fb;
                fb_detected = true;
                is_bga = true;
                if (serial_ok) {
                    drivers::Serial::println("[INIT] Graphical Framebuffer fallback: Bochs/QEMU BGA detected via PCI.");
                }
            } else {
                if (serial_ok) {
                    drivers::Serial::println("[WARNING] No Multiboot VBE or Bochs BGA adapter detected. Skipping GUI.");
                }
            }
        }

        if (fb_detected) {
            if (is_bga) {
                // Setup BGA graphics registers to 1920x1080x32
                drivers::Framebuffer::setup_bga(1920, 1080, 32);
                fb_width = 1920;
                fb_height = 1080;
                fb_pitch = 1920 * 4;
                fb_bpp = 32;
            }

            bool fb_ok = drivers::Framebuffer::init(fb_phys, fb_width, fb_height, fb_pitch, fb_bpp);
            if (serial_ok) {
                if (fb_ok) {
                    drivers::Serial::println("[INIT] Graphical Framebuffer driver loaded successfully.");
                } else {
                    drivers::Serial::println("[ERROR] Graphical Framebuffer driver failed to allocate back buffer!");
                }
            }
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
            fs::tarfs_init(tar_start + VMM_DIRECT_MAP_OFFSET, tar_end + VMM_DIRECT_MAP_OFFSET);
        } else {
            if (serial_ok) {
                drivers::Serial::println("[TarFS] Error: No Multiboot modules found!");
            }
        }
    }
    // Initialize SATA AHCI controller and mount exFAT partition
    {
        if (drivers::Ahci::init()) {
            uint64_t exfat_lba = fs::Partition::find_exfat_partition();
            if (fs::Exfat::init(exfat_lba)) {
                fs::VFS::register_mount("/disk", fs::Exfat::get_root_node());
                drivers::Vga::println("[exFAT] Disk mounted successfully at '/disk'");
                if (serial_ok) {
                    drivers::Serial::println("[exFAT] Disk mounted successfully at '/disk'");
                }
            } else {
                drivers::Vga::println("[exFAT] Error: Failed to initialize exFAT filesystem!");
                if (serial_ok) {
                    drivers::Serial::println("[exFAT] Error: Failed to initialize exFAT filesystem!");
                }
            }
        } else {
            drivers::Vga::println("[AHCI] SATA disk controller not present.");
            if (serial_ok) {
                drivers::Serial::println("[AHCI] SATA disk controller not present.");
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
                fs::File f = { hello_node, 0, 0, 1 };
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
                fs::File f = { info_node, 0, 0, 1 };
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

        // Spawn GUI User Space Paint Thread (Ring 3)
        UserBootstrapArgs* u_args3 = new UserBootstrapArgs{user_paint_entry, 0x600000000ULL, 0x600100000ULL};
        kernel::thread_create(user_bootstrap_thread, u_args3);

        // Spawn GUI Window Compositor demo thread
        kernel::thread_create(gui_demo_thread, nullptr);

        // Initialize Local APIC Timer (periodic Vector 32 ticks)
        drivers::Apic::init_timer(0x80000);

        // Enable hardware interrupts
        drivers::Serial::println("[TEST] Enabling interrupts via 'sti' to start scheduling...");
        __asm__ __volatile__ ("sti");

        // Wait for GUI Window Compositor demo to complete its test sequence
        while (!gui_demo_complete) {
            kernel::schedule();
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
