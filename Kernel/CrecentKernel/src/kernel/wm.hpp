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
    bool is_minimized;
    char title[64];
    uint32_t bg_color;
    bool is_dragging;
    Window* next;
    uint32_t* buffer;
    Event pending_event;
    char text_input[1024];
    int text_len;
    int selected_item_idx;

    Window(int id, int x, int y, int w, int h, const char* title, uint32_t color);
    void draw(bool is_active);

    bool title_is(const char* s);
    bool title_starts_with(const char* s);
};

struct ContextMenu {
    bool active;
    int x, y, w, h;
    int type;        // 0 = Apple, 1 = Finder/File, 2 = Desktop Right-Click
    int hovered_item;
};

class WindowManager {
    friend class Window;
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

    // Internal WhiteSur helpers
    static void draw_wallpaper();
    static void draw_desktop_icons();
    static void arrange_desktop();
    static void draw_menu_bar();
    static void draw_dock();
    static void draw_menu();
    static void draw_window_shadow(const Rect& r, bool active, uint8_t alpha);
    static void draw_traffic_lights(Window* win, uint8_t alpha);
    static void draw_window_body(Window* win, uint8_t alpha, bool is_terminal);
    static void draw_terminal_content(Window* win);
    static void draw_finder_content(Window* win);
    static void draw_system_log_content(Window* win);
    static void draw_perf_monitor_content(Window* win);
    static void draw_about_content(Window* win);
    static void draw_safari_content(Window* win);
    static void draw_mail_content(Window* win);
    static void draw_appstore_content(Window* win);
    static void draw_notes_content(Window* win);
    static void draw_code_editor_content(Window* win);
    static void draw_drag_preview();
    static void draw_window_client_area(Window* win);
    static void minimize_window_animated(Window* win);

    static bool in_rect(int px, int py, int rx, int ry, int rw, int rh);
    static bool in_rect(int px, int py, const Rect& r);
    static void bring_to_front(Window* win);
    static void draw_all_windows();
    static void blit_cursor();
    static void redraw_dirty_rect(const Rect& dirty);

    static void int_to_str(int v, char* buf, int len);

    static int perf_cpu_history[20];
    static int perf_current_cpu;
    static int frame_counter;

public:
    static void init();
    static int get_string_width(const char* s, float size);
    static void draw_string(const char* s, int x, int y, uint32_t c, float size);
    static Window* create_window(int x, int y, int w, int h, const char* title, uint32_t color);
    static void close_window(int id);
    static Window* get_window_by_id(int id);

    static void draw_desktop();
    static void draw_mac_decorations();
    static void force_redraw_all();
    static void trigger_host_persist();
    static void handle_mouse_move(int new_x, int new_y, bool left_pressed, bool right_pressed);
    static void handle_key_press(char c);
    static void tick();
    static void draw_settings_content(Window* win);

    static int get_mouse_x() { return mouse_x; }
    static int get_mouse_y() { return mouse_y; }

    // UI Scaling and Customization globals
    static float ui_scale;
    static bool dark_mode;
    static int wallpaper_theme_id;

    static inline int scale_dim(int val) { return (int)(val * ui_scale); }
    static inline float scale_font(float size) { return size * ui_scale; }
    static void set_ui_scale(float scale);
    static void set_theme(bool dark);
};

} // namespace wm
