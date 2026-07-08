#include "framebuffer.hpp"
#include "../kernel/vmm.hpp"
#include "../kernel/heap.hpp"
#include "font.hpp"

namespace drivers {

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = ((uint32_t)1 << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFC);
    __asm__ __volatile__ ("outl %0, %1" : : "a"(address), "d"((uint16_t)0x0CF8));
    uint32_t val;
    __asm__ __volatile__ ("inl %1, %0" : "=a"(val) : "d"((uint16_t)0x0CFC));
    return val;
}

uint64_t Framebuffer::detect_bga() {
    // Scan standard PCI buses/slots for Bochs/QEMU VGA Card (Vendor 0x1234, Device 0x1111)
    for (uint32_t bus = 0; bus < 8; ++bus) {
        for (uint32_t slot = 0; slot < 32; ++slot) {
            uint32_t id = pci_read_config(bus, slot, 0, 0);
            uint16_t vendor = (uint16_t)id;
            uint16_t device = (uint16_t)(id >> 16);
            if (vendor == 0x1234 && device == 0x1111) {
                uint32_t bar0 = pci_read_config(bus, slot, 0, 0x10);
                
                // Fix 5: Check if BAR0 indicates a 64-bit memory mapping
                if ((bar0 & 0x6) == 0x4) { 
                    uint32_t bar1 = pci_read_config(bus, slot, 0, 0x14);
                    uint64_t base = ((uint64_t)bar1 << 32) | (bar0 & 0xFFFFFFF0ULL);
                    return base;
                }
                return bar0 & 0xFFFFFFF0ULL; // 32-bit Memory Mapping
            }
        }
    }
    // Fallback default for QEMU VGA LFB if PCI config scan yields no device
    return 0xFD000000ULL;
}

static void bga_write(uint16_t index, uint16_t data) {
    __asm__ __volatile__ ("outw %0, %1" : : "a"(index), "d"((uint16_t)0x01CE));
    __asm__ __volatile__ ("outw %0, %1" : : "a"(data), "d"((uint16_t)0x01CF));
}

void Framebuffer::setup_bga(uint32_t w, uint32_t h, uint32_t b) {
    bga_write(4, 0); // VBE_DISPI_INDEX_ENABLE = 4, disable BGA temporarily
    bga_write(1, w); // VBE_DISPI_INDEX_XRES = 1
    bga_write(2, h); // VBE_DISPI_INDEX_YRES = 2
    bga_write(3, b); // VBE_DISPI_INDEX_BPP = 3
    
    // Fix 4: Set Virtual Resolution and Offsets explicitly for strict VBE compliance
    bga_write(5, w); // VBE_DISPI_INDEX_VIRT_WIDTH = 5
    bga_write(6, h); // VBE_DISPI_INDEX_VIRT_HEIGHT = 6
    bga_write(7, 0); // VBE_DISPI_INDEX_X_OFFSET = 7
    bga_write(8, 0); // VBE_DISPI_INDEX_Y_OFFSET = 8
    
    bga_write(4, 0x01 | 0x40); // VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED
}


uint64_t Framebuffer::physical_base = 0;
uint32_t* Framebuffer::virtual_base = nullptr;
uint32_t* Framebuffer::back_buffer = nullptr;
uint32_t Framebuffer::width = 0;
uint32_t Framebuffer::height = 0;
uint32_t Framebuffer::pitch = 0;
uint8_t Framebuffer::bpp = 0;
bool Framebuffer::initialized = false;
Rect Framebuffer::clip_rect = {0, 0, 0, 0};
int Framebuffer::wallpaper_theme_id = 0;

bool Framebuffer::init(uint64_t phys_addr, uint32_t w, uint32_t h, uint32_t p, uint8_t b) {
    physical_base = phys_addr;
    width = w;
    height = h;
    pitch = p;
    bpp = b;

    // Fix 6: Cast to 64-bit before multiplication to prevent 32-bit overflow on large resolutions
    uint64_t size = (uint64_t)height * pitch;
    
    // 1. Map physical framebuffer memory range to the virtual window at 56GB (0xE00000000)
    virtual_base = (uint32_t*)0xE00000000ULL;

    for (uint64_t offset = 0; offset < size; offset += 4096) {
        kernel::vmm_map_page((uint64_t)virtual_base + offset, physical_base + offset, 
                             kernel::VMM_FLAG_PRESENT | kernel::VMM_FLAG_WRITABLE | kernel::VMM_FLAG_WRITE_THR);
    }

    // 2. Allocate the back buffer in the kernel slab heap
    back_buffer = (uint32_t*)kernel::kmalloc(size);
    if (!back_buffer) {
        return false;
    }

    // 3. Clear initial frame to black
    initialized = true;
    clip_rect = {0, 0, (int)width, (int)height};
    clear(0x00000000);
    swap_buffers();

    return true;
}

void Framebuffer::set_clip_rect(Rect r) {
    int x1 = r.x;
    int y1 = r.y;
    int x2 = r.x + r.w;
    int y2 = r.y + r.h;

    // Enforce hard hardware boundaries
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int)width) x2 = (int)width;
    if (y2 > (int)height) y2 = (int)height;
    
    // Prevent inversion anomalies
    if (x2 < x1) x2 = x1;
    if (y2 < y1) y2 = y1;

    clip_rect = {x1, y1, x2 - x1, y2 - y1};
}

void Framebuffer::clear_clip_rect() {
    clip_rect = {0, 0, (int)width, (int)height};
}

void Framebuffer::clear(uint32_t color) {
    if (!initialized) return;
    uint32_t size_words = (height * pitch) / 4;
    for (uint32_t i = 0; i < size_words; ++i) {
        back_buffer[i] = color;
    }
}

void Framebuffer::draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x < (uint32_t)clip_rect.x || x >= (uint32_t)(clip_rect.x + clip_rect.w) ||
        y < (uint32_t)clip_rect.y || y >= (uint32_t)(clip_rect.y + clip_rect.h)) return;
    back_buffer[y * (pitch / 4) + x] = color;
}

// Fix 7: Added clip_rect bounds checking to get_pixel
uint32_t Framebuffer::get_pixel(uint32_t x, uint32_t y) {
    if (!initialized || 
        x < (uint32_t)clip_rect.x || x >= (uint32_t)(clip_rect.x + clip_rect.w) ||
        y < (uint32_t)clip_rect.y || y >= (uint32_t)(clip_rect.y + clip_rect.h)) return 0;
    return back_buffer[y * (pitch / 4) + x];
}

void Framebuffer::draw_pixel_physical(uint32_t x, uint32_t y, uint32_t color) {
    if (!initialized || x >= width || y >= height) return;
    virtual_base[y * (pitch / 4) + x] = color;
}

void Framebuffer::draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    int start_x = (int)x;
    int start_y = (int)y;
    int end_x = start_x + (int)w;
    int end_y = start_y + (int)h;

    if (start_x < clip_rect.x) start_x = clip_rect.x;
    if (start_y < clip_rect.y) start_y = clip_rect.y;
    if (end_x > clip_rect.x + clip_rect.w) end_x = clip_rect.x + clip_rect.w;
    if (end_y > clip_rect.y + clip_rect.h) end_y = clip_rect.y + clip_rect.h;

    if (start_x >= end_x || start_y >= end_y) return;

    for (int cy = start_y; cy < end_y; ++cy) {
        uint32_t line_offset = cy * (pitch / 4);
        for (int cx = start_x; cx < end_x; ++cx) {
            back_buffer[line_offset + cx] = color;
        }
    }
}

void Framebuffer::draw_rect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint8_t alpha) {
    if (!initialized || alpha == 0) return;
    
    int start_x = (int)x;
    int start_y = (int)y;
    int end_x = start_x + (int)w;
    int end_y = start_y + (int)h;

    if (start_x < clip_rect.x) start_x = clip_rect.x;
    if (start_y < clip_rect.y) start_y = clip_rect.y;
    if (end_x > clip_rect.x + clip_rect.w) end_x = clip_rect.x + clip_rect.w;
    if (end_y > clip_rect.y + clip_rect.h) end_y = clip_rect.y + clip_rect.h;

    if (start_x >= end_x || start_y >= end_y) return;

    uint32_t pitch_words = pitch / 4;
    for (int cy = start_y; cy < end_y; ++cy) {
        uint32_t line_offset = cy * pitch_words;
        if (alpha == 255) {
            for (int cx = start_x; cx < end_x; ++cx) {
                back_buffer[line_offset + cx] = color;
            }
        } else {
            for (int cx = start_x; cx < end_x; ++cx) {
                uint32_t bg = back_buffer[line_offset + cx];
                uint32_t rb = (((color & 0xFF00FF) * alpha) + ((bg & 0xFF00FF) * (255 - alpha))) >> 8;
                uint32_t g  = (((color & 0x00FF00) * alpha) + ((bg & 0x00FF00) * (255 - alpha))) >> 8;
                back_buffer[line_offset + cx] = (rb & 0xFF00FF) | (g & 0x00FF00);
            }
        }
    }
}

void Framebuffer::draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha) {
    if (!initialized || alpha == 0) return;
    
    int start_y = y < clip_rect.y ? clip_rect.y : y;
    int end_y = (y + h) > (clip_rect.y + clip_rect.h) ? (clip_rect.y + clip_rect.h) : (y + h);
    if (start_y >= end_y) return;

    uint32_t pitch_words = pitch / 4;
    int r2 = r * r;

    for (int cy = start_y; cy < end_y; ++cy) {
        int ry = cy - y; 
        int start_x = x;
        int end_x = x + w - 1;

        // Fix 3: Gracefully handle cases where h < 2*r by taking max(dx) of overlapping corners
        if (ry < r || ry >= h - r) {
            int dx_top = -1, dx_bot = -1;
            
            if (ry < r) {
                int dy = r - ry;
                while ((dx_top + 1) * (dx_top + 1) + dy * dy <= r2) dx_top++;
            }
            if (ry >= h - r) {
                int dy = ry - (h - 1 - r);
                while ((dx_bot + 1) * (dx_bot + 1) + dy * dy <= r2) dx_bot++;
            }
            
            int dx = dx_top > dx_bot ? dx_top : dx_bot;
            int corner_w = r - dx;
            start_x += corner_w;
            end_x -= corner_w;
        }

        if (start_x < clip_rect.x) start_x = clip_rect.x;
        if (end_x >= clip_rect.x + clip_rect.w) end_x = clip_rect.x + clip_rect.w - 1;
        if (start_x > end_x) continue;

        uint32_t line_offset = cy * pitch_words;
        if (alpha == 255) {
            for (int cx = start_x; cx <= end_x; ++cx) {
                back_buffer[line_offset + cx] = color;
            }
        } else {
            for (int cx = start_x; cx <= end_x; ++cx) {
                uint32_t bg = back_buffer[line_offset + cx];
                uint32_t rb = (((color & 0xFF00FF) * alpha) + ((bg & 0xFF00FF) * (255 - alpha))) >> 8;
                uint32_t g  = (((color & 0x00FF00) * alpha) + ((bg & 0x00FF00) * (255 - alpha))) >> 8;
                back_buffer[line_offset + cx] = (rb & 0xFF00FF) | (g & 0x00FF00);
            }
        }
    }
}

void Framebuffer::draw_circle_filled(int xc, int yc, int r, uint32_t color) {
    if (!initialized) return;
    int r2 = r * r;
    int start_y = (yc - r) < clip_rect.y ? clip_rect.y : (yc - r);
    int end_y = (yc + r) > (clip_rect.y + clip_rect.h - 1) ? (clip_rect.y + clip_rect.h - 1) : (yc + r);
    
    uint32_t pitch_words = pitch / 4;
    for (int cy = start_y; cy <= end_y; ++cy) {
        int dy = cy - yc;
        int dy2 = dy * dy;
        int dx = 0;
        while (dx*dx + dy2 <= r2) dx++;
        dx--;
        
        int start_x = xc - dx;
        int end_x = xc + dx;
        
        if (start_x < clip_rect.x) start_x = clip_rect.x;
        if (end_x >= clip_rect.x + clip_rect.w) end_x = clip_rect.x + clip_rect.w - 1;
        if (start_x > end_x) continue;
        
        uint32_t line_offset = cy * pitch_words;
        for (int cx = start_x; cx <= end_x; ++cx) {
            back_buffer[line_offset + cx] = color;
        }
    }
}

void Framebuffer::draw_circle_filled_alpha(int xc, int yc, int r, uint32_t color, uint8_t alpha) {
    if (!initialized || alpha == 0) return;
    int r2 = r * r;
    int start_y = (yc - r) < clip_rect.y ? clip_rect.y : (yc - r);
    int end_y = (yc + r) > (clip_rect.y + clip_rect.h - 1) ? (clip_rect.y + clip_rect.h - 1) : (yc + r);
    
    uint32_t pitch_words = pitch / 4;
    for (int cy = start_y; cy <= end_y; ++cy) {
        int dy = cy - yc;
        int dy2 = dy * dy;
        int dx = 0;
        while (dx*dx + dy2 <= r2) dx++;
        dx--;
        
        int start_x = xc - dx;
        int end_x = xc + dx;
        
        if (start_x < clip_rect.x) start_x = clip_rect.x;
        if (end_x >= clip_rect.x + clip_rect.w) end_x = clip_rect.x + clip_rect.w - 1;
        if (start_x > end_x) continue;
        
        uint32_t line_offset = cy * pitch_words;
        if (alpha == 255) {
            for (int cx = start_x; cx <= end_x; ++cx) {
                back_buffer[line_offset + cx] = color;
            }
        } else {
            for (int cx = start_x; cx <= end_x; ++cx) {
                uint32_t bg = back_buffer[line_offset + cx];
                uint32_t rb = (((color & 0xFF00FF) * alpha) + ((bg & 0xFF00FF) * (255 - alpha))) >> 8;
                uint32_t g  = (((color & 0x00FF00) * alpha) + ((bg & 0x00FF00) * (255 - alpha))) >> 8;
                back_buffer[line_offset + cx] = (rb & 0xFF00FF) | (g & 0x00FF00);
            }
        }
    }
}

static uint32_t wallpaper_lut[3000];

void Framebuffer::draw_mac_wallpaper(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!initialized) return;
    
    int start_x = (int)x;
    int start_y = (int)y;
    int end_x = start_x + (int)w;
    int end_y = start_y + (int)h;

    if (start_x < clip_rect.x) start_x = clip_rect.x;
    if (start_y < clip_rect.y) start_y = clip_rect.y;
    if (end_x > clip_rect.x + clip_rect.w) end_x = clip_rect.x + clip_rect.w;
    if (end_y > clip_rect.y + clip_rect.h) end_y = clip_rect.y + clip_rect.h;

    if (start_x >= end_x || start_y >= end_y) return;

    uint32_t pitch_words = pitch / 4;
    
    if (wallpaper_theme_id == 3) {
        for (int cy = start_y; cy < end_y; ++cy) {
            uint32_t line_offset = cy * pitch_words;
            for (int cx = start_x; cx < end_x; ++cx) {
                back_buffer[line_offset + cx] = 0x001B1B1F;
            }
        }
        return;
    } else if (wallpaper_theme_id == 4) {
        for (int cy = start_y; cy < end_y; ++cy) {
            uint32_t line_offset = cy * pitch_words;
            for (int cx = start_x; cx < end_x; ++cx) {
                uint32_t dist_sq = cx * cx + cy * cy;
                uint32_t factor = (dist_sq * 16777ULL) >> 26;
                factor = factor & 0xFF;
                uint32_t r = ((0x5E * (255 - factor) + 0xEC * factor) * 257) >> 16;
                uint32_t g = ((0x21 * (255 - factor) + 0x48 * factor) * 257) >> 16;
                uint32_t b = ((0xD0 * (255 - factor) + 0x99 * factor) * 257) >> 16;
                back_buffer[line_offset + cx] = (r << 16) | (g << 8) | b;
            }
        }
        return;
    }

    static int last_wallpaper_theme_id = -1;
    if (wallpaper_theme_id != last_wallpaper_theme_id) {
        last_wallpaper_theme_id = wallpaper_theme_id;
        
        uint8_t r1 = 0x5E, g1 = 0x21, b1 = 0xD0; // Royal Purple
        uint8_t r2 = 0x0F, g2 = 0x52, b2 = 0xBA; // Sapphire Blue

        if (wallpaper_theme_id == 1) { // Sunset
            r1 = 0xEC; g1 = 0x48; b1 = 0x99;
            r2 = 0xF5; g2 = 0x9E; b2 = 0x0B;
        } else if (wallpaper_theme_id == 2) { // Forest
            r1 = 0x05; g1 = 0x96; b1 = 0x69;
            r2 = 0x0D; g2 = 0x94; b2 = 0x88;
        }

        uint32_t max_dist = width + height;
        if (max_dist > 3000) max_dist = 3000;
        for (uint32_t dist = 0; dist < max_dist; ++dist) {
            uint32_t factor = (dist * 255) / max_dist;
            if (factor > 255) factor = 255;
            uint32_t r = ((r1 * (255 - factor)) + (r2 * factor)) / 255;
            uint32_t g = ((g1 * (255 - factor)) + (g2 * factor)) / 255;
            uint32_t b = ((b1 * (255 - factor)) + (b2 * factor)) / 255;
            wallpaper_lut[dist] = (r << 16) | (g << 8) | b;
        }
    }

    for (int cy = start_y; cy < end_y; ++cy) {
        uint32_t line_offset = cy * pitch_words;
        for (int cx = start_x; cx < end_x; ++cx) {
            uint32_t dist = cx + cy;
            if (dist >= 3000) dist = 2999;
            back_buffer[line_offset + cx] = wallpaper_lut[dist];
        }
    }
}

void Framebuffer::set_wallpaper_theme(int theme_id) {
    if (theme_id >= 0 && theme_id <= 2) {
        wallpaper_theme_id = theme_id;
    }
}

void Framebuffer::draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0; if (dx < 0) dx = -dx;
    int dy = y1 - y0; if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    while (true) {
        draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Framebuffer::draw_char(char c, uint32_t x, uint32_t y, uint32_t color) {
    uint8_t uc = (uint8_t)c;
    if (uc > 127) return;
    const uint8_t* glyph = font_8x8[uc];
    for (int row = 0; row < 8; ++row) {
        uint8_t row_data = glyph[row];
        for (int col = 0; col < 8; ++col) {
            if (row_data & (0x80 >> col)) {
                draw_pixel(x + col, y + row, color);
            }
        }
    }
}

void Framebuffer::draw_string(const char* str, uint32_t x, uint32_t y, uint32_t color) {
    uint32_t cx = x;
    while (*str) {
        if (*str == '\n') {
            y += 8;
            cx = x;
        } else {
            draw_char(*str, cx, y, color);
            cx += 8;
        }
        str++;
    }
}

void Framebuffer::swap_buffers() {
    if (!initialized) return;
    
    // Fix 1: Use a flat 32-bit loop. The compiler with -O2 will auto-vectorize this
    // safely into 256-bit/128-bit writes, avoiding alignment traps and shearing 
    // caused by manually enforcing 64-bit blocks when pitch lacks 8-byte alignment.
    uint32_t total_words = (height * pitch) / 4;
    for (uint32_t i = 0; i < total_words; ++i) {
        virtual_base[i] = back_buffer[i];
    }
}

void Framebuffer::swap_dirty_rect(Rect r) {
    if (!initialized) return;

    int x0 = r.x; if (x0 < 0) x0 = 0;
    int y0 = r.y; if (y0 < 0) y0 = 0;
    int x1 = r.x + r.w; if (x1 > (int)width) x1 = width;
    int y1 = r.y + r.h; if (y1 > (int)height) y1 = height;
    
    if (x0 >= x1 || y0 >= y1) return;

    uint32_t pitch_words = pitch / 4;
    for (int y = y0; y < y1; ++y) {
        uint32_t offset = y * pitch_words;
        for (int x = x0; x < x1; ++x) {
            virtual_base[offset + x] = back_buffer[offset + x];
        }
    }
}

extern "C" void* memcpy(void* dest, const void* src, size_t n);

void Framebuffer::blit_buffer(int x, int y, int w, int h, const uint32_t* src_buf) {
    if (!initialized || !src_buf) return;
    int start_x = x;
    int start_y = y;
    int end_x = x + w;
    int end_y = y + h;

    if (start_x < clip_rect.x) start_x = clip_rect.x;
    if (start_y < clip_rect.y) start_y = clip_rect.y;
    if (end_x > clip_rect.x + clip_rect.w) end_x = clip_rect.x + clip_rect.w;
    if (end_y > clip_rect.y + clip_rect.h) end_y = clip_rect.y + clip_rect.h;

    if (start_x >= end_x || start_y >= end_y) return;

    uint32_t pitch_words = pitch / 4;
    uint32_t words_to_copy = end_x - start_x;
    uint32_t bytes_to_copy = words_to_copy * sizeof(uint32_t);

    for (int cy = start_y; cy < end_y; ++cy) {
        int src_y = cy - y;
        uint32_t line_offset = cy * pitch_words;
        memcpy(&back_buffer[line_offset + start_x], &src_buf[src_y * w + (start_x - x)], bytes_to_copy);
    }
}

void Framebuffer::swap_dirty_rect_fast(Rect r) {
    if (!initialized) return;

    int x1 = r.x;
    int y1 = r.y;
    int x2 = r.x + r.w;
    int y2 = r.y + r.h;

    // Enforce hard hardware boundaries
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int)width) x2 = (int)width;
    if (y2 > (int)height) y2 = (int)height;
    
    // Prevent inversion anomalies
    if (x2 < x1) x2 = x1;
    if (y2 < y1) y2 = y1;

    if (x1 >= x2 || y1 >= y2) return;

    uint32_t pitch_words = pitch / 4;
    uint32_t bytes_to_copy = (x2 - x1) * sizeof(uint32_t);
    for (int y = y1; y < y2; ++y) {
        uint32_t line_offset = y * pitch_words;
        memcpy(virtual_base + line_offset + x1, back_buffer + line_offset + x1, bytes_to_copy);
    }
}

} // namespace drivers