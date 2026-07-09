#include "serial.hpp"

namespace drivers {

bool Serial::init() {
    outb(COM1_PORT + 1, 0x00);    // Disable all interrupts
    outb(COM1_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1_PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(COM1_PORT + 1, 0x00);    //                  (hi byte)
    outb(COM1_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1_PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(COM1_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
    
    // Set in loopback mode, test the serial chip
    outb(COM1_PORT + 4, 0x1E);
    outb(COM1_PORT + 0, 0xAE);
    
    // Check if serial is faulty (i.e. if it returns the same byte)
    if (inb(COM1_PORT + 0) != 0xAE) {
        return false;
    }
    
    // If serial is not faulty, set it in normal operation mode
    // (not-loopback, IRQs enabled, OUT#1 and OUT#2 enabled)
    outb(COM1_PORT + 4, 0x0F);
    return true;
}

bool Serial::is_transmit_empty() {
    return (inb(COM1_PORT + 5) & 0x20) != 0;
}

bool Serial::is_received() {
    return (inb(COM1_PORT + 5) & 1) != 0;
}

char Serial::read_char() {
    return (char)inb(COM1_PORT);
}

void Serial::putc(char c) {
    while (!is_transmit_empty());
    outb(COM1_PORT, c);
}

void Serial::print(const char* str) {
    if (!str) return;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        if (str[i] == '\n') {
            putc('\r'); // Map newline to CR+LF for serial console compatibility
        }
        putc(str[i]);
    }
}

void Serial::println(const char* str) {
    print(str);
    print("\n");
}

} // namespace drivers
