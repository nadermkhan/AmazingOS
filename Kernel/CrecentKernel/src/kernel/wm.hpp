#pragma once

#include "types.hpp"
#include "../drivers/framebuffer.hpp"
#include "../fs/vfs.hpp"

namespace wm {

using drivers::Rect;

struct DirtyList {
    Rect rects[16];
    int count = 0;

    void clear() {
        count = 0;
    }

    void add(const Rect& r);
    Rect get_bounding_box() const;
};

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

    // Finder VFS Cache
    static constexpr int FINDER_MAX_ITEMS = 64;
    fs::VFSNode finder_items[FINDER_MAX_ITEMS];
    int finder_item_count;
    bool finder_needs_reload;
    bool finder_list_view_mode;
    bool finder_editing_path;

    Window(int id, int x, int y, int w, int h, const char* title, uint32_t color);
    void draw(bool is_active);

    bool title_is(const char* s);
    bool title_starts_with(const char* s);
};

struct MenuItem {
    const char* label;   // Item text
    int action_id;       // Command ID dispatched on click
    int submenu_type;    // Nested menu type ID (-1 if none)
    bool enabled;        // Clickable state
};

struct ContextMenu {
    bool active;
    int x, y, w, h;
    int type;        // 0 = Apple, 1 = Finder/File, 2 = Desktop Right-Click, etc.
    int hovered_item;
};

struct SubMenu {
    bool active;
    int x, y, w, h;
    int type;        // Submenu type ID (e.g. 100 for New, 101 for Theme)
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
    static SubMenu active_submenu;

    static void focus_window(Window* win);

    // Internal WhiteSur helpers
    static void draw_wallpaper();
    static void draw_desktop_icons();
    static void arrange_desktop();
    static void draw_menu_bar();
    static void draw_dock();
    static void focus_or_create_app(const char* title, uint32_t bg_color, int w, int h);
    static void draw_menu();
    static void execute_menu_action(int action_id);
    static void remove_vfs_node(const char* path);
    static void delete_desktop_item(int sel);
    static void draw_window_shadow(const Rect& r, bool active, uint8_t alpha);
    static void draw_traffic_lights(Window* win, uint8_t alpha);
    static void draw_window_body(Window* win, uint8_t alpha, bool is_terminal);
    static void draw_terminal_content(Window* win);
    static void draw_finder_content(Window* win);
    static void populate_finder_cache(Window* win);
    static void rename_item(const char* parent_path, const char* old_name, const char* new_name);
    static void delete_finder_item(const char* parent_path, const char* name, bool is_directory);
    static void draw_system_log_content(Window* win);
    static void draw_perf_monitor_content(Window* win);
    static void draw_about_content(Window* win);
    static void draw_safari_content(Window* win);
    static void draw_mail_content(Window* win);
    static void draw_appstore_content(Window* win);
    static void draw_notes_content(Window* win);
    static void draw_code_editor_content(Window* win);
    static void draw_audio_player_content(Window* win);
    static void draw_picture_viewer_content(Window* win);
    static void load_sng_file(const char* path);
    static void load_wav_file(const char* path);
    static void audio_play_frequency(uint32_t freq);
    static void audio_stop();
    static void audio_tick();
    static void draw_drag_preview();
    static void draw_window_client_area(Window* win);
    static void minimize_window_animated(Window* win);

    static bool in_rect(int px, int py, int rx, int ry, int rw, int rh);
    static bool in_rect(int px, int py, const Rect& r);
    static void bring_to_front(Window* win);
    static void draw_all_windows();
    static void blit_cursor();
    static void update_hardware_cursor_fast();
    static void redraw_dirty_rect(const Rect& dirty);
    static void redraw_dirty_list(const DirtyList& list);

    static void int_to_str(int v, char* buf, int len);

    struct SongNote {
        uint32_t freq;
        uint32_t duration;
    };
    static bool audio_playing;
    static char audio_current_song[128];
    static SongNote audio_notes[256];
    static int audio_note_idx;
    static int audio_note_count;
    static uint32_t audio_note_end_frame;
    static int audio_visualizer_seed;

    static bool audio_is_wav;
    static uint8_t* audio_wav_data;
    static uint32_t audio_wav_size;
    static uint32_t audio_wav_offset;
    static uint32_t audio_wav_sample_rate;
    static uint16_t audio_wav_channels;
    static uint16_t audio_wav_bits_per_sample;
    static uint32_t audio_amplitude;
    static uint32_t audio_wav_phase;
    static uint8_t next_buffer_to_fill;
    static void fill_wav_buffer_slice(int buffer_idx);

    static int perf_cpu_history[20];
    static int perf_current_cpu;
    static int frame_counter;

public:
    static void init();
    static void draw_cursor();
    static void erase_cursor();
    static int get_string_width(const char* s, float size);
    static void draw_string(const char* s, int x, int y, uint32_t c, float size);
    static Window* create_window(int x, int y, int w, int h, const char* title, uint32_t color);
    static void close_window(int id);
    static Window* get_window_by_id(int id);

    static void draw_desktop();
    static void draw_mac_decorations();
    static void draw_taskbar();
    static void draw_start_menu();
    static void force_redraw_all();
    static void invalidate_window(int id);
    static void trigger_host_persist();
    static void handle_mouse_move(int new_x, int new_y, bool left_pressed, bool right_pressed);
    static void handle_key_press(char c);
    static void tick();
    static void draw_settings_content(Window* win);
    static bool is_drag_in_progress();

    static int get_mouse_x() { return mouse_x; }
    static int get_mouse_y() { return mouse_y; }

    // UI Scaling and Customization globals
    static float ui_scale;
    static bool dark_mode;
    static int wallpaper_theme_id;
    static int current_theme;

    static inline int scale_dim(int val) { return (int)(val * ui_scale); }
    static inline float scale_font(float size) { return size * ui_scale; }
    static void set_ui_scale(float scale);
    static void set_theme(int theme_id);
};

} // namespace wm
