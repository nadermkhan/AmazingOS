#include "vga.hpp"
#include "serial.hpp"

namespace drivers {

// Define and initialize static members
size_t Vga::cursor_x = 0;
size_t Vga::cursor_y = 0;
uint8_t Vga::current_color_attr = 0x07; // Light grey on black by default
volatile uint16_t* const Vga::buffer = (volatile uint16_t*)0xB8000;

void Vga::init() {
    current_color_attr = (uint8_t)(((uint8_t)VgaColor::LIGHT_GREY) | (((uint8_t)VgaColor::BLACK) << 4));
    clear();
}

void Vga::clear(VgaColor bg) {
    // Fill the screen with space character and the specified background attribute
    uint8_t clear_color = (uint8_t)(((uint8_t)VgaColor::LIGHT_GREY) | (((uint8_t)bg) << 4));
    uint16_t blank = (uint16_t)(' ' | (clear_color << 8));
    for (size_t i = 0; i < ROWS * COLS; ++i) {
        buffer[i] = blank;
    }
    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

void Vga::set_color(VgaColor fg, VgaColor bg) {
    current_color_attr = (uint8_t)(((uint8_t)fg) | (((uint8_t)bg) << 4));
}

void Vga::scroll() {
    // Copy each row starting from row 1 to the row above it
    for (size_t y = 1; y < ROWS; ++y) {
        for (size_t x = 0; x < COLS; ++x) {
            buffer[(y - 1) * COLS + x] = buffer[y * COLS + x];
        }
    }
    // Clear the bottom row with the current background/foreground style
    uint16_t blank = (uint16_t)(' ' | (current_color_attr << 8));
    for (size_t x = 0; x < COLS; ++x) {
        buffer[(ROWS - 1) * COLS + x] = blank;
    }
    cursor_y = ROWS - 1;
}

void Vga::update_cursor() {
    uint16_t pos = (uint16_t)(cursor_y * COLS + cursor_x);
    
    // Command the CRTC controller to update the hardware cursor position
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void Vga::putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        // Tab stops every 8 characters
        cursor_x = (cursor_x + 8) & ~(8 - 1);
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = COLS - 1;
        }
        buffer[cursor_y * COLS + cursor_x] = (uint16_t)(' ' | (current_color_attr << 8));
    } else {
        buffer[cursor_y * COLS + cursor_x] = (uint16_t)(c | (current_color_attr << 8));
        cursor_x++;
    }

    // Wrap around if cursor exceeds line width
    if (cursor_x >= COLS) {
        cursor_x = 0;
        cursor_y++;
    }

    // Scroll if cursor exceeds screen height
    if (cursor_y >= ROWS) {
        scroll();
    }

    update_cursor();
}

void Vga::print(const char* str) {
    if (!str) return;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        putc(str[i]);
    }
}

void Vga::println(const char* str) {
    print(str);
    putc('\n');
}

} // namespace drivers
