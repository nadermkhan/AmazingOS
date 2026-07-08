#pragma once

#include "../kernel/types.hpp"

namespace drivers {

// COM1 Base Address
constexpr uint16_t COM1_PORT = 0x3F8;

// Inline assembly helpers for port I/O
inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}

inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

class Serial {
public:
    // Initialize the COM1 serial port
    static bool init();

    // Check if the transmit buffer is empty
    static bool is_transmit_empty();

    // Write a single character to the serial port
    static void putc(char c);

    // Write a string to the serial port
    static void print(const char* str);

    // Write a string with a newline to the serial port
    static void println(const char* str);
};

} // namespace drivers
