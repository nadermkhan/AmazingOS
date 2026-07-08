#include "wm.hpp"
#include "../drivers/framebuffer.hpp"
#include "../drivers/serial.hpp"

namespace wm {

Window* WindowManager::window_list_head = nullptr;
int WindowManager::next_window_id = 1;
int WindowManager::mouse_x = 512;
int WindowManager::mouse_y = 384;
bool WindowManager::mouse_pressed = false;
Window* WindowManager::active_window = nullptr;
int WindowManager::drag_offset_x = 0;
int WindowManager::drag_offset_y = 0;

Window::Window(int id, int x, int y, int w, int h, const char* title, uint32_t color) {
    this->id = id;
    this->rect = {x, y, w, h};
    
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

void Window::draw() {
    // 1. Draw outer frame shadow/border (steel gray: 0x00A0A0A0)
    drivers::Framebuffer::draw_rect(rect.x, rect.y, rect.w, rect.h, 0x00A0A0A0);

    // 2. Draw active/inactive title bar (focused: steel blue 0x002A4B7C, default: 0x00555555)
    bool isActive = (WindowManager::get_mouse_x() >= rect.x && WindowManager::get_mouse_x() < rect.x + rect.w &&
                     WindowManager::get_mouse_y() >= rect.y && WindowManager::get_mouse_y() < rect.y + rect.h);
    uint32_t title_bar_color = isActive ? 0x002A4B7C : 0x004A5A6A;
    drivers::Framebuffer::draw_rect(rect.x + 2, rect.y + 2, rect.w - 4, 18, title_bar_color);

    // 3. Render Title text (white)
    drivers::Framebuffer::draw_string(title, rect.x + 6, rect.y + 7, 0x00FFFFFF);

    // 4. Render Close Button (red widget square)
    drivers::Framebuffer::draw_rect(rect.x + rect.w - 16, rect.y + 5, 12, 12, 0x00FF3333);
    drivers::Framebuffer::draw_rect(rect.x + rect.w - 13, rect.y + 8, 6, 6, 0x00FFFFFF); // Inner dot

    // 5. Fill Window Client Area (Window background body)
    drivers::Framebuffer::draw_rect(rect.x + 2, rect.y + 20, rect.w - 4, rect.h - 22, bg_color);
}

void WindowManager::init() {
    window_list_head = nullptr;
    next_window_id = 1;
    mouse_x = 512;
    mouse_y = 384;
    mouse_pressed = false;
    active_window = nullptr;
    
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

static void draw_mouse_cursor(int mx, int my) {
    // Render a high-contrast white arrow with a black border
    // Draw white interior arrow body
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < y + 1; ++x) {
            drivers::Framebuffer::draw_pixel(mx + x, my + y, 0x00FFFFFF);
        }
    }
    // Draw black outline border around the arrow
    for (int y = 0; y < 13; ++y) {
        drivers::Framebuffer::draw_pixel(mx, my + y, 0x00000000);
        drivers::Framebuffer::draw_pixel(mx + y, my + y, 0x00000000);
    }
    for (int x = 0; x < 13; ++x) {
        drivers::Framebuffer::draw_pixel(mx + x, my + 12, 0x00000000);
    }
}

void WindowManager::draw_desktop() {
    // 1. Clear background wallpaper (gorgeous slate steel color: 0x003A4E5C)
    drivers::Framebuffer::clear(0x003A4E5C);

    // 2. Draw desktop decoration header text
    drivers::Framebuffer::draw_string("CrecentOS - Desktop Environment (Ring 0 Compositor)", 20, 20, 0x00E0E0E0);
    drivers::Framebuffer::draw_string("Use Mouse to Drag Windows | Preemption & Multitasking Active", 20, 36, 0x0090A0B0);

    // 3. Render windows in z-order (first elements drawn at the back, tail element on top)
    Window* curr = window_list_head;
    while (curr) {
        curr->draw();
        curr = curr->next;
    }

    // 4. Render Mouse cursor last so it sits on top of all windows
    draw_mouse_cursor(mouse_x, mouse_y);
    
    // 5. Swap the frame buffers to update screen
    drivers::Framebuffer::swap_buffers();
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

    // Detect click state changes (fresh mouse click event)
    if (pressed && !mouse_pressed) {
        // Traverse windows from back-to-front, selecting the frontmost clicked window
        Window* clicked = nullptr;
        Window* curr = window_list_head;
        while (curr) {
            // Check if coordinates land in the title bar zone (height 20px)
            Rect title_rect = {curr->rect.x, curr->rect.y, curr->rect.w, 20};
            if (title_rect.contains(new_x, new_y)) {
                clicked = curr;
            }
            curr = curr->next;
        }

        if (clicked) {
            active_window = clicked;
            active_window->is_dragging = true;
            drag_offset_x = new_x - active_window->rect.x;
            drag_offset_y = new_y - active_window->rect.y;

            // Re-order active window to front of z-order compositor list
            focus_window(active_window);
            
            drivers::Serial::print("[WM] Focus/Drag window: ");
            drivers::Serial::println(active_window->title);
        }
    }

    // Update coordinates
    mouse_x = new_x;
    mouse_y = new_y;

    // Process drag movement
    if (pressed && active_window && active_window->is_dragging) {
        active_window->rect.x = new_x - drag_offset_x;
        active_window->rect.y = new_y - drag_offset_y;
        
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

    // Click release state change
    if (!pressed && mouse_pressed) {
        if (active_window) {
            active_window->is_dragging = false;
            drivers::Serial::print("[WM] Released window: ");
            drivers::Serial::println(active_window->title);
            active_window = nullptr;
        }
    }

    mouse_pressed = pressed;
}

} // namespace wm
