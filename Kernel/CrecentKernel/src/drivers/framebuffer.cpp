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
                return bar0 & 0xFFFFFFF0ULL; // Extract base address
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

bool Framebuffer::init(uint64_t phys_addr, uint32_t w, uint32_t h, uint32_t p, uint8_t b) {
    physical_base = phys_addr;
    width = w;
    height = h;
    pitch = p;
    bpp = b;

    // 1. Map physical framebuffer memory range to the virtual window at 56GB (0xE00000000)
    // Map with Write-Through caching flag (VMM_FLAG_WRITE_THR) to avoid memory write stalls on MMIO
    uint64_t size = height * pitch;
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
    clear(0x00000000);
    swap_buffers();

    return true;
}

void Framebuffer::clear(uint32_t color) {
    if (!initialized) return;
    uint32_t size_words = (height * pitch) / 4;
    for (uint32_t i = 0; i < size_words; ++i) {
        back_buffer[i] = color;
    }
}

void Framebuffer::draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    // Fast boundary check to ensure memory safety
    if (x >= width || y >= height) return;
    back_buffer[y * (pitch / 4) + x] = color;
}

uint32_t Framebuffer::get_pixel(uint32_t x, uint32_t y) {
    if (!initialized || x >= width || y >= height) return 0;
    return back_buffer[y * (pitch / 4) + x];
}

void Framebuffer::draw_pixel_physical(uint32_t x, uint32_t y, uint32_t color) {
    if (!initialized || x >= width || y >= height) return;
    virtual_base[y * (pitch / 4) + x] = color;
}

void Framebuffer::draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t end_x = x + w;
    uint32_t end_y = y + h;
    
    // Clamp to screen limits
    if (end_x > width) end_x = width;
    if (end_y > height) end_y = height;

    for (uint32_t cy = y; cy < end_y; ++cy) {
        uint32_t line_offset = cy * (pitch / 4);
        for (uint32_t cx = x; cx < end_x; ++cx) {
            back_buffer[line_offset + cx] = color;
        }
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
    // Optimized 64-bit copying transfers 2 pixels at a time (8 bytes), maximizing bus blit rates
    uint64_t size_quads = (height * pitch) / 8;
    uint64_t* src = (uint64_t*)back_buffer;
    uint64_t* dest = (uint64_t*)virtual_base;
    for (uint64_t i = 0; i < size_quads; ++i) {
        dest[i] = src[i];
    }
}

void Framebuffer::swap_dirty_rect(Rect r) {
    if (!initialized) return;

    // Boundary containment checks
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

void Framebuffer::swap_dirty_rect_fast(Rect r) {
    if (!initialized) return;

    // Boundary containment checks
    int x0 = r.x; if (x0 < 0) x0 = 0;
    int y0 = r.y; if (y0 < 0) y0 = 0;
    int x1 = r.x + r.w; if (x1 > (int)width) x1 = width;
    int y1 = r.y + r.h; if (y1 > (int)height) y1 = height;
    
    if (x0 >= x1 || y0 >= y1) return;

    uint32_t pitch_words = pitch / 4;
    for (int y = y0; y < y1; ++y) {
        uint32_t line_offset = y * pitch_words;
        uint32_t* src = &back_buffer[line_offset + x0];
        uint32_t* dest = &virtual_base[line_offset + x0];
        
        // Fast row-level block copy using uint64_t transfers (2 pixels per instruction)
        uint32_t words_to_copy = x1 - x0;
        uint32_t quads_to_copy = words_to_copy / 2;
        uint64_t* src_q = (uint64_t*)src;
        uint64_t* dest_q = (uint64_t*)dest;
        
        for (uint32_t i = 0; i < quads_to_copy; ++i) {
            dest_q[i] = src_q[i];
        }
        
        // Copy leftover pixel if width was odd
        if (words_to_copy % 2) {
            dest[words_to_copy - 1] = src[words_to_copy - 1];
        }
    }
}

} // namespace drivers
