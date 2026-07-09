#pragma once

#include "kernel/types.hpp"

namespace drivers {

class Ps2 {
private:
    static uint8_t mouse_cycle;
    static uint8_t mouse_bytes[3];
    
    // Ring buffers and state are managed statically in ps2.cpp

    // Hardware interface helpers
    static bool wait_write();
    static bool wait_read();
    static void write_data(uint8_t val);
    static uint8_t read_data();
    static void write_cmd(uint8_t cmd);
    static void send_mouse(uint8_t cmd);
    static bool send_mouse_expect_ack(uint8_t cmd);

public:
    static void init();
    static void handle_keyboard_interrupt();
    static void handle_mouse_interrupt(uint8_t status = 0);
    
    // Test injection helpers for automated verification
    static void inject_mouse_event(int dx, int dy, bool left, bool right);
    static void inject_keyboard_char(char c);
    
    // Bottom-half polling interfaces for deferred graphics rendering
    static bool poll_mouse(int& dx, int& dy, bool& left, bool& right);
    static bool poll_keyboard(char& c);
};

} // namespace drivers
