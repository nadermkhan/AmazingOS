#include "ttf.hpp"
#include "fs/vfs.hpp"
#include "kernel/heap.hpp"
#include "drivers/serial.hpp"
#include "drivers/framebuffer.hpp"

#ifndef NULL
#define NULL 0
#endif

extern "C" {
    double floor(double x);
    double ceil(double x);
    double sqrt(double x);
    double pow(double base, double exp);
    double fabs(double x);
    double fmod(double x, double y);
    double cos(double x);
    double acos(double x);
    void* memcpy(void* dest, const void* src, size_t n);
    void* memset(void* s, int c, size_t n);
    size_t strlen(const char* s);
}

#define STBTT_ifloor(x)    ((int)floor(x))
#define STBTT_iceil(x)     ((int)ceil(x))
#define STBTT_sqrt(x)      sqrt(x)
#define STBTT_pow(x,y)     pow(x,y)
#define STBTT_fmod(x,y)    fmod(x,y)
#define STBTT_cos(x)       cos(x)
#define STBTT_acos(x)      acos(x)
#define STBTT_fabs(x)      fabs(x)
#define STBTT_malloc(x,u)  kernel::kmalloc(x)
#define STBTT_free(x,u)    kernel::kfree(x)
#define STBTT_assert(x)
#define STBTT_strlen(x)    strlen(x)
#define STBTT_memcpy       memcpy
#define STBTT_memset       memset

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace drivers {

uint8_t* TtfRenderer::font_buffer = nullptr;
bool TtfRenderer::initialized = false;
static stbtt_fontinfo font_info;

bool TtfRenderer::init() {
    fs::VFSNode* font_node = fs::VFS::open("/tar/arial.ttf");
    if (!font_node) {
        drivers::Serial::println("[TTF] Error: Failed to open /tar/arial.ttf");
        return false;
    }
    
    uint64_t size = font_node->size;
    font_buffer = (uint8_t*)kernel::kmalloc(size);
    if (!font_buffer) {
        drivers::Serial::println("[TTF] Error: Failed to allocate font buffer memory");
        return false;
    }
    
    fs::File f = { font_node, 0, 0 };
    ssize_t read_bytes = fs::VFS::read(&f, font_buffer, size);
    if (read_bytes != (ssize_t)size) {
        drivers::Serial::println("[TTF] Error: Failed to read font file completely");
        kernel::kfree(font_buffer);
        font_buffer = nullptr;
        return false;
    }
    
    if (!stbtt_InitFont(&font_info, font_buffer, stbtt_GetFontOffsetForIndex(font_buffer, 0))) {
        drivers::Serial::println("[TTF] Error: stbtt_InitFont failed");
        kernel::kfree(font_buffer);
        font_buffer = nullptr;
        return false;
    }
    
    initialized = true;
    drivers::Serial::println("[INIT] TrueType Font Renderer initialized successfully with Arial.");
    return true;
}

void TtfRenderer::draw_char(char c, uint32_t x, uint32_t y, uint32_t color, float size) {
    if (!initialized) {
        Framebuffer::draw_char(c, x, y, color);
        return;
    }
    
    float scale = stbtt_ScaleForPixelHeight(&font_info, size);
    int w, h, xoff, yoff;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(&font_info, scale, scale, c, &w, &h, &xoff, &yoff);
    if (!bitmap) return;
    
    for (int cy = 0; cy < h; ++cy) {
        for (int cx = 0; cx < w; ++cx) {
            uint8_t alpha = bitmap[cy * w + cx];
            if (alpha > 0) {
                uint32_t sx = x + xoff + cx;
                uint32_t sy = y + yoff + cy;
                
                if (sx < Framebuffer::get_width() && sy < Framebuffer::get_height()) {
                    uint32_t bg = Framebuffer::get_pixel(sx, sy);
                    uint32_t rb = ((color & 0xFF00FF) * alpha + (bg & 0xFF00FF) * (255 - alpha)) >> 8;
                    uint32_t g  = ((color & 0x00FF00) * alpha + (bg & 0x00FF00) * (255 - alpha)) >> 8;
                    Framebuffer::draw_pixel(sx, sy, (rb & 0xFF00FF) | (g & 0x00FF00));
                }
            }
        }
    }
    
    stbtt_FreeBitmap(bitmap, 0);
}

void TtfRenderer::draw_string(const char* str, uint32_t x, uint32_t y, uint32_t color, float size) {
    if (!initialized) {
        Framebuffer::draw_string(str, x, y, color);
        return;
    }
    
    float scale = stbtt_ScaleForPixelHeight(&font_info, size);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
    
    int baseline = (int)(ascent * scale);
    
    uint32_t cx = x;
    uint32_t cy = y;
    
    while (*str) {
        if (*str == '\n') {
            cy += (int)((ascent - descent + line_gap) * scale);
            cx = x;
        } else {
            int advance_width, left_side_bearing;
            stbtt_GetCodepointHMetrics(&font_info, *str, &advance_width, &left_side_bearing);
            
            draw_char(*str, cx, cy + baseline, color, size);
            cx += (int)(advance_width * scale);
            
            if (*(str + 1)) {
                cx += (int)(stbtt_GetCodepointKernAdvance(&font_info, *str, *(str + 1)) * scale);
            }
        }
        str++;
    }
}

uint32_t TtfRenderer::get_string_width(const char* str, float size) {
    if (!initialized) {
        uint32_t len = 0;
        while (str[len]) len++;
        return len * 8;
    }
    
    float scale = stbtt_ScaleForPixelHeight(&font_info, size);
    uint32_t width = 0;
    while (*str) {
        if (*str != '\n') {
            int advance_width, left_side_bearing;
            stbtt_GetCodepointHMetrics(&font_info, *str, &advance_width, &left_side_bearing);
            width += (int)(advance_width * scale);
            
            if (*(str + 1)) {
                width += (int)(stbtt_GetCodepointKernAdvance(&font_info, *str, *(str + 1)) * scale);
            }
        }
        str++;
    }
    return width;
}

} // namespace drivers
