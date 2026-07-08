#pragma once

#include "kernel/types.hpp"

namespace drivers {

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    bool intersects(const Rect& other) const {
        return x < other.x + other.w && x + w > other.x &&
               y < other.y + other.h && y + h > other.y;
    }
};

class Framebuffer {
private:
    static uint64_t physical_base;
    static uint32_t* virtual_base;
    static uint32_t* back_buffer;
    static uint32_t width;
    static uint32_t height;
    static uint32_t pitch;
    static uint8_t bpp;
    static bool initialized;

public:
    static uint64_t detect_bga();
    static void setup_bga(uint32_t w, uint32_t h, uint32_t b);
    static bool init(uint64_t phys_addr, uint32_t w, uint32_t h, uint32_t p, uint8_t b);
    static void clear(uint32_t color);
    
    // Core drawing primitives
    static void draw_pixel(uint32_t x, uint32_t y, uint32_t color);
    static uint32_t get_pixel(uint32_t x, uint32_t y);
    static void draw_pixel_physical(uint32_t x, uint32_t y, uint32_t color);
    static void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    static void draw_line(int x0, int y0, int x1, int y1, uint32_t color);
    static void draw_char(char c, uint32_t x, uint32_t y, uint32_t color);
    static void draw_string(const char* str, uint32_t x, uint32_t y, uint32_t color);
    
    // Double buffering blit routines
    static void swap_buffers();
    static void swap_dirty_rect(Rect r);
    static void swap_dirty_rect_fast(Rect r);
    
    // Dimension getters
    static uint32_t get_width() { return width; }
    static uint32_t get_height() { return height; }
    static bool is_initialized() { return initialized; }
};

} // namespace drivers
