#include "framebuffer.hpp"
#include "../kernel/vmm.hpp"
#include "../kernel/heap.hpp"
#include "font.hpp"
#include "serial.hpp"

extern "C" void* memcpy(void* dest, const void* src, size_t n);

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
    for (uint32_t bus = 0; bus < 8; ++bus) {
        for (uint32_t slot = 0; slot < 32; ++slot) {
            uint32_t id = pci_read_config(bus, slot, 0, 0);
            uint16_t vendor = (uint16_t)id;
            uint16_t device = (uint16_t)(id >> 16);
            if (vendor == 0x1234 && device == 0x1111) {
                uint32_t bar0 = pci_read_config(bus, slot, 0, 0x10);
                if ((bar0 & 0x6) == 0x4) { 
                    uint32_t bar1 = pci_read_config(bus, slot, 0, 0x14);
                    uint64_t base = ((uint64_t)bar1 << 32) | (bar0 & 0xFFFFFFF0ULL);
                    return base;
                }
                return bar0 & 0xFFFFFFF0ULL;
            }
        }
    }
    return 0xFD000000ULL;
}

static int current_page = 0;

static void bga_write(uint16_t index, uint16_t data) {
    __asm__ __volatile__ ("outw %0, %1" : : "a"(index), "d"((uint16_t)0x01CE));
    __asm__ __volatile__ ("outw %0, %1" : : "a"(data), "d"((uint16_t)0x01CF));
}

void Framebuffer::setup_bga(uint32_t w, uint32_t h, uint32_t b) {
    bga_write(4, 0);      // Disable VBE extensions
    bga_write(1, w);      // XRES
    bga_write(2, h);      // YRES
    bga_write(3, b);      // BPP
    bga_write(5, 0);      // BANK (set to 0)
    bga_write(6, w);      // VIRT_WIDTH
    bga_write(7, h * 2);  // VIRT_HEIGHT (doubled for hardware double-buffering)
    bga_write(8, 0);      // X_OFFSET
    bga_write(9, 0);      // Y_OFFSET
    bga_write(4, 0x01 | 0x40); // Re-enable extensions with LFB enabled
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
uint32_t* Framebuffer::wallpaper_cache = nullptr;

bool Framebuffer::init(uint64_t phys_addr, uint32_t w, uint32_t h, uint32_t p, uint8_t b) {
    physical_base = phys_addr;
    width = w;
    height = h;
    pitch = p;
    bpp = b;

    uint64_t size = (uint64_t)height * pitch;
    
    virtual_base = (uint32_t*)0xE00000000ULL;

    // Configure IA32_PAT MSR (0x277) to set PAT2 to Write-Combining (1)
    // Default PAT: PAT0=WB(6), PAT1=WT(4), PAT2=UC-(7), PAT3=UC(0)
    // We modify PAT2 to WC (1) so that PCD=1, PWT=0 maps to Write-Combining memory.
    {
        uint32_t low, high;
        __asm__ __volatile__ ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x277));
        uint64_t pat = ((uint64_t)high << 32) | low;
        pat &= ~(0x7ULL << 16); // Clear PAT2 (bits 16-18)
        pat |= (0x1ULL << 16);  // Set PAT2 to Write-Combining (1)
        low = (uint32_t)pat;
        high = (uint32_t)(pat >> 32);
        __asm__ __volatile__ ("wrmsr" : : "c"(0x277), "a"(low), "d"(high));
    }

    for (uint64_t offset = 0; offset < size * 2; offset += 4096) {
        kernel::vmm_map_page((uint64_t)virtual_base + offset, physical_base + offset, 
                             kernel::VMM_FLAG_PRESENT | kernel::VMM_FLAG_WRITABLE | kernel::VMM_FLAG_CACHE_DIS);
    }

    back_buffer = (uint32_t*)kernel::kmalloc(size);
    if (!back_buffer) return false;

    wallpaper_cache = (uint32_t*)kernel::kmalloc(size);
    if (!wallpaper_cache) {
        kernel::kfree(back_buffer);
        back_buffer = nullptr;
        return false;
    }

    initialized = true;
    clip_rect = {0, 0, (int)width, (int)height};
    
    update_wallpaper_cache();
    clear(0x00000000);
    swap_buffers();

    return true;
}

void Framebuffer::set_clip_rect(Rect r) {
    int x1 = r.x;
    int y1 = r.y;
    int x2 = r.x + r.w;
    int y2 = r.y + r.h;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int)width) x2 = (int)width;
    if (y2 > (int)height) y2 = (int)height;
    
    if (x2 < x1) x2 = x1;
    if (y2 < y1) y2 = y1;

    clip_rect = {x1, y1, x2 - x1, y2 - y1};
}

void Framebuffer::clear_clip_rect() {
    clip_rect = {0, 0, (int)width, (int)height};
}

void Framebuffer::clear(uint32_t color) {
    if (!initialized) return;
    uint64_t size_words = ((uint64_t)height * pitch) / 4;
    for (uint64_t i = 0; i < size_words; ++i) {
        back_buffer[i] = color;
    }
}

void Framebuffer::draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x < (uint32_t)clip_rect.x || x >= (uint32_t)(clip_rect.x + clip_rect.w) ||
        y < (uint32_t)clip_rect.y || y >= (uint32_t)(clip_rect.y + clip_rect.h)) return;
    back_buffer[y * (pitch / 4) + x] = color;
}

uint32_t Framebuffer::get_pixel(uint32_t x, uint32_t y) {
    if (!initialized || 
        x < (uint32_t)clip_rect.x || x >= (uint32_t)(clip_rect.x + clip_rect.w) ||
        y < (uint32_t)clip_rect.y || y >= (uint32_t)(clip_rect.y + clip_rect.h)) return 0;
    return back_buffer[y * (pitch / 4) + x];
}

uint32_t Framebuffer::get_pixel_unclipped(uint32_t x, uint32_t y) {
    if (!initialized || x >= width || y >= height) return 0;
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
    const uint32_t src_rb = (color & 0xFF00FF) * alpha;
    const uint32_t src_g  = (color & 0x00FF00) * alpha;
    const uint32_t inv    = 255 - alpha;

    for (int cy = start_y; cy < end_y; ++cy) {
        uint32_t line_offset = cy * pitch_words;
        if (alpha == 255) {
            for (int cx = start_x; cx < end_x; ++cx) {
                back_buffer[line_offset + cx] = color;
            }
        } else {
            for (int cx = start_x; cx < end_x; ++cx) {
                uint32_t bg = back_buffer[line_offset + cx];
                uint32_t rb = (src_rb + (bg & 0xFF00FF) * inv) >> 8;
                uint32_t g  = (src_g  + (bg & 0x00FF00) * inv) >> 8;
                back_buffer[line_offset + cx] = (rb & 0xFF00FF) | (g & 0x00FF00);
            }
        }
    }
}

extern "C" float sqrtf(float x);

void Framebuffer::draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha) {
    if (!initialized || alpha == 0) return;
    
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    int start_y = y < clip_rect.y ? clip_rect.y : y;
    int end_y = (y + h) > (clip_rect.y + clip_rect.h) ? (clip_rect.y + clip_rect.h) : (y + h);
    if (start_y >= end_y) return;
    
    int start_x_clip = x < clip_rect.x ? clip_rect.x : x;
    int end_x_clip = (x + w) > (clip_rect.x + clip_rect.w) ? (clip_rect.x + clip_rect.w) : (x + w);
    if (start_x_clip >= end_x_clip) return;

    uint32_t pitch_words = pitch / 4;
    const uint32_t src_rb = (color & 0xFF00FF);
    const uint32_t src_g  = (color & 0x00FF00);

    for (int cy = start_y; cy < end_y; ++cy) {
        uint32_t line_offset = cy * pitch_words;
        
        for (int cx = start_x_clip; cx < end_x_clip; ++cx) {
            bool draw = true;
            uint8_t pixel_alpha = alpha;
            
            // Check if we are in one of the 4 corners
            if (r > 0) {
                int dx = 0, dy = 0;
                if (cx < x + r) dx = (x + r) - cx;
                else if (cx >= x + w - r) dx = cx - (x + w - 1 - r);
                
                if (cy < y + r) dy = (y + r) - cy;
                else if (cy >= y + h - r) dy = cy - (y + h - 1 - r);
                
                if (dx > 0 && dy > 0) {
                    int dist_sq = dx * dx + dy * dy;
                    int r_sq = r * r;
                    if (dist_sq > r_sq) {
                        draw = false;
                    } else if (dist_sq > (r - 1) * (r - 1)) {
                        int diff = dist_sq - (r - 1) * (r - 1);
                        int denom = 2 * r - 1;
                        int coverage = 256 - (diff * 256) / denom;
                        if (coverage < 0) coverage = 0;
                        if (coverage > 256) coverage = 256;
                        pixel_alpha = (uint8_t)((alpha * coverage) >> 8);
                    }
                }
            }
            
            if (!draw) continue;
            
            if (pixel_alpha == 255) {
                back_buffer[line_offset + cx] = color;
            } else if (pixel_alpha > 0) {
                uint32_t bg = back_buffer[line_offset + cx];
                uint32_t inv = 255 - pixel_alpha;
                uint32_t s_rb = src_rb * pixel_alpha;
                uint32_t s_g = src_g * pixel_alpha;
                uint32_t rb = (s_rb + (bg & 0xFF00FF) * inv) >> 8;
                uint32_t g  = (s_g  + (bg & 0x00FF00) * inv) >> 8;
                back_buffer[line_offset + cx] = (rb & 0xFF00FF) | (g & 0x00FF00);
            }
        }
    }
}

void Framebuffer::draw_circle_filled(int xc, int yc, int r, uint32_t color) {
    if (!initialized || r < 0) return;
    if (r == 0) {
        draw_pixel(xc, yc, color);
        return;
    }

    int x = r, y = 0;
    int err = 1 - r;

    while (x >= y) {
        draw_rect(xc - x, yc + y, 2 * x, 1, color);
        draw_rect(xc - y, yc + x, 2 * y, 1, color);
        draw_rect(xc - x, yc - y, 2 * x, 1, color);
        draw_rect(xc - y, yc - x, 2 * y, 1, color);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void Framebuffer::draw_circle_filled_alpha(int xc, int yc, int r, uint32_t color, uint8_t alpha) {
    if (!initialized || alpha == 0 || r < 0) return;
    if (r == 0) {
        draw_pixel(xc, yc, color);
        return;
    }

    int x = r, y = 0;
    int err = 1 - r;

    while (x >= y) {
        draw_rect_alpha(xc - x, yc + y, 2 * x, 1, color, alpha);
        draw_rect_alpha(xc - y, yc + x, 2 * y, 1, color, alpha);
        draw_rect_alpha(xc - x, yc - y, 2 * x, 1, color, alpha);
        draw_rect_alpha(xc - y, yc - x, 2 * y, 1, color, alpha);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static uint32_t wallpaper_lut[3000];

static float fast_sin(float x) {
    float pi = 3.14159265f;
    float two_pi = 6.2831853f;
    if (x > 100.0f || x < -100.0f) {
        float q = x * 0.15915494f; // x / two_pi
        int qi = (int)q;
        x = x - (float)qi * two_pi;
    }
    while (x > pi) x -= two_pi;
    while (x < -pi) x += two_pi;
    float x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * 0.00019841f)));
}

void Framebuffer::generate_wallpaper_procedural(uint32_t* dst) {
    uint32_t pitch_words = pitch / 4;

    if (wallpaper_theme_id == 0) {
        // Windows XP "Bliss" HD Procedural Wallpaper
        
        // 1. Precalculate hill heights per column
        static float back_hill_y[2048];
        static float front_hill_y[2048];
        uint32_t max_w = width < 2048 ? width : 2048;
        for (uint32_t cx = 0; cx < max_w; ++cx) {
            float x_norm = (float)cx / width;
            back_hill_y[cx] = height * 0.65f + height * 0.08f * fast_sin(x_norm * 4.2f + 1.5f);
            front_hill_y[cx] = height * 0.72f + height * 0.09f * fast_sin(x_norm * 3.1f - 0.5f);
        }

        // 2. Predefine cloud blobs with precalculated inverse squared radius
        struct CloudBlob {
            float x, y, inv_r2;
        };
        CloudBlob clouds[] = {
            { 300.0f, 150.0f, 1.0f / (90.0f * 90.0f) },
            { 380.0f, 170.0f, 1.0f / (120.0f * 120.0f) },
            { 470.0f, 160.0f, 1.0f / (100.0f * 100.0f) },
            { 1200.0f, 180.0f, 1.0f / (140.0f * 140.0f) },
            { 1320.0f, 200.0f, 1.0f / (160.0f * 160.0f) },
            { 1450.0f, 170.0f, 1.0f / (110.0f * 110.0f) }
        };

        for (uint32_t cy = 0; cy < height; ++cy) {
            uint32_t line_offset = cy * pitch_words;
            float y_norm = (float)cy / height;

            // Sky gradient: deep blue at top -> light sky blue near horizon
            uint8_t sky_r = (uint8_t)((1.0f - y_norm) * 0x0A + y_norm * 0x7E); // 10 -> 126
            uint8_t sky_g = (uint8_t)((1.0f - y_norm) * 0x48 + y_norm * 0xB8); // 72 -> 184
            uint8_t sky_b = (uint8_t)((1.0f - y_norm) * 0xC2 + y_norm * 0xFB); // 194 -> 251

            for (uint32_t cx = 0; cx < width; ++cx) {
                float x_norm = (float)cx / width;

                uint8_t r = sky_r;
                uint8_t g = sky_g;
                uint8_t b = sky_b;

                // Procedural fluffy white clouds in the sky using fast circular blobs
                float cloud_val = 0.0f;
                if (cy < height * 0.55f) {
                    for (int i = 0; i < 6; ++i) {
                        float dx = (float)cx - clouds[i].x;
                        float dy = (float)cy - clouds[i].y;
                        float dist2 = dx * dx + dy * dy;
                        float inv_r2 = clouds[i].inv_r2;
                        float ratio = dist2 * inv_r2;
                        if (ratio < 1.0f) {
                            float f = 1.0f - ratio;
                            cloud_val += f * f * 0.6f;
                        }
                    }
                    if (cloud_val > 1.0f) cloud_val = 1.0f;
                }
                
                r = (uint8_t)(r * (1.0f - cloud_val) + 255 * cloud_val);
                g = (uint8_t)(g * (1.0f - cloud_val) + 255 * cloud_val);
                b = (uint8_t)(b * (1.0f - cloud_val) + 248 * cloud_val); // slightly warm white

                // Get precalculated hill heights
                float b_hill = back_hill_y[cx < 2048 ? cx : 2047];
                float f_hill = front_hill_y[cx < 2048 ? cx : 2047];

                if ((float)cy >= f_hill) {
                    // Front hill: vibrant green with sunshine highlight at peak
                    float depth = ((float)cy - f_hill) / (height - f_hill + 1);
                    float sun_factor = fast_sin(x_norm * 3.1f - 0.5f);
                    if (sun_factor < 0) sun_factor = 0;

                    uint8_t hill_r = (uint8_t)((1.0f - depth) * (38 + sun_factor * 18) + depth * 15);
                    uint8_t hill_g = (uint8_t)((1.0f - depth) * (162 + sun_factor * 33) + depth * 85);
                    uint8_t hill_b = (uint8_t)((1.0f - depth) * (38 + sun_factor * 12) + depth * 15);

                    r = hill_r; g = hill_g; b = hill_b;
                } else if ((float)cy >= b_hill) {
                    // Back hill: darker green/teal for depth atmospheric perspective
                    float depth = ((float)cy - b_hill) / (height - b_hill + 1);
                    float sun_factor = fast_sin(x_norm * 4.2f + 1.5f);
                    if (sun_factor < 0) sun_factor = 0;

                    uint8_t hill_r = (uint8_t)((1.0f - depth) * (24 + sun_factor * 12) + depth * 10);
                    uint8_t hill_g = (uint8_t)((1.0f - depth) * (118 + sun_factor * 22) + depth * 65);
                    uint8_t hill_b = (uint8_t)((1.0f - depth) * (45 + sun_factor * 8) + depth * 25);

                    r = hill_r; g = hill_g; b = hill_b;
                }

                dst[line_offset + cx] = (r << 16) | (g << 8) | b;
            }
        }
    }

    if (wallpaper_theme_id == 3) {
        for (uint32_t cy = 0; cy < height; ++cy) {
            uint32_t line_offset = cy * pitch_words;
            for (uint32_t cx = 0; cx < width; ++cx) {
                dst[line_offset + cx] = 0x001B1B1F;
            }
        }
        return;
    } else if (wallpaper_theme_id == 4) {
        for (uint32_t cy = 0; cy < height; ++cy) {
            uint32_t line_offset = cy * pitch_words;
            uint32_t cy2 = cy * cy;
            uint32_t cx2 = 0;
            for (uint32_t cx = 0; cx < width; ++cx) {
                uint32_t dist_sq = cx2 + cy2;
                uint32_t factor = (dist_sq * 16777ULL) >> 26;
                factor = factor & 0xFF;
                uint32_t r = ((0x5E * (255 - factor) + 0xEC * factor) * 257) >> 16;
                uint32_t g = ((0x21 * (255 - factor) + 0x48 * factor) * 257) >> 16;
                uint32_t b = ((0xD0 * (255 - factor) + 0x99 * factor) * 257) >> 16;
                dst[line_offset + cx] = (r << 16) | (g << 8) | b;
                cx2 += 2 * cx + 1;
            }
        }
        return;
    }

    uint8_t r1 = 0x5E, g1 = 0x21, b1 = 0xD0;
    uint8_t r2 = 0x0F, g2 = 0x52, b2 = 0xBA;

    if (wallpaper_theme_id == 1) { 
        r1 = 0xEC; g1 = 0x48; b1 = 0x99;
        r2 = 0xF5; g2 = 0x9E; b2 = 0x0B;
    } else if (wallpaper_theme_id == 2) { 
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

    for (uint32_t cy = 0; cy < height; ++cy) {
        uint32_t line_offset = cy * pitch_words;
        for (uint32_t cx = 0; cx < width; ++cx) {
            uint32_t dist = cx + cy;
            if (dist >= 3000) dist = 2999;
            dst[line_offset + cx] = wallpaper_lut[dist];
        }
    }
}

void Framebuffer::update_wallpaper_cache() {
    if (!initialized || !wallpaper_cache) return;
    
    Rect old_clip = clip_rect;
    clip_rect = {0, 0, (int)width, (int)height};
    
    generate_wallpaper_procedural(wallpaper_cache);
    
    clip_rect = old_clip;
}

void Framebuffer::draw_mac_wallpaper(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!initialized || !wallpaper_cache) return;
    
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
    uint64_t bytes_to_copy = (uint64_t)(end_x - start_x) * sizeof(uint32_t);

    for (int cy = start_y; cy < end_y; ++cy) {
        uint64_t line_offset = (uint64_t)cy * pitch_words;
        memcpy(&back_buffer[line_offset + start_x], 
               &wallpaper_cache[line_offset + start_x], 
               bytes_to_copy);
    }
}

void Framebuffer::set_wallpaper_theme(int theme_id) {
    if (theme_id >= 0 && theme_id <= 4) {
        wallpaper_theme_id = theme_id;
        update_wallpaper_cache();
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
    uint32_t pitch_words = pitch / 4;
    
    for (int row = 0; row < 8; ++row) {
        int py = (int)y + row;
        if (py < clip_rect.y || py >= clip_rect.y + clip_rect.h) continue;
        
        uint8_t row_data = glyph[row];
        uint32_t line_off = py * pitch_words;
        for (int col = 0; col < 8; ++col) {
            int px = (int)x + col;
            if (px < clip_rect.x || px >= clip_rect.x + clip_rect.w) continue;
            
            if (row_data & (0x80 >> col)) {
                back_buffer[line_off + px] = color;
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
    uint64_t total_bytes = (uint64_t)height * pitch;
    int back_page = current_page ^ 1;
    
    // 1. Copy full screen to the hidden back page in VRAM
    uint32_t* dest_back = (uint32_t*)((uint64_t)virtual_base + back_page * total_bytes);
    memcpy(dest_back, back_buffer, total_bytes);
    
    // 2. Perform the hardware page flip
    bga_write(9, back_page * height);
    
    // 3. Copy full screen to the old visible page (new back page) to keep them in sync
    uint32_t* dest_front = (uint32_t*)((uint64_t)virtual_base + current_page * total_bytes);
    memcpy(dest_front, back_buffer, total_bytes);
    
    current_page = back_page;
}

void Framebuffer::swap_dirty_rect(Rect r) {
    if (!initialized) return;

    // Clip rectangle to strictly fit hardware bounds
    int start_x = r.x < 0 ? 0 : r.x;
    int start_y = r.y < 0 ? 0 : r.y;
    int end_x = r.x + r.w > (int)width ? (int)width : r.x + r.w;
    int end_y = r.y + r.h > (int)height ? (int)height : r.y + r.h;

    if (start_x >= end_x || start_y >= end_y) return;

    uint32_t pitch_words = pitch / 4;
    uint32_t bytes_per_line = (end_x - start_x) * sizeof(uint32_t);
    uint64_t total_bytes = (uint64_t)height * pitch;

    int back_page = current_page ^ 1;
    uint32_t* back_buffer_vram = (uint32_t*)((uint64_t)virtual_base + back_page * total_bytes);

    // Transfer exclusively the damaged geometry to the hidden page in VRAM
    for (int cy = start_y; cy < end_y; ++cy) {
        uint64_t offset = (uint64_t)cy * pitch_words + start_x;
        memcpy(&back_buffer_vram[offset], &back_buffer[offset], bytes_per_line);
    }

    // Toggle the visible page
    bga_write(9, back_page * height);

    // To keep the new back page in sync, also copy the dirty rect to it
    uint32_t* front_buffer_vram = (uint32_t*)((uint64_t)virtual_base + current_page * total_bytes);
    for (int cy = start_y; cy < end_y; ++cy) {
        uint64_t offset = (uint64_t)cy * pitch_words + start_x;
        memcpy(&front_buffer_vram[offset], &back_buffer[offset], bytes_per_line);
    }

    current_page = back_page;
}

void Framebuffer::swap_dirty_rects(const Rect* rects, int count) {
    if (!initialized || count <= 0 || !rects) return;

    uint32_t pitch_words = pitch / 4;
    uint64_t total_bytes = (uint64_t)height * pitch;

    int back_page = current_page ^ 1;
    uint32_t* back_buffer_vram = (uint32_t*)((uint64_t)virtual_base + back_page * total_bytes);

    // 1. Transfer all dirty regions to the hidden page in VRAM
    for (int i = 0; i < count; ++i) {
        Rect r = rects[i];
        int start_x = r.x < 0 ? 0 : r.x;
        int start_y = r.y < 0 ? 0 : r.y;
        int end_x = r.x + r.w > (int)width ? (int)width : r.x + r.w;
        int end_y = r.y + r.h > (int)height ? (int)height : r.y + r.h;
        if (start_x >= end_x || start_y >= end_y) continue;

        uint32_t bytes_per_line = (end_x - start_x) * sizeof(uint32_t);
        for (int cy = start_y; cy < end_y; ++cy) {
            uint64_t offset = (uint64_t)cy * pitch_words + start_x;
            memcpy(&back_buffer_vram[offset], &back_buffer[offset], bytes_per_line);
        }
    }

    // 2. Perform a single hardware page flip
    bga_write(9, back_page * height);

    // 3. Keep the new back page in sync by copying the same regions
    uint32_t* front_buffer_vram = (uint32_t*)((uint64_t)virtual_base + current_page * total_bytes);
    for (int i = 0; i < count; ++i) {
        Rect r = rects[i];
        int start_x = r.x < 0 ? 0 : r.x;
        int start_y = r.y < 0 ? 0 : r.y;
        int end_x = r.x + r.w > (int)width ? (int)width : r.x + r.w;
        int end_y = r.y + r.h > (int)height ? (int)height : r.y + r.h;
        if (start_x >= end_x || start_y >= end_y) continue;

        uint32_t bytes_per_line = (end_x - start_x) * sizeof(uint32_t);
        for (int cy = start_y; cy < end_y; ++cy) {
            uint64_t offset = (uint64_t)cy * pitch_words + start_x;
            memcpy(&front_buffer_vram[offset], &back_buffer[offset], bytes_per_line);
        }
    }

    current_page = back_page;
}

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
    uint64_t bytes_to_copy = (uint64_t)words_to_copy * sizeof(uint32_t);

    for (int cy = start_y; cy < end_y; ++cy) {
        int src_y = cy - y;
        uint64_t line_offset = (uint64_t)cy * pitch_words;
        memcpy(&back_buffer[line_offset + start_x], &src_buf[src_y * w + (start_x - x)], bytes_to_copy);
    }
}

void Framebuffer::swap_dirty_rect_fast(Rect r) {
    swap_dirty_rect(r);
}

} // namespace drivers