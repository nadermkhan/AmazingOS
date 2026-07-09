#include "ps2.hpp"
#include "serial.hpp"

namespace drivers {

// Ports definition
#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64

// Static fields definition
uint8_t Ps2::mouse_cycle = 0;
uint8_t Ps2::mouse_bytes[3] = {0, 0, 0};

// SPSC Ring Buffer for Keyboard characters
#define KBD_RING_SIZE 64
static volatile char kbd_ring[KBD_RING_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;

// SPSC Ring Buffer for Mouse events
struct MouseEvent {
    int dx;
    int dy;
    bool left;
    bool right;

    MouseEvent() = default;
    MouseEvent(const volatile MouseEvent& other) {
        dx = other.dx;
        dy = other.dy;
        left = other.left;
        right = other.right;
    }
    void operator=(const MouseEvent& other) volatile {
        dx = other.dx;
        dy = other.dy;
        left = other.left;
        right = other.right;
    }
};

#define MOUSE_RING_SIZE 64
static volatile MouseEvent mouse_ring[MOUSE_RING_SIZE];
static volatile uint32_t mouse_head = 0;
static volatile uint32_t mouse_tail = 0;

// Shift state
static volatile bool shift_active = false;

// Interrupt save/restore helpers
static inline uint64_t save_interrupts() {
    uint64_t rflags;
    __asm__ __volatile__(
        "pushfq\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(rflags)
        :
        : "memory"
    );
    return rflags;
}

static inline void restore_interrupts(uint64_t rflags) {
    __asm__ __volatile__(
        "push %0\n\t"
        "popfq"
        :
        : "r"(rflags)
        : "memory"
    );
}

// Constant-time O(1) Scan-code Set 1 conversion table
static const char kbd_us[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',    /* 9 */
  '9', '0', '-', '=', '\b',    /* Backspace */
  '\t',            /* Tab */
  'q', 'w', 'e', 'r',    /* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',    /* Enter key */
    0,            /* 29 - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',    /* 39 */
  '\'', '`',   0,        /* Left shift */
  '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.',    /* 49 */
  '/',   0,            /* Right shift */
  '*',
    0,    /* Alt */
  ' ',    /* Space bar */
  0,    /* Caps lock */
  0,    /* 59 - F1 ... F10 */
  0,   0,   0,   0,   0,   0,   0,   0,
  0,    /* F10 */
  0,    /* Num lock */
  0,    /* Scroll Lock */
  0,    /* Home */
  0,    /* Up */
  0,    /* Page Up */
  '-',
  0,    /* Left */
  0,
  0,    /* Right */
  '+',
  0,    /* End */
  0,    /* Down */
  0,    /* Page Down */
  0,    /* Insert */
  0,    /* Delete */
  0,   0,   0,
  0,    /* F11 */
  0,    /* F12 */
  0
};

static const char kbd_us_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*',    /* 9 */
  '(', ')', '_', '+', '\b',    /* Backspace */
  '\t',            /* Tab */
  'Q', 'W', 'E', 'R',    /* 19 */
  'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',    /* Enter key */
    0,            /* 29 - Control */
  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',    /* 39 */
  '"', '~',   0,        /* Left shift */
  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>',    /* 49 */
  '?',   0,            /* Right shift */
  '*',
    0,    /* Alt */
  ' ',    /* Space bar */
  0,    /* Caps lock */
  0,    /* 59 - F1 ... F10 */
  0,   0,   0,   0,   0,   0,   0,   0,
  0,    /* F10 */
  0,    /* Num lock */
  0,    /* Scroll Lock */
  0,    /* Home */
  0,    /* Up */
  0,    /* Page Up */
  '-',
  0,    /* Left */
  0,
  0,    /* Right */
  '+',
  0,    /* End */
  0,    /* Down */
  0,    /* Page Down */
  0,    /* Insert */
  0,    /* Delete */
  0,   0,   0,
  0,    /* F11 */
  0,    /* F12 */
  0
};

bool Ps2::wait_write() {
    for (int i = 0; i < 100000; ++i) {
        __asm__ __volatile__ ("pause");
        if (!(inb(PS2_STATUS_PORT) & 2)) return true;
    }
    return false;
}

bool Ps2::wait_read() {
    for (int i = 0; i < 100000; ++i) {
        __asm__ __volatile__ ("pause");
        if (inb(PS2_STATUS_PORT) & 1) return true;
    }
    return false;
}

void Ps2::write_data(uint8_t val) {
    if (!wait_write()) {
        Serial::println("[PS2] Error: wait_write timeout in write_data");
        return;
    }
    outb(PS2_DATA_PORT, val);
}

uint8_t Ps2::read_data() {
    if (!wait_read()) {
        Serial::println("[PS2] Error: wait_read timeout in read_data");
        return 0;
    }
    return inb(PS2_DATA_PORT);
}

void Ps2::write_cmd(uint8_t cmd) {
    if (!wait_write()) {
        Serial::println("[PS2] Error: wait_write timeout in write_cmd");
        return;
    }
    outb(PS2_COMMAND_PORT, cmd);
}

void Ps2::send_mouse(uint8_t cmd) {
    write_cmd(0xD4);
    write_data(cmd);
}

bool Ps2::send_mouse_expect_ack(uint8_t cmd) {
    send_mouse(cmd);
    uint8_t ack = read_data();
    if (ack != 0xFA) {
        Serial::println("[PS2] Error: send_mouse command failed to receive ACK");
        return false;
    }
    return true;
}

void Ps2::init() {
    mouse_cycle = 0;
    mouse_bytes[0] = 0;
    mouse_bytes[1] = 0;
    mouse_bytes[2] = 0;
    
    // Reset ring buffer pointers
    kbd_head = 0;
    kbd_tail = 0;
    mouse_head = 0;
    mouse_tail = 0;
    shift_active = false;
    
    Serial::println("[INIT] Configuring 8042 PS/2 controller...");
    
    // 1. Enable keyboard and mouse ports
    write_cmd(0xAE); // Enable keyboard
    write_cmd(0xA8); // Enable mouse auxiliary port
    
    // 2. Read Controller Configuration Byte (CCB)
    write_cmd(0x20);
    uint8_t ccb = read_data();
    
    // Disable interrupts first to prevent partial packet delivery during config
    ccb &= ~0x03;
    ccb &= ~0x30;
    write_cmd(0x60);
    write_data(ccb);
    
    // CCB Write Read-back Verification
    write_cmd(0x20);
    uint8_t readback_ccb = read_data();
    if (readback_ccb != ccb) {
        Serial::println("[PS2] Warning: CCB configuration write verification failed!");
    }
    
    // 3. Reset mouse device to ensure standard reporting mode
    if (send_mouse_expect_ack(0xFF)) { // Reset Command
        uint8_t self_test = read_data();
        uint8_t dev_id = read_data();
        (void)self_test;
        (void)dev_id;
    } else {
        Serial::println("[PS2] Error: Mouse reset failed. Bailing early.");
        return;
    }
    
    // 4. Load default mouse configurations
    if (!send_mouse_expect_ack(0xF6)) { // Set defaults
        Serial::println("[PS2] Error: Mouse set defaults failed. Bailing early.");
        return;
    }
    
    // 5. Enable data streaming
    if (!send_mouse_expect_ack(0xF4)) { // Enable mouse reporting
        Serial::println("[PS2] Error: Mouse enable reporting failed. Bailing early.");
        return;
    }

    // 6. Flush any leftover command bytes or responses from buffers (capped at 16 iterations)
    int flush_count = 0;
    while ((inb(PS2_STATUS_PORT) & 1) && flush_count++ < 16) {
        inb(PS2_DATA_PORT);
    }

    mouse_cycle = 0;

    // 7. Finally, enable keyboard & mouse interrupts in CCB
    ccb |= 0x03;
    write_cmd(0x60);
    write_data(ccb);
    Serial::println("[INIT] PS/2 Mouse data streaming enabled successfully.");
}

void Ps2::handle_keyboard_interrupt() {
    uint8_t status = inb(PS2_STATUS_PORT);
    if (status & 0x20) {
        // Redirect auxiliary data to mouse ISR, passing already read status
        handle_mouse_interrupt(status);
        return;
    }
    
    uint8_t scancode = inb(PS2_DATA_PORT);
    
    bool is_release = (scancode & 0x80);
    uint8_t key_code = scancode & 0x7F;
    
    if (key_code == 0x2A || key_code == 0x36) { // Left Shift (0x2A) or Right Shift (0x36)
        shift_active = !is_release;
    } else if (!is_release) {
        char c = shift_active ? kbd_us_shift[key_code] : kbd_us[key_code];
        if (c) {
            uint32_t next_tail = (kbd_tail + 1) % KBD_RING_SIZE;
            if (next_tail != kbd_head) {
                kbd_ring[kbd_tail] = c;
                kbd_tail = next_tail;
            }
        }
    }
}

void Ps2::handle_mouse_interrupt(uint8_t status) {
    if (status == 0) {
        status = inb(PS2_STATUS_PORT);
    }
    if (!(status & 0x20)) {
        // Discard keyboard byte if it was incorrectly routed to mouse ISR
        inb(PS2_DATA_PORT);
        return;
    }

    uint8_t b = inb(PS2_DATA_PORT);
    
    if (mouse_cycle == 0) {
        // Self-Healing Sync Check: Bit 3 of the first byte must always be 1
        // Tighter validation: standard PS/2 mice have bits 6 and 7 as 0 on byte 0
        if ((b & 0xC8) == 0x08) {
            mouse_bytes[0] = b;
            mouse_cycle = 1;
        }
    } else if (mouse_cycle == 1) {
        mouse_bytes[1] = b;
        mouse_cycle = 2;
    } else if (mouse_cycle == 2) {
        mouse_bytes[2] = b;
        mouse_cycle = 0;
        
        int dx = (int8_t)mouse_bytes[1];
        int dy = (int8_t)mouse_bytes[2];
        bool left = (mouse_bytes[0] & 0x01);
        bool right = (mouse_bytes[0] & 0x02);
        
        // Push mouse event to the ring buffer
        uint32_t next_tail = (mouse_tail + 1) % MOUSE_RING_SIZE;
        if (next_tail != mouse_head) {
            MouseEvent ev;
            ev.dx = dx;
            ev.dy = dy;
            ev.left = left;
            ev.right = right;
            mouse_ring[mouse_tail] = ev;
            mouse_tail = next_tail;
        }
    }
}

bool Ps2::poll_mouse(int& dx, int& dy, bool& left, bool& right) {
    if (mouse_head == mouse_tail) return false;
    
    MouseEvent ev = mouse_ring[mouse_head];
    dx = ev.dx;
    dy = ev.dy;
    left = ev.left;
    right = ev.right;
    
    mouse_head = (mouse_head + 1) % MOUSE_RING_SIZE;
    return true;
}

bool Ps2::poll_keyboard(char& c) {
    if (kbd_head == kbd_tail) return false;
    
    c = kbd_ring[kbd_head];
    kbd_head = (kbd_head + 1) % KBD_RING_SIZE;
    return true;
}

void Ps2::inject_mouse_event(int dx, int dy, bool left, bool right) {
    uint64_t rflags = save_interrupts();
    uint32_t next_tail = (mouse_tail + 1) % MOUSE_RING_SIZE;
    if (next_tail != mouse_head) {
        MouseEvent ev;
        ev.dx = dx;
        ev.dy = dy;
        ev.left = left;
        ev.right = right;
        mouse_ring[mouse_tail] = ev;
        mouse_tail = next_tail;
    }
    restore_interrupts(rflags);
}

void Ps2::inject_keyboard_char(char c) {
    uint64_t rflags = save_interrupts();
    uint32_t next_tail = (kbd_tail + 1) % KBD_RING_SIZE;
    if (next_tail != kbd_head) {
        kbd_ring[kbd_tail] = c;
        kbd_tail = next_tail;
    }
    restore_interrupts(rflags);
}

} // namespace drivers
