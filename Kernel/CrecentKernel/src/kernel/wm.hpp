#pragma once

#include "types.hpp"
#include "../drivers/framebuffer.hpp"

namespace wm {

using drivers::Rect;

class Window {
public:
    int id;
    Rect rect;
    char title[64];
    uint32_t bg_color;
    bool is_dragging;
    Window* next;

    Window(int id, int x, int y, int w, int h, const char* title, uint32_t color);
    void draw();
};

class WindowManager {
private:
    static Window* window_list_head;
    static int next_window_id;
    static int mouse_x;
    static int mouse_y;
    static bool mouse_pressed;
    static Window* active_window;
    static int drag_offset_x;
    static int drag_offset_y;

    static void focus_window(Window* win);

public:
    static void init();
    static Window* create_window(int x, int y, int w, int h, const char* title, uint32_t color);
    static void draw_desktop();
    static void handle_mouse_move(int new_x, int new_y, bool pressed);
    
    static int get_mouse_x() { return mouse_x; }
    static int get_mouse_y() { return mouse_y; }
};

} // namespace wm
