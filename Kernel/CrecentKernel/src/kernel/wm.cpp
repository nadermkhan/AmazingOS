#include "wm.hpp"
#include "../drivers/framebuffer.hpp"
#include "../drivers/serial.hpp"

namespace wm {

Window* WindowManager::window_list_head = nullptr;
int WindowManager::next_window_id = 1;
int WindowManager::mouse_x = 512;
int WindowManager::mouse_y = 384;
int WindowManager::last_mouse_x = 512;
int WindowManager::last_mouse_y = 384;
bool WindowManager::mouse_pressed = false;
Window* WindowManager::active_window = nullptr;
int WindowManager::drag_offset_x = 0;
int WindowManager::drag_offset_y = 0;

Window::Window(int id, int x, int y, int w, int h, const char* title, uint32_t color) {
    this->id = id;
    this->rect = {x, y, w, h};
    this->orig_rect = {x, y, w, h};
    this->is_maximized = false;
    
    int i = 0;
    while (title[i] && i < 63) {
        this->title[i] = title[i];
        i++;
    }
    this->title[i] = '\0';
    
    this->bg_color = color;
    this->is_dragging = false;
    this->next = nullptr;
}

static inline uint32_t get_wallpaper_color(uint32_t cx, uint32_t cy) {
    uint32_t factor = (cx + cy) * 255 / 1792;
    uint32_t r = ((0x5E * (255 - factor)) + (0x0F * factor)) >> 8;
    uint32_t g = ((0x21 * (255 - factor)) + (0x52 * factor)) >> 8;
    uint32_t b = ((0xD0 * (255 - factor)) + (0xBC * factor)) >> 8;
    return (r << 16) | (g << 8) | b;
}

static uint32_t cursor_bg_cache[256];
static int cached_cursor_x = 0;
static int cached_cursor_y = 0;
static bool cursor_cached = false;

void Window::draw() {
    uint8_t alpha = is_dragging ? 140 : 255;
    
    // Draw translucent 3D drop shadows (bottom and right borders)
    // 4px wide, black color (0x00000000) with alpha = 60
    drivers::Framebuffer::draw_rect_alpha(rect.x + 4, rect.y + rect.h, rect.w, 4, 0x00000000, 60);
    drivers::Framebuffer::draw_rect_alpha(rect.x + rect.w, rect.y + 4, 4, rect.h - 4, 0x00000000, 60);

    // 1. Draw outer frame border (macOS thin grey outline border)
    if (is_dragging) {
        drivers::Framebuffer::draw_rect_alpha(rect.x, rect.y, rect.w, rect.h, 0x00D0D0D0, alpha);
    } else {
        drivers::Framebuffer::draw_rect(rect.x, rect.y, rect.w, rect.h, 0x00D0D0D0);
    }

    // 2. Draw title bar (focused: light grey 0x00E5E5E5, unfocused: 0x00F0F0F0)
    bool isActive = (WindowManager::get_mouse_x() >= rect.x && WindowManager::get_mouse_x() < rect.x + rect.w &&
                     WindowManager::get_mouse_y() >= rect.y && WindowManager::get_mouse_y() < rect.y + rect.h);
    uint32_t title_bar_color = isActive ? 0x00E5E5E5 : 0x00F0F0F0;
    if (is_dragging) {
        drivers::Framebuffer::draw_rect_alpha(rect.x + 1, rect.y + 1, rect.w - 2, 22, title_bar_color, alpha);
    } else {
        drivers::Framebuffer::draw_rect(rect.x + 1, rect.y + 1, rect.w - 2, 22, title_bar_color);
    }

    // Shave top window corners to make them rounded (macOS Big Sur style)
    drivers::Framebuffer::draw_pixel(rect.x, rect.y, get_wallpaper_color(rect.x, rect.y));
    drivers::Framebuffer::draw_pixel(rect.x + 1, rect.y, get_wallpaper_color(rect.x + 1, rect.y));
    drivers::Framebuffer::draw_pixel(rect.x, rect.y + 1, get_wallpaper_color(rect.x, rect.y + 1));

    drivers::Framebuffer::draw_pixel(rect.x + rect.w - 1, rect.y, get_wallpaper_color(rect.x + rect.w - 1, rect.y));
    drivers::Framebuffer::draw_pixel(rect.x + rect.w - 2, rect.y, get_wallpaper_color(rect.x + rect.w - 2, rect.y));
    drivers::Framebuffer::draw_pixel(rect.x + rect.w - 1, rect.y + 1, get_wallpaper_color(rect.x + rect.w - 1, rect.y + 1));

    // 3. Render Title text (centered macOS style, dark grey text for light mode aesthetic)
    int title_len = 0;
    while (title[title_len]) title_len++;
    int title_x = rect.x + (rect.w - title_len * 8) / 2;
    drivers::Framebuffer::draw_string(title, title_x, rect.y + 8, 0x00333333);

    // 4. Render Close, Minimize, Maximize Buttons (macOS Traffic Lights!)
    if (is_dragging) {
        drivers::Framebuffer::draw_rect_alpha(rect.x + 10, rect.y + 8, 8, 8, 0x00FF5F56, alpha); // Red close
        drivers::Framebuffer::draw_rect_alpha(rect.x + 24, rect.y + 8, 8, 8, 0x00FFBD2E, alpha); // Yellow minimize
        drivers::Framebuffer::draw_rect_alpha(rect.x + 38, rect.y + 8, 8, 8, 0x0027C93F, alpha); // Green maximize
    } else {
        drivers::Framebuffer::draw_rect(rect.x + 10, rect.y + 8, 8, 8, 0x00FF5F56); // Red close
        drivers::Framebuffer::draw_rect(rect.x + 24, rect.y + 8, 8, 8, 0x00FFBD2E); // Yellow minimize
        drivers::Framebuffer::draw_rect(rect.x + 38, rect.y + 8, 8, 8, 0x0027C93F); // Green maximize
    }

    // 5. Fill Window Client Area (Window background body - clean white)
    bool isTerminal = (title[0] == 'T' && title[1] == 'e' && title[2] == 'r' && title[3] == 'm' && title[4] == 'i' && title[5] == 'n' && title[6] == 'a' && title[7] == 'l' && title[8] == '\0');
    if (isTerminal) {
        // Terminal is ALWAYS translucent dark charcoal, even when not dragging!
        drivers::Framebuffer::draw_rect_alpha(rect.x + 1, rect.y + 23, rect.w - 2, rect.h - 24, bg_color, is_dragging ? 120 : 180);
    } else {
        if (is_dragging) {
            drivers::Framebuffer::draw_rect_alpha(rect.x + 1, rect.y + 23, rect.w - 2, rect.h - 24, bg_color, alpha);
        } else {
            drivers::Framebuffer::draw_rect(rect.x + 1, rect.y + 23, rect.w - 2, rect.h - 24, bg_color);
        }
    }

    // Shave bottom window corners to make them rounded
    drivers::Framebuffer::draw_pixel(rect.x, rect.y + rect.h - 1, get_wallpaper_color(rect.x, rect.y + rect.h - 1));
    drivers::Framebuffer::draw_pixel(rect.x + 1, rect.y + rect.h - 1, get_wallpaper_color(rect.x + 1, rect.y + rect.h - 1));
    drivers::Framebuffer::draw_pixel(rect.x, rect.y + rect.h - 2, get_wallpaper_color(rect.x, rect.y + rect.h - 2));

    drivers::Framebuffer::draw_pixel(rect.x + rect.w - 1, rect.y + rect.h - 1, get_wallpaper_color(rect.x + rect.w - 1, rect.y + rect.h - 1));
    drivers::Framebuffer::draw_pixel(rect.x + rect.w - 2, rect.y + rect.h - 1, get_wallpaper_color(rect.x + rect.w - 2, rect.y + rect.h - 1));
    drivers::Framebuffer::draw_pixel(rect.x + rect.w - 1, rect.y + rect.h - 2, get_wallpaper_color(rect.x + rect.w - 1, rect.y + rect.h - 2));

    // 6. Draw custom mock content for styled apps
    const char* t = title;
    if (isTerminal) {
        drivers::Framebuffer::draw_string("crecent@macos:~$ neofetch", rect.x + 10, rect.y + 30, 0x004AF02C); // Green prompt
        drivers::Framebuffer::draw_string("OS: CrecentOS Whitesur 1.0", rect.x + 10, rect.y + 46, 0x00D0D0D0);
        drivers::Framebuffer::draw_string("Kernel: x86_64 Freestanding Core", rect.x + 10, rect.y + 60, 0x00D0D0D0);
        drivers::Framebuffer::draw_string("Shell: Terminal (Ring 0 Compositor)", rect.x + 10, rect.y + 74, 0x00D0D0D0);
        drivers::Framebuffer::draw_string("Resolution: 1024x768 (60 FPS VBE)", rect.x + 10, rect.y + 88, 0x00D0D0D0);
        drivers::Framebuffer::draw_string("Uptime: 2 mins", rect.x + 10, rect.y + 102, 0x00D0D0D0);
        drivers::Framebuffer::draw_string("crecent@macos:~$ _", rect.x + 10, rect.y + 120, 0x00FFFFFF);
    } else if (t[0] == 'F' && t[1] == 'i' && t[2] == 'n' && t[3] == 'd' && t[4] == 'e' && t[5] == 'r' && t[6] == '\0') {
        // Mock Finder Content
        drivers::Framebuffer::draw_rect(rect.x + 20, rect.y + 40, 24, 18, 0x0000A2C9); // Folder icon (Cyan)
        drivers::Framebuffer::draw_string("tar", rect.x + 20, rect.y + 62, 0x00333333);
        
        drivers::Framebuffer::draw_rect(rect.x + 75, rect.y + 40, 24, 18, 0x0000A2C9); // Folder icon (Cyan)
        drivers::Framebuffer::draw_string("dev", rect.x + 75, rect.y + 62, 0x00333333);
        
        drivers::Framebuffer::draw_rect(rect.x + 130, rect.y + 40, 24, 18, 0x0000A2C9); // Folder icon (Cyan)
        drivers::Framebuffer::draw_string("sys", rect.x + 130, rect.y + 62, 0x00333333);

        drivers::Framebuffer::draw_rect(rect.x + 20, rect.y + 90, 20, 24, 0x00E0E0E0); // File icon
        drivers::Framebuffer::draw_string("hello.txt", rect.x + 12, rect.y + 118, 0x00333333);
        
        drivers::Framebuffer::draw_rect(rect.x + 85, rect.y + 90, 20, 24, 0x00E0E0E0); // File icon
        drivers::Framebuffer::draw_string("info.txt", rect.x + 77, rect.y + 118, 0x00333333);
    }
}

void WindowManager::init() {
    window_list_head = nullptr;
    next_window_id = 1;
    mouse_x = 512;
    mouse_y = 384;
    last_mouse_x = 512;
    last_mouse_y = 384;
    mouse_pressed = false;
    active_window = nullptr;
    cursor_cached = false;
    
    drivers::Serial::println("[INIT] Window Manager Compositor initialized.");
}

Window* WindowManager::create_window(int x, int y, int w, int h, const char* title, uint32_t color) {
    Window* win = new Window(next_window_id++, x, y, w, h, title, color);
    
    // Append window to the z-order rendering list (end of list draws on top)
    if (!window_list_head) {
        window_list_head = win;
    } else {
        Window* curr = window_list_head;
        while (curr->next) {
            curr = curr->next;
        }
        curr->next = win;
    }
    
    drivers::Serial::print("[WM] Created window: ID=");
    // Print ID manually in decimal
    char buf[16];
    int val = win->id;
    int idx = 0;
    if (val == 0) buf[idx++] = '0';
    else {
        char rev[16];
        int r_idx = 0;
        while (val > 0) {
            rev[r_idx++] = '0' + (val % 10);
            val /= 10;
        }
        while (r_idx > 0) buf[idx++] = rev[--r_idx];
    }
    buf[idx] = '\0';
    drivers::Serial::print(buf);
    drivers::Serial::print(" Title=\"");
    drivers::Serial::print(win->title);
    drivers::Serial::println("\"");
    
    return win;
}

void WindowManager::close_window(int id) {
    if (!window_list_head) return;
    
    Window* target = nullptr;
    Window* prev = nullptr;
    Window* curr = window_list_head;
    while (curr) {
        if (curr->id == id) {
            target = curr;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    
    if (target) {
        if (prev) {
            prev->next = target->next;
        } else {
            window_list_head = target->next;
        }
        
        if (active_window == target) {
            active_window = nullptr;
        }
        
        delete target;
    }
}

void WindowManager::draw_cursor() {
    int mx = mouse_x;
    int my = mouse_y;
    
    // 1. Cache the clean background pixels under the cursor in the BACK buffer
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            cursor_bg_cache[y * 16 + x] = drivers::Framebuffer::get_pixel(mx + x, my + y);
        }
    }
    cached_cursor_x = mx;
    cached_cursor_y = my;
    cursor_cached = true;
    
    // 2. Draw high-contrast white arrow with a black outline directly on the BACK buffer
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < y + 1; ++x) {
            drivers::Framebuffer::draw_pixel(mx + x, my + y, 0x00FFFFFF);
        }
    }
    for (int y = 0; y < 13; ++y) {
        drivers::Framebuffer::draw_pixel(mx, my + y, 0x00000000);
        drivers::Framebuffer::draw_pixel(mx + y, my + y, 0x00000000);
    }
    for (int x = 0; x < 13; ++x) {
        drivers::Framebuffer::draw_pixel(mx + x, my + 12, 0x00000000);
    }
}

void WindowManager::erase_cursor() {
    if (!cursor_cached) return;
    
    // Restore the saved background pixels back to the BACK buffer
    int mx = cached_cursor_x;
    int my = cached_cursor_y;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            drivers::Framebuffer::draw_pixel(mx + x, my + y, cursor_bg_cache[y * 16 + x]);
        }
    }
    cursor_cached = false;
}

void WindowManager::draw_mac_decorations() {
    // 1. Draw top macOS Menu Bar (translucent-style light grey)
    drivers::Framebuffer::draw_rect(0, 0, 1024, 22, 0x00F0F0F0);
    drivers::Framebuffer::draw_rect(0, 22, 1024, 1, 0x00D2D2D2);

    // 2. Draw Apple Logo and Menu Bar items
    static const uint8_t apple_logo[8] = {
        0b00011000, //   **  
        0b00111100, //  **** 
        0b01111110, // ******
        0b11111111, //********
        0b11111111, //********
        0b11111111, //********
        0b01111110, // ******
        0b00110110  //  ** ** 
    };
    int start_x = 10;
    int start_y = 7;
    for (int r = 0; r < 8; ++r) {
        uint8_t row_bits = apple_logo[r];
        for (int c = 0; c < 8; ++c) {
            if (row_bits & (0x80 >> c)) {
                drivers::Framebuffer::draw_pixel(start_x + c, start_y + r, 0x00333333);
            }
        }
    }
    drivers::Framebuffer::draw_string("Finder  File  Edit  View  Go  Window  Help", 26, 7, 0x00333333);
    drivers::Framebuffer::draw_string("Wed 14:35", 930, 7, 0x00333333);

    // 3. Draw bottom macOS Dock (translucent centered tray)
    // Dock centered at x=312, y=710, width=400, height=48
    drivers::Framebuffer::draw_rect(312, 710, 400, 48, 0x00EAEAEA);
    drivers::Framebuffer::draw_rect(312, 710, 400, 1, 0x00C0C0C0);
    drivers::Framebuffer::draw_rect(312, 757, 400, 1, 0x00C0C0C0);
    drivers::Framebuffer::draw_rect(312, 710, 1, 48, 0x00C0C0C0);
    drivers::Framebuffer::draw_rect(711, 710, 1, 48, 0x00C0C0C0);

    // Shave Dock corners to make it rounded
    drivers::Framebuffer::draw_pixel(312, 710, get_wallpaper_color(312, 710));
    drivers::Framebuffer::draw_pixel(313, 710, get_wallpaper_color(313, 710));
    drivers::Framebuffer::draw_pixel(312, 711, get_wallpaper_color(312, 711));
    drivers::Framebuffer::draw_pixel(711, 710, get_wallpaper_color(711, 710));
    drivers::Framebuffer::draw_pixel(710, 710, get_wallpaper_color(710, 710));
    drivers::Framebuffer::draw_pixel(711, 711, get_wallpaper_color(711, 711));
    drivers::Framebuffer::draw_pixel(312, 757, get_wallpaper_color(312, 757));
    drivers::Framebuffer::draw_pixel(313, 757, get_wallpaper_color(313, 757));
    drivers::Framebuffer::draw_pixel(312, 756, get_wallpaper_color(312, 756));
    drivers::Framebuffer::draw_pixel(711, 757, get_wallpaper_color(711, 757));
    drivers::Framebuffer::draw_pixel(710, 757, get_wallpaper_color(710, 757));
    drivers::Framebuffer::draw_pixel(711, 756, get_wallpaper_color(711, 756));

    // 4. Draw Dock Application Icons (colored rounded squares with clean identifiers)
    drivers::Framebuffer::draw_rect(332, 716, 36, 36, 0x001B72E8); // Finder (Blue)
    drivers::Framebuffer::draw_string("F", 346, 730, 0x00FFFFFF);

    drivers::Framebuffer::draw_rect(388, 716, 36, 36, 0x0000A2C9); // Safari (Cyan)
    drivers::Framebuffer::draw_string("S", 402, 730, 0x00FFFFFF);

    drivers::Framebuffer::draw_rect(444, 716, 36, 36, 0x00E0E0E0); // Mail (White)
    drivers::Framebuffer::draw_string("M", 458, 730, 0x00FF3B30);

    drivers::Framebuffer::draw_rect(500, 716, 36, 36, 0x001A1A1A); // Terminal (Black)
    drivers::Framebuffer::draw_string("T", 514, 730, 0x004AF02C);

    drivers::Framebuffer::draw_rect(556, 716, 36, 36, 0x007E8E9F); // Settings (Grey)
    drivers::Framebuffer::draw_string("A", 570, 730, 0x00FFFFFF);

    drivers::Framebuffer::draw_rect(612, 716, 36, 36, 0x00FFCC00); // Notes (Yellow)
    drivers::Framebuffer::draw_string("N", 626, 730, 0x00333333);

    // 5. Draw indicator dots under icons if Finder or Terminal windows are open
    bool finder_running = false;
    bool terminal_running = false;
    Window* curr_win = window_list_head;
    while (curr_win) {
        const char* t = curr_win->title;
        if (t[0] == 'F' && t[1] == 'i' && t[2] == 'n' && t[3] == 'd' && t[4] == 'e' && t[5] == 'r' && t[6] == '\0') {
            finder_running = true;
        }
        if (t[0] == 'T' && t[1] == 'e' && t[2] == 'r' && t[3] == 'm' && t[4] == 'i' && t[5] == 'n' && t[6] == 'a' && t[7] == 'l' && t[8] == '\0') {
            terminal_running = true;
        }
        curr_win = curr_win->next;
    }
    if (finder_running) {
        drivers::Framebuffer::draw_rect(348, 753, 4, 2, 0x00333333);
    }
    if (terminal_running) {
        drivers::Framebuffer::draw_rect(516, 753, 4, 2, 0x00333333);
    }
}

void WindowManager::draw_desktop() {
    // Non-op as we handle precise dirty rect blits inside mouse movement handles
}

void WindowManager::force_redraw_all() {
    // Clear back buffer with gradient wallpaper
    drivers::Framebuffer::draw_mac_wallpaper(0, 0, 1024, 768);
    
    // Draw macOS desktop decorations
    draw_mac_decorations();
    
    // Draw windows in z-order
    Window* curr = window_list_head;
    while (curr) {
        curr->draw();
        curr = curr->next;
    }
    
    // Swap full screen
    drivers::Framebuffer::swap_buffers();
    
    // Draw cursor in the back buffer and cache it
    draw_cursor();
    
    // Blit the small cursor rect to display
    Rect cursor_rect = { mouse_x, mouse_y, 16, 16 };
    drivers::Framebuffer::swap_dirty_rect_fast(cursor_rect);

    last_mouse_x = mouse_x;
    last_mouse_y = mouse_y;
}

void WindowManager::focus_window(Window* win) {
    if (!win || !WindowManager::window_list_head) return;
    if (win->next == nullptr) return; // Already at the top (tail)

    // Remove window node from its current linked position
    if (win == WindowManager::window_list_head) {
        WindowManager::window_list_head = win->next;
    } else {
        Window* prev = WindowManager::window_list_head;
        while (prev && prev->next != win) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = win->next;
        }
    }

    // Traverse and append to the tail (which draws last/topmost)
    Window* tail = WindowManager::window_list_head;
    while (tail && tail->next) {
        tail = tail->next;
    }
    if (tail) {
        tail->next = win;
        win->next = nullptr;
    } else {
        WindowManager::window_list_head = win;
        win->next = nullptr;
    }
}

void WindowManager::handle_mouse_move(int new_x, int new_y, bool pressed) {
    // Clamp mouse positions to display bounds (1024x768)
    if (new_x < 0) new_x = 0;
    if (new_x >= 1024) new_x = 1023;
    if (new_y < 0) new_y = 0;
    if (new_y >= 768) new_y = 767;

    // 1. Erase the old cursor from the BACK buffer
    erase_cursor();

    bool position_changed = (new_x != mouse_x || new_y != mouse_y);

    // Detect click state changes (fresh mouse click event)
    if (pressed && !mouse_pressed) {
        // Traverse windows from back-to-front, selecting the frontmost clicked window
        Window* clicked = nullptr;
        Window* curr = window_list_head;
        while (curr) {
            // Check if coordinates land in the title bar zone (height 22px in macOS style)
            Rect title_rect = {curr->rect.x, curr->rect.y, curr->rect.w, 22};
            if (title_rect.contains(new_x, new_y)) {
                clicked = curr;
            }
            curr = curr->next;
        }

        if (clicked) {
            // Check Red Close button click
            if (new_x >= clicked->rect.x + 10 && new_x <= clicked->rect.x + 18 &&
                new_y >= clicked->rect.y + 8 && new_y <= clicked->rect.y + 16) {
                close_window(clicked->id);
                force_redraw_all();
            }
            // Check Green Maximize button click
            else if (new_x >= clicked->rect.x + 38 && new_x <= clicked->rect.x + 46 &&
                     new_y >= clicked->rect.y + 8 && new_y <= clicked->rect.y + 16) {
                if (clicked->is_maximized) {
                    clicked->rect = clicked->orig_rect;
                    clicked->is_maximized = false;
                } else {
                    clicked->orig_rect = clicked->rect;
                    clicked->rect = { 0, 22, 1024, 680 }; // standard fullscreen below menu bar, above dock
                    clicked->is_maximized = true;
                }
                focus_window(clicked);
                force_redraw_all();
            }
            // Standard focus and drag start
            else {
                active_window = clicked;
                active_window->is_dragging = true;
                drag_offset_x = new_x - active_window->rect.x;
                drag_offset_y = new_y - active_window->rect.y;

                // Re-order active window to front of z-order compositor list
                focus_window(active_window);
                
                drivers::Serial::print("[WM] Focus/Drag window: ");
                drivers::Serial::println(active_window->title);
                
                // Full redraw to reflect focus change (active window z-order shift)
                force_redraw_all();
            }
        }
        // Check Dock app launching clicks
        else if (new_y >= 710 && new_y <= 758 && new_x >= 312 && new_x <= 712) {
            // Finder
            if (new_x >= 332 && new_x <= 368) {
                bool open = false;
                Window* w = window_list_head;
                while (w) {
                    const char* t = w->title;
                    if (t[0] == 'F' && t[1] == 'i' && t[2] == 'n' && t[3] == 'd' && t[4] == 'e' && t[5] == 'r' && t[6] == '\0') {
                        open = true;
                        focus_window(w);
                        break;
                    }
                    w = w->next;
                }
                if (!open) {
                    create_window(100, 100, 300, 200, "Finder", 0x00FFFFFF);
                }
                force_redraw_all();
            }
            // Terminal
            else if (new_x >= 500 && new_x <= 536) {
                bool open = false;
                Window* w = window_list_head;
                while (w) {
                    const char* t = w->title;
                    if (t[0] == 'T' && t[1] == 'e' && t[2] == 'r' && t[3] == 'm' && t[4] == 'i' && t[5] == 'n' && t[6] == 'a' && t[7] == 'l' && t[8] == '\0') {
                        open = true;
                        focus_window(w);
                        break;
                    }
                    w = w->next;
                }
                if (!open) {
                    create_window(150, 150, 300, 200, "Terminal", 0x001A1A1A);
                }
                force_redraw_all();
            }
        }
    }

    // Update coordinates
    mouse_x = new_x;
    mouse_y = new_y;

    // Process drag movement with Bounding-Box Merging (translucency + cursor inside)
    if (pressed && active_window && active_window->is_dragging && position_changed) {
        Rect old_rect = active_window->rect;
        int next_x = new_x - drag_offset_x;
        int next_y = new_y - drag_offset_y;

        if (next_x != old_rect.x || next_y != old_rect.y) {
            active_window->rect.x = next_x;
            active_window->rect.y = next_y;

            // Enclose old and new window rects AND cursor rects in the unified dirty bounding box
            // Note: window boundaries are expanded by +4 to encapsulate drop shadows
            int x_min = old_rect.x < next_x ? old_rect.x : next_x;
            if (last_mouse_x < x_min) x_min = last_mouse_x;
            if (mouse_x < x_min) x_min = mouse_x;

            int y_min = old_rect.y < next_y ? old_rect.y : next_y;
            if (last_mouse_y < y_min) y_min = last_mouse_y;
            if (mouse_y < y_min) y_min = mouse_y;

            int old_r = old_rect.x + old_rect.w + 4;
            int new_r = next_x + active_window->rect.w + 4;
            int x_max = old_r > new_r ? old_r : new_r;
            if (last_mouse_x + 16 > x_max) x_max = last_mouse_x + 16;
            if (mouse_x + 16 > x_max) x_max = mouse_x + 16;

            int old_b = old_rect.y + old_rect.h + 4;
            int new_b = next_y + active_window->rect.h + 4;
            int y_max = old_b > new_b ? old_b : new_b;
            if (last_mouse_y + 16 > y_max) y_max = last_mouse_y + 16;
            if (mouse_y + 16 > y_max) y_max = mouse_y + 16;

            Rect dirty = { x_min, y_min, x_max - x_min, y_max - y_min };

            // 1. Clear dirty region in back buffer to gradient wallpaper
            drivers::Framebuffer::draw_mac_wallpaper(dirty.x, dirty.y, dirty.w, dirty.h);

            // 2. Draw macOS desktop decorations in back buffer
            draw_mac_decorations();

            // 3. Draw all windows in z-order
            Window* curr = window_list_head;
            while (curr) {
                curr->draw();
                curr = curr->next;
            }

            // 4. Draw the new cursor on the back buffer so it is blitted atomically
            draw_cursor();

            // 5. Swap only the dirty rectangle using fast aligned blitter
            drivers::Framebuffer::swap_dirty_rect_fast(dirty);

            // Log coordinates for automated verification
            drivers::Serial::print("[WM] Drag position: X=");
            char buf_x[16];
            int val_x = active_window->rect.x;
            int idx_x = 0;
            if (val_x == 0) buf_x[idx_x++] = '0';
            else {
                char rev_x[16];
                int r_idx_x = 0;
                while (val_x > 0) {
                    rev_x[r_idx_x++] = '0' + (val_x % 10);
                    val_x /= 10;
                }
                while (r_idx_x > 0) buf_x[idx_x++] = rev_x[--r_idx_x];
            }
            buf_x[idx_x] = '\0';
            drivers::Serial::print(buf_x);

            drivers::Serial::print(" Y=");
            char buf_y[16];
            int val_y = active_window->rect.y;
            int idx_y = 0;
            if (val_y == 0) buf_y[idx_y++] = '0';
            else {
                char rev_y[16];
                int r_idx_y = 0;
                while (val_y > 0) {
                    rev_y[r_idx_y++] = '0' + (val_y % 10);
                    val_y /= 10;
                }
                while (r_idx_y > 0) buf_y[idx_y++] = rev_y[--r_idx_y];
            }
            buf_y[idx_y] = '\0';
            drivers::Serial::println(buf_y);
        }
    } else if (position_changed) {
        // If the window is not dragging but the mouse moved, we only swap the cursor dirty region
        int x_min = last_mouse_x < mouse_x ? last_mouse_x : mouse_x;
        int y_min = last_mouse_y < mouse_y ? last_mouse_y : mouse_y;
        int x_max = (last_mouse_x + 16) > (mouse_x + 16) ? (last_mouse_x + 16) : (mouse_x + 16);
        int y_max = (last_mouse_y + 16) > (mouse_y + 16) ? (last_mouse_y + 16) : (mouse_y + 16);
        Rect cursor_dirty = { x_min, y_min, x_max - x_min, y_max - y_min };

        // Draw new cursor on the back buffer
        draw_cursor();

        // Swap only this small cursor dirty region
        drivers::Framebuffer::swap_dirty_rect_fast(cursor_dirty);
    }

    // Click release state change
    if (!pressed && mouse_pressed) {
        if (active_window) {
            active_window->is_dragging = false;
            drivers::Serial::print("[WM] Released window: ");
            drivers::Serial::println(active_window->title);
            active_window = nullptr;
            
            // Full redraw to clean up any window boundaries
            force_redraw_all();
        }
    }

    mouse_pressed = pressed;

    // Cache current cursor positions
    last_mouse_x = mouse_x;
    last_mouse_y = mouse_y;
}

} // namespace wm
