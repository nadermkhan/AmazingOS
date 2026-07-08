#pragma once

#include "kernel/types.hpp"

namespace drivers {

class Ps2 {
private:
    static uint8_t mouse_cycle;
    static uint8_t mouse_bytes[3];
    
    // Top-Half atomic deltas and click states
    static volatile int mouse_dx;
    static volatile int mouse_dy;
    static volatile bool mouse_left_pressed;
    static volatile bool mouse_right_pressed;
    static volatile bool mouse_dirty;

    // Keyboard buffer
    static volatile char keyboard_char;
    static volatile bool keyboard_dirty;

    // Hardware interface helpers
    static bool wait_write();
    static bool wait_read();
    static void write_data(uint8_t val);
    static uint8_t read_data();
    static void write_cmd(uint8_t cmd);

public:
    static void init();
    static void handle_keyboard_interrupt();
    static void handle_mouse_interrupt();
    
    // Test injection helpers for automated verification
    static void inject_mouse_event(int dx, int dy, bool left, bool right);
    static void inject_keyboard_char(char c);
    
    // Bottom-half polling interfaces for deferred graphics rendering
    static bool poll_mouse(int& dx, int& dy, bool& left, bool& right);
    static bool poll_keyboard(char& c);
};

} // namespace drivers
