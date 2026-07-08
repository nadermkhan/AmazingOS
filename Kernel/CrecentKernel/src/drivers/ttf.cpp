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

struct CachedGlyph {
    bool active;
    int w, h, xoff, yoff;
    int advance_width;
    uint8_t* bitmap;
};

struct FontSizeCache {
    float size;
    bool active;
    CachedGlyph glyphs[128];
};

static FontSizeCache size_caches[4] = {};

static FontSizeCache* get_or_create_size_cache(float size) {
    for (int i = 0; i < 4; ++i) {
        if (size_caches[i].active && size_caches[i].size == size) {
            return &size_caches[i];
        }
    }
    
    int slot = -1;
    for (int i = 0; i < 4; ++i) {
        if (!size_caches[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        slot = 0;
        for (int c = 0; c < 128; ++c) {
            if (size_caches[slot].glyphs[c].active && size_caches[slot].glyphs[c].bitmap) {
                kernel::kfree(size_caches[slot].glyphs[c].bitmap);
            }
        }
        size_caches[slot].active = false;
    }
    
    FontSizeCache* cache = &size_caches[slot];
    cache->size = size;
    cache->active = true;
    for (int c = 0; c < 128; ++c) {
        cache->glyphs[c].active = false;
        cache->glyphs[c].bitmap = nullptr;
    }
    
    float scale = stbtt_ScaleForPixelHeight(&font_info, size);
    for (int c = 32; c < 128; ++c) {
        int w, h, xoff, yoff;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(&font_info, scale, scale, c, &w, &h, &xoff, &yoff);
        
        int advance_width, left_side_bearing;
        stbtt_GetCodepointHMetrics(&font_info, c, &advance_width, &left_side_bearing);
        
        cache->glyphs[c].active = true;
        cache->glyphs[c].w = w;
        cache->glyphs[c].h = h;
        cache->glyphs[c].xoff = xoff;
        cache->glyphs[c].yoff = yoff;
        cache->glyphs[c].advance_width = (int)(advance_width * scale);
        
        if (bitmap && w > 0 && h > 0) {
            cache->glyphs[c].bitmap = (uint8_t*)kernel::kmalloc(w * h);
            if (cache->glyphs[c].bitmap) {
                memcpy(cache->glyphs[c].bitmap, bitmap, w * h);
            }
            stbtt_FreeBitmap(bitmap, 0);
        } else {
            cache->glyphs[c].bitmap = nullptr;
        }
    }
    
    return cache;
}

bool TtfRenderer::init() {
    fs::VFSNode* font_node = fs::VFS::open("/tar/inter.ttf");
    if (!font_node) {
        drivers::Serial::println("[TTF] Error: Failed to open /tar/inter.ttf");
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
    drivers::Serial::println("[INIT] TrueType Font Renderer initialized successfully with Inter.");
    return true;
}

void TtfRenderer::draw_char(char c, uint32_t x, uint32_t y, uint32_t color, float size) {
    if (!initialized) {
        Framebuffer::draw_char(c, x, y, color);
        return;
    }
    
    uint8_t uc = (uint8_t)c;
    if (uc > 127) return;
    
    FontSizeCache* cache = get_or_create_size_cache(size);
    if (!cache) return;
    
    CachedGlyph& glyph = cache->glyphs[uc];
    if (!glyph.active || !glyph.bitmap) return;
    
    int w = glyph.w;
    int h = glyph.h;
    int xoff = glyph.xoff;
    int yoff = glyph.yoff;
    uint8_t* bitmap = glyph.bitmap;
    
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
}

void TtfRenderer::draw_string(const char* str, uint32_t x, uint32_t y, uint32_t color, float size) {
    if (!initialized) {
        Framebuffer::draw_string(str, x, y, color);
        return;
    }
    
    FontSizeCache* cache = get_or_create_size_cache(size);
    if (!cache) return;
    
    float scale = stbtt_ScaleForPixelHeight(&font_info, size);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
    int baseline = (int)(ascent * scale);
    
    uint32_t cx = x;
    uint32_t cy = y;
    
    while (*str) {
        uint8_t uc = (uint8_t)*str;
        if (*str == '\n') {
            cy += (int)((ascent - descent + line_gap) * scale);
            cx = x;
        } else if (uc < 128) {
            CachedGlyph& glyph = cache->glyphs[uc];
            if (glyph.active) {
                if (glyph.bitmap) {
                    int w = glyph.w;
                    int h = glyph.h;
                    int xoff = glyph.xoff;
                    int yoff = glyph.yoff;
                    uint8_t* bitmap = glyph.bitmap;
                    uint32_t draw_y = cy + baseline;
                    
                    for (int cy_b = 0; cy_b < h; ++cy_b) {
                        for (int cx_b = 0; cx_b < w; ++cx_b) {
                            uint8_t alpha = bitmap[cy_b * w + cx_b];
                            if (alpha > 0) {
                                uint32_t sx = cx + xoff + cx_b;
                                uint32_t sy = draw_y + yoff + cy_b;
                                
                                if (sx < Framebuffer::get_width() && sy < Framebuffer::get_height()) {
                                    uint32_t bg = Framebuffer::get_pixel(sx, sy);
                                    uint32_t rb = ((color & 0xFF00FF) * alpha + (bg & 0xFF00FF) * (255 - alpha)) >> 8;
                                    uint32_t g  = ((color & 0x00FF00) * alpha + (bg & 0x00FF00) * (255 - alpha)) >> 8;
                                    Framebuffer::draw_pixel(sx, sy, (rb & 0xFF00FF) | (g & 0x00FF00));
                                }
                            }
                        }
                    }
                }
                cx += glyph.advance_width;
                
                if (*(str + 1)) {
                    cx += (int)(stbtt_GetCodepointKernAdvance(&font_info, *str, *(str + 1)) * scale);
                }
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
    
    FontSizeCache* cache = get_or_create_size_cache(size);
    if (!cache) return 0;
    
    float scale = stbtt_ScaleForPixelHeight(&font_info, size);
    uint32_t width = 0;
    while (*str) {
        uint8_t uc = (uint8_t)*str;
        if (*str != '\n' && uc < 128) {
            CachedGlyph& glyph = cache->glyphs[uc];
            if (glyph.active) {
                width += glyph.advance_width;
                if (*(str + 1)) {
                    width += (int)(stbtt_GetCodepointKernAdvance(&font_info, *str, *(str + 1)) * scale);
                }
            }
        }
        str++;
    }
    return width;
}

} // namespace drivers
