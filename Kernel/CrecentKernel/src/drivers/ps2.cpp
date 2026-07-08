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
volatile int Ps2::mouse_dx = 0;
volatile int Ps2::mouse_dy = 0;
volatile bool Ps2::mouse_left_pressed = false;
volatile bool Ps2::mouse_right_pressed = false;
volatile bool Ps2::mouse_dirty = false;
volatile char Ps2::keyboard_char = 0;
volatile bool Ps2::keyboard_dirty = false;

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

bool Ps2::wait_write() {
    uint32_t timeout = 100000;
    while (timeout-- > 0) {
        if (!(inb(PS2_STATUS_PORT) & 2)) return true;
    }
    return false;
}

bool Ps2::wait_read() {
    uint32_t timeout = 100000;
    while (timeout-- > 0) {
        if (inb(PS2_STATUS_PORT) & 1) return true;
    }
    return false;
}

void Ps2::write_data(uint8_t val) {
    wait_write();
    outb(PS2_DATA_PORT, val);
}

uint8_t Ps2::read_data() {
    wait_read();
    return inb(PS2_DATA_PORT);
}

void Ps2::write_cmd(uint8_t cmd) {
    wait_write();
    outb(PS2_COMMAND_PORT, cmd);
}

void Ps2::init() {
    mouse_cycle = 0;
    mouse_dx = 0;
    mouse_dy = 0;
    mouse_left_pressed = false;
    mouse_right_pressed = false;
    mouse_dirty = false;
    keyboard_char = 0;
    keyboard_dirty = false;
    
    Serial::println("[INIT] Configuring 8042 PS/2 controller...");
    
    // 1. Enable keyboard and mouse ports
    write_cmd(0xAE); // Enable keyboard
    write_cmd(0xA8); // Enable mouse auxiliary port
    
    // 2. Read Controller Configuration Byte (CCB)
    write_cmd(0x20);
    uint8_t ccb = read_data();
    
    // Enable mouse interrupt (bit 1), keyboard interrupt (bit 0)
    // Enable clock lines (clear bits 4 and 5)
    ccb |= 0x03;
    ccb &= ~0x30;
    
    // 3. Write modified CCB back
    write_cmd(0x60);
    write_data(ccb);
    
    // 4. Reset mouse device to ensure standard reporting mode
    write_cmd(0xD4); // Redirect next write to mouse
    write_data(0xFF); // Reset Command
    uint8_t ack = read_data();
    uint8_t self_test = read_data();
    uint8_t dev_id = read_data();
    (void)ack;
    (void)self_test;
    (void)dev_id;
    
    // 5. Load default mouse configurations
    write_cmd(0xD4);
    write_data(0xF6); // Set defaults
    read_data(); // ACK
    
    // 6. Enable data streaming
    write_cmd(0xD4); // Redirect next write to auxiliary mouse device
    write_data(0xF4); // Enable mouse reporting
    
    uint8_t stream_ack = read_data();
    if (stream_ack == 0xFA) {
        Serial::println("[INIT] PS/2 Mouse data streaming enabled successfully.");
    } else {
        Serial::print("[INIT] WARNING: PS/2 Mouse streaming ACK failed: ");
        char buf[8];
        buf[0] = "0123456789ABCDEF"[(stream_ack >> 4) & 0x0F];
        buf[1] = "0123456789ABCDEF"[stream_ack & 0x0F];
        buf[2] = '\0';
        Serial::println(buf);
    }

    // 7. Flush any leftover command bytes or responses from buffers
    while (inb(PS2_STATUS_PORT) & 1) {
        inb(PS2_DATA_PORT);
    }
}

void Ps2::handle_keyboard_interrupt() {
    uint8_t scancode = inb(PS2_DATA_PORT);
    
    // Ignore key release events (which have bit 7 set, i.e. >= 0x80)
    if (!(scancode & 0x80)) {
        char c = kbd_us[scancode];
        if (c) {
            keyboard_char = c;
            keyboard_dirty = true;
            
            Serial::print("[KBD] Key down: '");
            char str[2] = {c, '\0'};
            Serial::print(str);
            Serial::println("'");
        }
    }
}

void Ps2::handle_mouse_interrupt() {
    uint8_t b = inb(PS2_DATA_PORT);
    
    if (mouse_cycle == 0) {
        // Self-Healing Sync Check: Bit 3 of the first byte must always be 1
        if (b & 0x08) {
            mouse_bytes[0] = b;
            mouse_cycle = 1;
        }
    } else if (mouse_cycle == 1) {
        mouse_bytes[1] = b;
        mouse_cycle = 2;
    } else if (mouse_cycle == 2) {
        mouse_bytes[2] = b;
        mouse_cycle = 0;
        
        // Extract deltas from bytes
        int dx = (int)mouse_bytes[1];
        int dy = (int)mouse_bytes[2];
        
        // Sign-extend X delta (Bit 4 of byte 0)
        if (mouse_bytes[0] & 0x10) {
            dx -= 256;
        }
        // Sign-extend Y delta (Bit 5 of byte 0)
        if (mouse_bytes[0] & 0x20) {
            dy -= 256;
        }
        
        // Accumulate relative movement values
        mouse_dx += dx;
        mouse_dy += dy;
        
        // Save button states
        mouse_left_pressed = (mouse_bytes[0] & 0x01);
        mouse_right_pressed = (mouse_bytes[0] & 0x02);
        mouse_dirty = true;
    }
}

bool Ps2::poll_mouse(int& dx, int& dy, bool& left, bool& right) {
    if (!mouse_dirty) return false;
    
    dx = mouse_dx;
    dy = mouse_dy;
    left = mouse_left_pressed;
    right = mouse_right_pressed;
    
    // Reset accumulators for next poll
    mouse_dx = 0;
    mouse_dy = 0;
    mouse_dirty = false;
    return true;
}

bool Ps2::poll_keyboard(char& c) {
    if (!keyboard_dirty) return false;
    
    c = keyboard_char;
    keyboard_dirty = false;
    return true;
}

void Ps2::inject_mouse_event(int dx, int dy, bool left, bool right) {
    mouse_dx += dx;
    mouse_dy += dy;
    mouse_left_pressed = left;
    mouse_right_pressed = right;
    mouse_dirty = true;
}

void Ps2::inject_keyboard_char(char c) {
    keyboard_char = c;
    keyboard_dirty = true;
}

} // namespace drivers
