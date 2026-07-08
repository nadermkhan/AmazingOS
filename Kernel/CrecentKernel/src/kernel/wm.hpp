#pragma once

#include "types.hpp"
#include "../drivers/framebuffer.hpp"

namespace wm {

using drivers::Rect;

class Window {
public:
    int id;
    Rect rect;
    Rect orig_rect;
    bool is_maximized;
    char title[64];
    uint32_t bg_color;
    bool is_dragging;
    Window* next;

    Window(int id, int x, int y, int w, int h, const char* title, uint32_t color);
    void draw();
};

struct ContextMenu {
    bool active;
    int x, y, w, h;
    int type; // 0 = Apple, 1 = Finder/File, 2 = Desktop Right-Click
    int hovered_item;
};

class WindowManager {
private:
    static Window* window_list_head;
    static int next_window_id;
    static int mouse_x;
    static int mouse_y;
    static int last_mouse_x;
    static int last_mouse_y;
    static bool mouse_pressed;
    static bool right_mouse_pressed;
    static Window* active_window;
    static int drag_offset_x;
    static int drag_offset_y;
    
    static ContextMenu active_menu;

    static void focus_window(Window* win);
    static void draw_cursor();
    static void erase_cursor();

public:
    static void init();
    static Window* create_window(int x, int y, int w, int h, const char* title, uint32_t color);
    static void close_window(int id);
    static void draw_desktop();
    static void draw_mac_decorations();
    static void force_redraw_all();
    static void handle_mouse_move(int new_x, int new_y, bool left_pressed, bool right_pressed);
    
    static int get_mouse_x() { return mouse_x; }
    static int get_mouse_y() { return mouse_y; }
};

} // namespace wm
