#pragma once

#include "../kernel/types.hpp"

namespace drivers {

enum class VgaColor : uint8_t {
    BLACK = 0,
    BLUE = 1,
    GREEN = 2,
    CYAN = 3,
    RED = 4,
    MAGENTA = 5,
    BROWN = 6,
    LIGHT_GREY = 7,
    DARK_GREY = 8,
    LIGHT_BLUE = 9,
    LIGHT_GREEN = 10,
    LIGHT_CYAN = 11,
    LIGHT_RED = 12,
    LIGHT_MAGENTA = 13,
    LIGHT_BROWN = 14,
    WHITE = 15,
};

class Vga {
public:
    static constexpr size_t COLS = 80;
    static constexpr size_t ROWS = 25;

    // Initialize the VGA console
    static void init();

    // Clear the console screen with current or custom background color
    static void clear(VgaColor bg = VgaColor::BLACK);

    // Set text attributes (foreground and background color)
    static void set_color(VgaColor fg, VgaColor bg);

    // Write a single character at current cursor position
    static void putc(char c);

    // Print a null-terminated string
    static void print(const char* str);

    // Print a string followed by a newline
    static void println(const char* str);

private:
    // Scroll the screen up by one line if cursor exceeds ROWS
    static void scroll();

    // Update hardware cursor position on the screen
    static void update_cursor();

    static size_t cursor_x;
    static size_t cursor_y;
    static uint8_t current_color_attr;
    static volatile uint16_t* const buffer;
};

} // namespace drivers
