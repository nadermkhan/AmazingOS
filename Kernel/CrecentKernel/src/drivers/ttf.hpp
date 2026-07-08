#pragma once

#include "kernel/types.hpp"

namespace drivers {

class TtfRenderer {
private:
    static uint8_t* font_buffer;
    static bool initialized;

public:
    static bool init();
    static void draw_char(char c, uint32_t x, uint32_t y, uint32_t color, float size);
    static void draw_string(const char* str, uint32_t x, uint32_t y, uint32_t color, float size);
    static uint32_t get_string_width(const char* str, float size);
    static bool is_initialized() { return initialized; }
};

} // namespace drivers
