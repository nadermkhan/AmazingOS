#include "wm.hpp"
#include "fs/vfs.hpp"
#include "../drivers/framebuffer.hpp"
#include "../drivers/serial.hpp"
#include "../drivers/ttf.hpp"

namespace wm {

// ---------------------------------------------------------------------------
// WhiteSur palette (all colours are 0x00RRGGBB)
// ---------------------------------------------------------------------------
static uint32_t C_WHITE        = 0x00FFFFFF;
static uint32_t C_BLACK        = 0x00000000;
static constexpr uint32_t C_TITLE_DARK   = 0x001A1A1A;
static uint32_t C_TEXT         = 0x00333333;
static uint32_t C_TEXT_MUTED   = 0x00666666;
static uint32_t C_MENU_BG      = 0x00F5F5F5;
static uint32_t C_MENU_LINE    = 0x00D2D2D2;
static uint32_t C_DOCK_BG      = 0x00EBEBEB;
static uint32_t C_DOCK_RIM     = 0x00FFFFFF;
static constexpr uint32_t C_HOVER        = 0x000F52BA;
static constexpr uint32_t C_CLOSE        = 0x00FF5F56;
static constexpr uint32_t C_MINIMIZE     = 0x00FFBD2E;
static constexpr uint32_t C_MAXIMIZE     = 0x0027C93F;
static constexpr uint32_t C_TERMINAL     = 0x001A1A1A;
static constexpr uint32_t C_FINDER       = 0x00FFFFFF;
static constexpr uint32_t C_FOLDER       = 0x0000A2C9;
static constexpr uint32_t C_FILE         = 0x00E0E0E0;

static int MENU_BAR_HEIGHT   = 32;
static int TITLE_BAR_HEIGHT  = 30;
static int TRAFFIC_Y         = 15;
static int TRAFFIC_R         = 6;
static int CORNER_RADIUS     = 12;
static int SHADOW_SIZE       = 16;
static int DOCK_HEIGHT       = 64;
static int DOCK_MARGIN_BOTTOM = 24;
static int DOCK_ICON_SIZE    = 48;
static int DOCK_ICON_SPACING = 36;
static int DOCK_WIDTH        = 640;
static constexpr int MENU_WIDTH        = 180;
static constexpr int MENU_ITEM_HEIGHT  = 26;
static constexpr int MENU_PADDING      = 4;

static int dragged_desktop_item_idx = -1;
static int click_start_x = 0;
static int click_start_y = 0;

static char safari_url[128] = "google.com";
static char safari_search_query[128] = "";
static bool safari_search_active = false;
static bool safari_focused_address = false;
static bool safari_focused_search = true;

static bool is_resizing_window = false;
static int resize_start_w = 0;
static int resize_start_h = 0;

struct LinkHitbox {
    Rect rect;
    char target[128];
};
static LinkHitbox safari_links[16];
static int safari_link_count = 0;

static int clipboard_item_idx = -1;
static bool clipboard_is_cut = false;
static bool is_renaming_item = false;

static uint32_t last_finder_click_time = 0;
static int last_finder_clicked_item_idx = -1;

static bool is_dragging_selection = false;

static int drag_preview_x = 0;
static int drag_preview_y = 0;
static int drag_preview_w = 0;
static int drag_preview_h = 0;
static char drag_preview_title[128] = "";

static bool wifi_connected = true;
static bool usb_connected = false;

static char terminal_lines[16][80] = {
    "OS: CrecentOS Whitesur 2.0 (Inter TTF)",
    "Kernel: x86_64 Freestanding Core",
    "Shell: Terminal (stb_truetype vector)",
    "Resolution: 1920x1080 (60 FPS BGA)",
    "Uptime: 2 mins",
    "",
    "Type 'help' to see available commands."
};
static int terminal_line_count = 7;

static void terminal_println(const char* line) {
    if (terminal_line_count >= 12) {
        for (int i = 0; i < 11; i++) {
            int l = 0;
            while (terminal_lines[i + 1][l]) {
                terminal_lines[i][l] = terminal_lines[i + 1][l];
                l++;
            }
            terminal_lines[i][l] = '\0';
        }
        terminal_line_count = 11;
    }
    int l = 0;
    while (line[l] && l < 79) {
        terminal_lines[terminal_line_count][l] = line[l];
        l++;
    }
    terminal_lines[terminal_line_count][l] = '\0';
    terminal_line_count++;
}

static bool title_starts_with_custom(const char* title, const char* prefix) {
    int i = 0;
    while (prefix[i]) {
        if (title[i] != prefix[i]) return false;
        i++;
    }
    return true;
}

// File-scoped cursor updater (fixes cache mismatch and OOB issues via physical VRAM rendering)
static void plot_cursor_to_backbuffer(int nx, int ny) {
    int w = drivers::Framebuffer::get_width();
    int h = drivers::Framebuffer::get_height();

    const char* cursor_grid[] = {
        "o                  ",
        "oo                 ",
        "oXo.               ",
        "oXXo.              ",
        "oXXXo.             ",
        "oXXXXo.            ",
        "oXXXXXo.           ",
        "oXXXXXXo.          ",
        "oXXXXXXXo.         ",
        "oXXXXXXXXo.        ",
        "oXXXXXXXXXo.       ",
        "oXXXXXXXXXXo.      ",
        "oXXXXXXXXXXXo.     ",
        "oXXXXXXoooooo.     ",
        "oXXoXXXo.x         ",
        "oXo.oXXXo.x        ",
        "oo. oXXXo.x        ",
        "o.   oXXXo.x       ",
        "     oXXXo.x       ",
        "      oXXXo.x      ",
        "      oXXXo.x      ",
        "       oXXo.x      ",
        "       ooo.x       ",
        "        ..x        ",
        "         xx        "
    };

    for (int y = 0; y < 25; ++y) {
        const char* row = cursor_grid[y];
        for (int x = 0; row[x] != '\0'; ++x) {
            char c = row[x];
            if (c == ' ') continue;

            int px = nx + x;
            int py = ny + y;
            if (px < 0 || px >= w || py < 0 || py >= h) continue;

            uint32_t color = 0;
            uint32_t alpha = 0;

            if (c == 'X') {
                color = 0x00FFFFFF;
                alpha = 255;
            } else if (c == 'o') {
                color = 0x00000000;
                alpha = 255;
            } else if (c == '.') {
                color = 0x00000000;
                alpha = 90;
            } else if (c == 'x') {
                color = 0x00000000;
                alpha = 40;
            } else {
                continue;
            }

            if (alpha == 255) {
                drivers::Framebuffer::draw_pixel(px, py, color);
            } else {
                // Highly optimized fast integer alpha blending (no floats, no divisions)
                uint32_t bg = drivers::Framebuffer::get_pixel(px, py);
                int bg_r = (bg >> 16) & 0xFF;
                int bg_g = (bg >> 8) & 0xFF;
                int bg_b = bg & 0xFF;

                int src_r = (color >> 16) & 0xFF;
                int src_g = (color >> 8) & 0xFF;
                int src_b = color & 0xFF;

                int out_r = bg_r + (((src_r - bg_r) * (int)alpha) >> 8);
                int out_g = bg_g + (((src_g - bg_g) * (int)alpha) >> 8);
                int out_b = bg_b + (((src_b - bg_b) * (int)alpha) >> 8);

                uint32_t blended = ((uint32_t)out_r << 16) | ((uint32_t)out_g << 8) | (uint32_t)out_b;
                drivers::Framebuffer::draw_pixel(px, py, blended);
            }
        }
    }
}


// ---------------------------------------------------------------------------
// Window implementation
// ---------------------------------------------------------------------------
Window::Window(int id, int x, int y, int w, int h, const char* title, uint32_t color) {
    this->id = id;
    this->rect = {x, y, w, h};
    this->orig_rect = {x, y, w, h};
    this->is_maximized = false;
    this->is_minimized = false;

    int i = 0;
    while (title[i] && i < 63) {
        this->title[i] = title[i];
        i++;
    }
    this->title[i] = '\0';

    this->bg_color = color;
    this->is_dragging = false;
    this->next = nullptr;
    this->buffer = nullptr;
    this->pending_event = {0, 0, 0, 0};
    this->text_len = 0;
    this->text_input[0] = '\0';
    this->selected_item_idx = -1;
}

bool Window::title_is(const char* s) {
    int i = 0;
    while (s[i] && this->title[i]) {
        if (this->title[i] != s[i]) return false;
        i++;
    }
    return this->title[i] == '\0' && s[i] == '\0';
}

bool Window::title_starts_with(const char* s) {
    int i = 0;
    while (s[i]) {
        if (this->title[i] != s[i]) return false;
        i++;
    }
    return true;
}

// Wallpaper helper removed in favor of direct Framebuffer::draw_mac_wallpaper call

struct DiskItem {
    char name[32];
    bool is_directory;
    bool is_terminal;
    const char* path; // "Desktop", "Applications", "Documents"
    int x, y; // Icon coordinates
    bool selected;
};

static DiskItem desktop_items[64];
static int desktop_item_count = 0;

static bool str_equal(const char* s1, const char* s2) {
    if (!s1 || !s2) return false;
    while (*s1 && *s2) {
        if (*s1 != *s2) return false;
        s1++; s2++;
    }
    return *s1 == *s2;
}

static void add_item(const char* name, bool is_dir, bool is_term, const char* path, int x, int y) {
    if (desktop_item_count >= 64) return;
    DiskItem& it = desktop_items[desktop_item_count++];
    int i = 0;
    while (name[i] && i < 31) { it.name[i] = name[i]; i++; }
    it.name[i] = '\0';
    it.is_directory = is_dir;
    it.is_terminal = is_term;
    it.path = path;
    it.x = x;
    it.y = y;
    it.selected = false;
}

void WindowManager::trigger_host_persist() {
    drivers::Serial::println("[PERSIST_BEGIN]");
    
    char f_buf[16];
    
    drivers::Serial::print("SCALE ");
    int scale_val = (int)(ui_scale * 100);
    int_to_str(scale_val, f_buf, 10);
    drivers::Serial::println(f_buf);
    
    drivers::Serial::print("THEME ");
    drivers::Serial::println(dark_mode ? "DARK" : "LIGHT");
    
    drivers::Serial::print("WALLPAPER ");
    int_to_str(wallpaper_theme_id, f_buf, 10);
    drivers::Serial::println(f_buf);
    
    for (int i = 0; i < desktop_item_count; i++) {
        DiskItem& it = desktop_items[i];
        if (str_equal(it.name, "USB_DRIVE") || str_equal(it.path, "USB_DRIVE")) continue;
        drivers::Serial::print("ITEM ");
        drivers::Serial::print(it.name);
        drivers::Serial::print(" ");
        drivers::Serial::print(it.is_directory ? "DIR" : (it.is_terminal ? "TERM" : "FILE"));
        drivers::Serial::print(" ");
        drivers::Serial::print(it.path);
        drivers::Serial::print(" ");
        int_to_str(it.x, f_buf, 10);
        drivers::Serial::print(f_buf);
        drivers::Serial::print(" ");
        int_to_str(it.y, f_buf, 10);
        drivers::Serial::println(f_buf);
    }
    
    for (size_t i = 0; i < fs::VFS::node_count; i++) {
        fs::VFSNode& node = fs::VFS::child_nodes[i];
        if (node.type == fs::NodeType::FILE && node.data && node.size > 0) {
            if (node.name[0] != '\0' && !title_starts_with_custom(node.name, "tar/")) {
                drivers::Serial::print("FILE ");
                drivers::Serial::print(node.name);
                drivers::Serial::print(" ");
                int_to_str(node.size, f_buf, 10);
                drivers::Serial::print(f_buf);
                drivers::Serial::print(" ");
                for (size_t j = 0; j < node.size; j++) {
                    char h_buf[3];
                    uint8_t val = (uint8_t)node.data[j];
                    const char* hex_digits = "0123456789ABCDEF";
                    h_buf[0] = hex_digits[val >> 4];
                    h_buf[1] = hex_digits[val & 0x0F];
                    h_buf[2] = '\0';
                    drivers::Serial::print(h_buf);
                }
                drivers::Serial::println("");
            }
        }
    }
    
    drivers::Serial::println("[PERSIST_END]");
}

void WindowManager::draw_drag_preview() {
}

// ---------------------------------------------------------------------------
// WindowManager static members
// ---------------------------------------------------------------------------
Window* WindowManager::window_list_head = nullptr;
int WindowManager::next_window_id = 1;
int WindowManager::mouse_x = 960;
int WindowManager::mouse_y = 540;
int WindowManager::last_mouse_x = 960;
int WindowManager::last_mouse_y = 540;
bool WindowManager::mouse_pressed = false;
bool WindowManager::right_mouse_pressed = false;
Window* WindowManager::active_window = nullptr;
int WindowManager::drag_offset_x = 0;
int WindowManager::drag_offset_y = 0;
ContextMenu WindowManager::active_menu = {false, 0, 0, 0, 0, 0, -1};
int WindowManager::perf_cpu_history[20] = {0};
int WindowManager::perf_current_cpu = 12;
int WindowManager::frame_counter = 0;
float WindowManager::ui_scale = 1.0f;
bool WindowManager::dark_mode = false;
int WindowManager::wallpaper_theme_id = 0;

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------
// Fix 5: Robust int_to_str handling boundary cases and INT_MIN securely
void WindowManager::int_to_str(int v, char* buf, int len) {
    if (len <= 0) return;
    if (len == 1) { buf[0] = '\0'; return; }

    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    char rev[16];
    int r = 0;
    bool neg = false;

    unsigned int uv;
    if (v < 0) {
        neg = true;
        uv = ~(unsigned int)v + 1; // Safe for INT_MIN
    } else {
        uv = (unsigned int)v;
    }

    while (uv > 0 && r < 15) {
        rev[r++] = '0' + (uv % 10);
        uv /= 10;
    }

    int idx = 0;
    if (neg && idx < len - 1) buf[idx++] = '-';
    while (r > 0 && idx < len - 1) {
        buf[idx++] = rev[--r];
    }
    buf[idx] = '\0';
}

int WindowManager::get_string_width(const char* s, float size) {
    return drivers::TtfRenderer::get_string_width(s, scale_font(size));
}

void WindowManager::draw_string(const char* s, int x, int y, uint32_t c, float size) {
    drivers::TtfRenderer::draw_string(s, x, y, c, scale_font(size));
}

bool WindowManager::in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

bool WindowManager::in_rect(int px, int py, const Rect& r) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
void WindowManager::draw_window_shadow(const Rect& r, bool active, uint8_t alpha) {
    // Skip shadow on any window if a drag or resize is in progress to maximize CPU frame rate
    bool drag_in_progress = (active_window && (active_window->is_dragging || is_resizing_window));
    if (drag_in_progress) return;

    int shadow_size = active ? 24 : 12;
    int step = active ? 4 : 2;
    bool dragging = (alpha < 255);
    for (int i = 0; i < shadow_size; i += step) {
        int a = (dragging ? 12 : (active ? 20 : 14)) - i;
        if (a < 0) a = 0;
        drivers::Framebuffer::draw_rounded_rect_alpha(
            r.x - i, r.y - i + (active ? 4 : 2),
            r.w + i * 2, r.h + i * 2,
            CORNER_RADIUS + i,
            C_BLACK, a);
    }
}

void WindowManager::draw_traffic_lights(Window* win, uint8_t alpha) {
    int x = win->rect.x;
    int y = win->rect.y;
    bool active = (win == active_window && !win->is_minimized);
    bool hover_controls = in_rect(mouse_x, mouse_y, x + 10, y + 5, 60, 20) && active;

    uint32_t c_close    = active ? C_CLOSE : 0x00CCCCCC;
    uint32_t c_minimize = active ? C_MINIMIZE : 0x00CCCCCC;
    uint32_t c_maximize = active ? C_MAXIMIZE : 0x00CCCCCC;

    if (win->is_dragging) {
        drivers::Framebuffer::draw_circle_filled_alpha(x + 19, y + TRAFFIC_Y, TRAFFIC_R, c_close, alpha);
        drivers::Framebuffer::draw_circle_filled_alpha(x + 39, y + TRAFFIC_Y, TRAFFIC_R, c_minimize, alpha);
        drivers::Framebuffer::draw_circle_filled_alpha(x + 59, y + TRAFFIC_Y, TRAFFIC_R, c_maximize, alpha);
    } else {
        drivers::Framebuffer::draw_circle_filled(x + 19, y + TRAFFIC_Y, TRAFFIC_R, c_close);
        drivers::Framebuffer::draw_circle_filled(x + 39, y + TRAFFIC_Y, TRAFFIC_R, c_minimize);
        drivers::Framebuffer::draw_circle_filled(x + 59, y + TRAFFIC_Y, TRAFFIC_R, c_maximize);
    }

    if (hover_controls && alpha == 255) {
        // Red cross
        int cx = x + 19, cy = y + TRAFFIC_Y;
        drivers::Framebuffer::draw_pixel(cx - 2, cy - 2, 0x00550000);
        drivers::Framebuffer::draw_pixel(cx - 1, cy - 1, 0x00550000);
        drivers::Framebuffer::draw_pixel(cx, cy, 0x00550000);
        drivers::Framebuffer::draw_pixel(cx + 1, cy + 1, 0x00550000);
        drivers::Framebuffer::draw_pixel(cx + 2, cy + 2, 0x00550000);
        drivers::Framebuffer::draw_pixel(cx - 2, cy + 2, 0x00550000);
        drivers::Framebuffer::draw_pixel(cx - 1, cy + 1, 0x00550000);
        drivers::Framebuffer::draw_pixel(cx + 1, cy - 1, 0x00550000);
        drivers::Framebuffer::draw_pixel(cx + 2, cy - 2, 0x00550000);

        // Yellow dash
        cx = x + 39;
        for (int dx = -2; dx <= 2; ++dx) {
            drivers::Framebuffer::draw_pixel(cx + dx, cy, 0x005C4300);
        }

        // Green plus
        cx = x + 59;
        for (int dx = -2; dx <= 2; ++dx) {
            drivers::Framebuffer::draw_pixel(cx + dx, cy, 0x000A4A00);
        }
        for (int dy = -2; dy <= 2; ++dy) {
            drivers::Framebuffer::draw_pixel(cx, cy + dy, 0x000A4A00);
        }
    }
}

// Fix 10 & 11: Matched header signature. is_active is determined internally. Translucency restricted to dragging.
void WindowManager::draw_window_body(Window* win, uint8_t alpha, bool is_terminal) {
    (void)is_terminal;
    const Rect& r = win->rect;
    uint8_t body_alpha = alpha; 

    bool active = (win == active_window && !win->is_minimized);
    uint32_t frame_bg = dark_mode ? 0x000F172A : 0x00D0D0D0;
    uint32_t title_bg = dark_mode ? (active ? 0x001E293B : 0x000F172A) : (active ? 0x00E8E8E8 : 0x00F4F4F4);

    // Outer frame (Base Layer)
    drivers::Framebuffer::draw_rounded_rect_alpha(r.x, r.y, r.w, r.h, CORNER_RADIUS, frame_bg, alpha);

    drivers::Framebuffer::draw_rounded_rect_alpha(r.x + 1, r.y + 1, r.w - 2, 26, 10, title_bg, alpha);
    drivers::Framebuffer::draw_rect_alpha(r.x + 1, r.y + 18, r.w - 2, 14, title_bg, alpha);

    float title_font = 15.0f;
    int title_w = get_string_width(win->title, title_font);
    int title_x = r.x + (r.w - title_w) / 2;
    draw_string(win->title, title_x, r.y + 7, C_TEXT, title_font);

    draw_traffic_lights(win, alpha);

    drivers::Framebuffer::draw_rounded_rect_alpha(
        r.x + 1, r.y + r.h - 22, r.w - 2, 21, 10, win->bg_color, body_alpha);
    drivers::Framebuffer::draw_rect_alpha(
        r.x + 1, r.y + TITLE_BAR_HEIGHT, r.w - 2, r.h - TITLE_BAR_HEIGHT - 12,
        win->bg_color, body_alpha);

    // Resize grip affordance (bottom-right corner dots)
    if (!win->is_maximized && !win->is_minimized) {
        int rx = r.x + r.w - 14;
        int ry = r.y + r.h - 14;
        uint32_t grip_c = dark_mode ? 0x00475569 : 0x00B0B0B0;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j <= i; j++) {
                drivers::Framebuffer::draw_pixel(rx + j*4, ry - (i-j)*4, grip_c);
                drivers::Framebuffer::draw_pixel(rx + j*4 + 1, ry - (i-j)*4, grip_c);
                drivers::Framebuffer::draw_pixel(rx + j*4, ry - (i-j)*4 + 1, grip_c);
                drivers::Framebuffer::draw_pixel(rx + j*4 + 1, ry - (i-j)*4 + 1, grip_c);
            }
        }
    }
}

void WindowManager::draw_terminal_content(Window* win) {
    int x = win->rect.x;
    int y = win->rect.y;
    const int prompt_y = 40;
    const int line_h = 22;
    
    // Draw terminal history lines
    for (int i = 0; i < terminal_line_count; i++) {
        draw_string(terminal_lines[i], x + 12, y + prompt_y + i * line_h, 0x00D0D0D0, 15.0f);
    }
    
    // Draw active input line at the bottom
    char prompt_line[300] = "crecent@macos:~$ ";
    int p_idx = 17;
    for (int i = 0; i < win->text_len; i++) {
        prompt_line[p_idx++] = win->text_input[i];
    }
    
    // Blinking cursor
    bool cursor_visible = (frame_counter / 20) % 2 == 0;
    if (cursor_visible) {
        prompt_line[p_idx++] = '_';
    }
    prompt_line[p_idx] = '\0';
    
    draw_string(prompt_line, x + 12, y + prompt_y + terminal_line_count * line_h, C_WHITE, 15.0f);
}

void WindowManager::draw_finder_content(Window* win) {
    int x = win->rect.x;
    int y = win->rect.y;
    int w = win->rect.w;
    int h = win->rect.h;
    
    const char* active_dir = win->text_input;
    if (win->text_len == 0) {
        active_dir = "Desktop";
    }
    
    // Draw Finder Sidebar
    uint32_t sidebar_bg = dark_mode ? 0x000F172A : 0x00F5F5F5;
    uint32_t content_bg = dark_mode ? 0x001E293B : 0x00FFFFFF;
    uint32_t border_c = dark_mode ? 0x00334155 : 0x00D2D2D2;
    
    drivers::Framebuffer::draw_rect_alpha(x + 1, y + TITLE_BAR_HEIGHT, 140, h - TITLE_BAR_HEIGHT - 12, sidebar_bg, 255);
    drivers::Framebuffer::draw_rect_alpha(x + 141, y + TITLE_BAR_HEIGHT, 1, h - TITLE_BAR_HEIGHT - 12, border_c, 255);
    drivers::Framebuffer::draw_rect_alpha(x + 142, y + TITLE_BAR_HEIGHT, w - 143, h - TITLE_BAR_HEIGHT - 12, content_bg, 255);

    // Draw Toolbar Area (ry: 0 to 35)
    int toolbar_y = y + TITLE_BAR_HEIGHT;
    drivers::Framebuffer::draw_rect_alpha(x + 142, toolbar_y, w - 143, 35, sidebar_bg, 255);
    drivers::Framebuffer::draw_rect_alpha(x + 142, toolbar_y + 34, w - 143, 1, border_c, 255);

    // Back Button
    drivers::Framebuffer::draw_rounded_rect_alpha(x + 152, toolbar_y + 5, 60, 24, 6, 0x00E0E0E0, 255);
    draw_string("Back", x + 168, toolbar_y + 10, C_TEXT, 13.0f);

    // Current Path Bar
    draw_string("Path: /", x + 225, toolbar_y + 10, C_TEXT_MUTED, 13.0f);
    draw_string(active_dir, x + 270, toolbar_y + 10, C_TEXT, 13.0f);

    // + Folder Button
    drivers::Framebuffer::draw_rounded_rect_alpha(x + w - 180, toolbar_y + 5, 80, 24, 6, 0x00E0E0E0, 255);
    draw_string("+ Folder", x + w - 168, toolbar_y + 10, C_TEXT, 13.0f);

    // + File Button
    drivers::Framebuffer::draw_rounded_rect_alpha(x + w - 90, toolbar_y + 5, 80, 24, 6, 0x00E0E0E0, 255);
    draw_string("+ File", x + w - 74, toolbar_y + 10, C_TEXT, 13.0f);

    // Sidebar items
    draw_string("Favorites", x + 15, y + TITLE_BAR_HEIGHT + 15, C_TEXT_MUTED, 12.0f);
    
    const char* favorites[] = {"Desktop", "Applications", "Documents"};
    for (int i = 0; i < 3; i++) {
        int iy = y + TITLE_BAR_HEIGHT + 40 + i * 30;
        bool is_selected = str_equal(active_dir, favorites[i]);
        if (is_selected) {
            drivers::Framebuffer::draw_rounded_rect_alpha(x + 8, iy - 4, 124, 24, 6, C_HOVER, 255);
        }
        uint32_t tc = is_selected ? C_WHITE : C_TEXT;
        draw_string(favorites[i], x + 18, iy, tc, 14.0f);
    }
    
    // Draw files in the active directory
    int grid_col = 0;
    int grid_row = 0;
    for (int i = 0; i < desktop_item_count; i++) {
        DiskItem& it = desktop_items[i];
        if (str_equal(it.path, active_dir)) {
            int ix = x + 170 + grid_col * 90;
            int iy = y + TITLE_BAR_HEIGHT + 55 + grid_row * 90;
            
            bool is_item_selected = (win->selected_item_idx == i);
            if (is_item_selected) {
                drivers::Framebuffer::draw_rounded_rect_alpha(ix, iy - 5, 64, 80, 8, 0x0080B0F0, 100);
            }

            uint32_t icon_c = it.is_directory ? C_FOLDER : (it.is_terminal ? 0x001A1A1A : C_FILE);
            drivers::Framebuffer::draw_rounded_rect_alpha(ix + 12, iy, 40, 40, 8, icon_c, 255);
            
            if (it.is_directory) {
                int lw = get_string_width("F", 14.0f);
                draw_string("F", ix + 12 + (40 - lw)/2, iy + 13, C_WHITE, 14.0f);
            } else if (it.is_terminal) {
                int lw = get_string_width("T", 14.0f);
                draw_string("T", ix + 12 + (40 - lw)/2, iy + 13, 0x004AF02C, 14.0f);
            }
            
            if (is_item_selected && is_renaming_item) {
                drivers::Framebuffer::draw_rounded_rect_alpha(ix - 10, iy + 42, 84, 20, 4, C_WHITE, 255);
                drivers::Framebuffer::draw_rounded_rect_alpha(ix - 10, iy + 42, 84, 20, 4, 0x000F52BA, 255);
                
                char rename_disp[64];
                int r_l = 0;
                while (it.name[r_l] && r_l < 60) { rename_disp[r_l] = it.name[r_l]; r_l++; }
                if ((frame_counter / 20) % 2 == 0) { rename_disp[r_l++] = '|'; }
                rename_disp[r_l] = '\0';
                
                int label_w = get_string_width(rename_disp, 12.0f);
                int label_x = ix + (64 - label_w) / 2;
                draw_string(rename_disp, label_x, iy + 46, C_TEXT, 12.0f);
            } else {
                int label_w = get_string_width(it.name, 13.0f);
                int label_x = ix + (64 - label_w) / 2;
                draw_string(it.name, label_x, iy + 45, C_TEXT, 13.0f);
            }
            
            grid_col++;
            if (grid_col >= 3) {
                grid_col = 0;
                grid_row++;
            }
        }
    }
}

void WindowManager::draw_system_log_content(Window* win) {
    int x = win->rect.x + 12;
    int y = win->rect.y + 40;
    const int lh = 22;

    uint32_t bg = win->bg_color;
    uint8_t br = (bg >> 16) & 0xFF;
    uint8_t bg_g = (bg >> 8) & 0xFF;
    uint8_t bb = bg & 0xFF;
    bool is_dark_bg = ((br + bg_g + bb) < 380);
    uint32_t c_muted = is_dark_bg ? 0x0094A3B8 : C_TEXT_MUTED;

    float s = 14.0f;
    draw_string("[0.000] Kernel initialized.", x, y, c_muted, s);
    draw_string("[0.012] VFS Root '/' mounted successfully.", x, y + lh, c_muted, s);
    draw_string("[0.045] TarFS loaded from initrd.", x, y + lh*2, c_muted, s);
    draw_string("[0.078] Found TrueType font: Inter.", x, y + lh*3, c_muted, s);
    draw_string("[0.120] Framebuffer compositor active.", x, y + lh*4, c_muted, s);
    draw_string("[0.125] Starting Window Manager...", x, y + lh*5, c_muted, s);
    draw_string("[0.130] Ready.", x, y + lh*6, C_MAXIMIZE, s);
}

void WindowManager::draw_perf_monitor_content(Window* win) {
    int x = win->rect.x + 16;
    int y = win->rect.y + 40;
    
    uint32_t bg = win->bg_color;
    uint8_t br = (bg >> 16) & 0xFF;
    uint8_t bg_g = (bg >> 8) & 0xFF;
    uint8_t bb = bg & 0xFF;
    bool is_dark_bg = ((br + bg_g + bb) < 380);
    uint32_t c_text = is_dark_bg ? 0x00F1F5F9 : C_TEXT;

    char cpu_str[64] = "CPU Usage:  ";
    char num_buf[16];
    int_to_str(perf_current_cpu, num_buf, 16);
    int c_idx = 12;
    for (int i = 0; num_buf[i]; i++) cpu_str[c_idx++] = num_buf[i];
    cpu_str[c_idx++] = '%';
    cpu_str[c_idx++] = ' ';
    cpu_str[c_idx++] = ' ';
    cpu_str[c_idx++] = '[';
    int bars = perf_current_cpu / 10;
    for (int i = 0; i < 10; i++) {
        cpu_str[c_idx++] = (i < bars) ? '|' : ' ';
    }
    cpu_str[c_idx++] = ']';
    cpu_str[c_idx] = '\0';

    draw_string(cpu_str, x, y, c_text, 15.0f);
    draw_string("Memory:     42MB / 512MB", x, y + 25, c_text, 15.0f);
    draw_string("VRAM:       16MB / 64MB", x, y + 50, c_text, 15.0f);
    draw_string("Tasks:      5 Running, 12 Sleeping", x, y + 75, c_text, 15.0f);
    
    char uptime_str[64] = "Uptime:     00:02:";
    int secs = frame_counter / 60;
    int_to_str(secs % 60, num_buf, 16);
    int u_idx = 18;
    if (secs % 60 < 10) uptime_str[u_idx++] = '0';
    for (int i = 0; num_buf[i]; i++) uptime_str[u_idx++] = num_buf[i];
    uptime_str[u_idx] = '\0';
    draw_string(uptime_str, x, y + 100, c_text, 15.0f);
    
    int gx = x;
    int gy = y + 140;
    uint32_t graph_bg = is_dark_bg ? 0x00334155 : 0x00E0E0E0;
    drivers::Framebuffer::draw_rect_alpha(gx, gy, 200, 60, graph_bg, 255);
    for (int i = 0; i < 20; i++) {
        int h = perf_cpu_history[i];
        if (h > 50) h = 50;
        drivers::Framebuffer::draw_rect_alpha(gx + i * 10, gy + 60 - h, 8, h, C_HOVER, 180);
    }
}

void WindowManager::draw_about_content(Window* win) {
    int x = win->rect.x;
    int y = win->rect.y;
    uint32_t bg = win->bg_color;

    // Vector Apple Logo
    drivers::Framebuffer::draw_circle_filled(x + 60, y + 100, 18, 0x00333333);
    drivers::Framebuffer::draw_circle_filled(x + 76, y + 100, 18, 0x00333333);
    drivers::Framebuffer::draw_circle_filled(x + 94, y + 96, 7, bg); // Bite
    drivers::Framebuffer::draw_circle_filled(x + 72, y + 78, 6, 0x00333333);  // Leaf

    // Details
    draw_string("Crecent OS", x + 130, y + 55, C_TEXT, 22.0f);
    draw_string("Version 14.2 (Sonoma Mock)", x + 130, y + 82, C_TEXT_MUTED, 14.0f);
    draw_string("Processor: 4.03 GHz Virtual x86_64", x + 130, y + 115, C_TEXT, 14.0f);
    draw_string("Memory: 512 MB 2666 MHz LPDDR4", x + 130, y + 135, C_TEXT, 14.0f);
    draw_string("Graphics: Bochs/QEMU BGA 64 MB", x + 130, y + 155, C_TEXT, 14.0f);
    draw_string("Serial Number: C02Y8888XX8X", x + 130, y + 175, C_TEXT_MUTED, 13.0f);

    drivers::Framebuffer::draw_rounded_rect_alpha(x + 130, y + 210, 110, 24, 6, 0x00E0E0E0, 255);
    draw_string("System Report...", x + 142, y + 215, C_TEXT, 13.0f);
}

static char vfs_html_buf[1024];

static const char* get_html_for_url(const char* url) {
    if (!wifi_connected && !title_starts_with_custom(url, "/")) {
        return "<h1>No Internet Connection</h1><hr><p>Safari cannot open the page because your device is not connected to the internet.</p><br><p>Please click the <b>Wifi: Off</b> status button in the top-right menu bar to reconnect!</p>";
    }
    if (str_equal(url, "apple.com") || str_equal(url, "www.apple.com")) {
        return "<h1>Apple</h1><hr><p>Titanium. So strong. So light. So Pro.</p><br><p><a href=\"github.com\">View GitHub projects</a></p><br><p><a href=\"crecent.org\">Crecent OS Project site</a></p><br><p><a href=\"google.com\">Back to Google</a></p>";
    } else if (str_equal(url, "github.com") || str_equal(url, "www.github.com")) {
        return "<h1>GitHub</h1><hr><h2>Let's build from here</h2><p>The world's leading AI-powered developer platform.</p><br><p><a href=\"crecent.org\">Crecent OS Project site</a></p><br><p><a href=\"google.com\">Search Google</a></p>";
    } else if (str_equal(url, "crecent.org") || str_equal(url, "www.crecent.org")) {
        return "<h1>Crecent OS Project</h1><hr><h2>Freestanding Core</h2><p>A modular operating system built on x86_64, using a custom Slab Allocator heap and standard STB TrueType fonts.</p><br><p><a href=\"apple.com\">Check Apple details</a></p>";
    } else if (title_starts_with_custom(url, "/")) {
        fs::VFSNode* node = fs::VFS::open(url);
        if (node) {
            fs::File f;
            f.node = node;
            f.offset = 0;
            ssize_t bytes = fs::VFS::read(&f, vfs_html_buf, sizeof(vfs_html_buf) - 1);
            if (bytes > 0) {
                vfs_html_buf[bytes] = '\0';
                return vfs_html_buf;
            }
        }
        return "<h1>File Not Found</h1><hr><p>Safari cannot open the local file path.</p>";
    }
    return nullptr;
}

static void render_html(const char* html_content, int x, int y, int w, int h) {
    int cur_x = x + 15;
    int cur_y = y + 80;
    
    int i = 0;
    bool in_tag = false;
    char tag_name[32] = "";
    int tag_len = 0;
    
    bool is_h1 = false;
    bool is_h2 = false;
    bool is_link = false;
    char link_target[128] = "";
    
    char text_buf[256];
    int text_idx = 0;
    
    safari_link_count = 0; // Reset active links

    auto flush_text = [&]() {
        if (text_idx > 0) {
            text_buf[text_idx] = '\0';
            uint32_t color = C_TEXT;
            float size = 14.0f;
            if (is_h1) { size = 24.0f; color = C_TEXT; }
            else if (is_h2) { size = 18.0f; color = C_TEXT; }
            else if (is_link) { size = 14.0f; color = 0x000F52BA; } // Blue link
            
            int tw = WindowManager::get_string_width(text_buf, size);
            if (cur_x + tw > x + w - 25) {
                cur_y += (int)(size + 6);
                cur_x = x + 15;
            }

            WindowManager::draw_string(text_buf, cur_x, cur_y, color, size);
            
            if (is_link) {
                if (safari_link_count < 16) {
                    safari_links[safari_link_count] = {
                        { cur_x, cur_y - 2, tw, (int)size + 6 },
                        ""
                    };
                    int l_idx = 0;
                    while (link_target[l_idx]) {
                        safari_links[safari_link_count].target[l_idx] = link_target[l_idx];
                        l_idx++;
                    }
                    safari_links[safari_link_count].target[l_idx] = '\0';
                    safari_link_count++;
                }
            }
            
            cur_x += tw + 4;
            text_idx = 0;
        }
    };

    while (html_content[i]) {
        char c = html_content[i];
        if (c == '<') {
            flush_text();
            in_tag = true;
            tag_len = 0;
        } else if (c == '>') {
            tag_name[tag_len] = '\0';
            in_tag = false;
            
            if (str_equal(tag_name, "h1")) { is_h1 = true; cur_x = x + 15; cur_y += 10; }
            else if (str_equal(tag_name, "/h1")) { flush_text(); is_h1 = false; cur_y += 32; cur_x = x + 15; }
            else if (str_equal(tag_name, "h2")) { is_h2 = true; cur_x = x + 15; cur_y += 8; }
            else if (str_equal(tag_name, "/h2")) { flush_text(); is_h2 = false; cur_y += 26; cur_x = x + 15; }
            else if (str_equal(tag_name, "p")) { cur_x = x + 15; }
            else if (str_equal(tag_name, "/p")) { flush_text(); cur_y += 22; cur_x = x + 15; }
            else if (title_starts_with_custom(tag_name, "a ")) {
                is_link = true;
                int h_idx = 0;
                while (tag_name[h_idx] && !title_starts_with_custom(tag_name + h_idx, "href=\"")) h_idx++;
                if (tag_name[h_idx]) {
                    h_idx += 6;
                    int t_idx = 0;
                    while (tag_name[h_idx] && tag_name[h_idx] != '\"' && t_idx < 120) {
                        link_target[t_idx++] = tag_name[h_idx++];
                    }
                    link_target[t_idx] = '\0';
                }
            }
            else if (str_equal(tag_name, "/a")) { flush_text(); is_link = false; }
            else if (str_equal(tag_name, "br")) { flush_text(); cur_y += 20; cur_x = x + 15; }
            else if (str_equal(tag_name, "hr")) {
                flush_text();
                drivers::Framebuffer::draw_rect_alpha(x + 15, cur_y + 10, w - 30, 1, 0x00E0E0E0, 255);
                cur_y += 22;
                cur_x = x + 15;
            }
        } else {
            if (in_tag) {
                if (tag_len < 31) tag_name[tag_len++] = c;
            } else {
                if (text_idx < 250) text_buf[text_idx++] = c;
            }
        }
        i++;
    }
    flush_text();
}

void WindowManager::draw_safari_content(Window* win) {
    int x = win->rect.x;
    int y = win->rect.y;
    int w = win->rect.w;
    int h = win->rect.h;

    // Navigation bar background
    drivers::Framebuffer::draw_rect_alpha(x + 1, y + 30, w - 2, 36, 0x00E8E8E8, 255);
    drivers::Framebuffer::draw_rect_alpha(x + 1, y + 66, w - 2, 1, 0x00D0D0D0, 255);

    // Navigation buttons
    draw_string("<", x + 16, y + 40, C_TEXT_MUTED, 16.0f);
    draw_string(">", x + 36, y + 40, C_TEXT_MUTED, 16.0f);
    draw_string("O", x + 56, y + 40, C_TEXT, 14.0f); // Reload

    // Address Bar (Highlight if focused)
    uint32_t addr_border = safari_focused_address ? 0x000F52BA : 0x00D0D0D0;
    drivers::Framebuffer::draw_rounded_rect_alpha(x + 85, y + 37, w - 110, 22, 6, addr_border, 255);
    drivers::Framebuffer::draw_rounded_rect_alpha(x + 86, y + 38, w - 112, 20, 5, C_WHITE, 255);
    
    // Address bar text (with blinking cursor if focused)
    char addr_str[150];
    int a_len = 0;
    while (safari_url[a_len]) { addr_str[a_len] = safari_url[a_len]; a_len++; }
    if (safari_focused_address && (frame_counter / 20) % 2 == 0) {
        addr_str[a_len++] = '|';
    }
    addr_str[a_len] = '\0';
    draw_string(addr_str, x + 95, y + 41, C_TEXT, 13.0f);

    // Client Area Background
    drivers::Framebuffer::draw_rect_alpha(x + 1, y + 67, w - 2, h - 67 - 12, C_WHITE, 255);

    // Render page
    const char* html = get_html_for_url(safari_url);
    if (html) {
        render_html(html, x, y, w, h);
    } else {
        int cx = x + w / 2;
        if (str_equal(safari_url, "google.com") || str_equal(safari_url, "www.google.com")) {
            // Draw Google Logo
            draw_string("G", cx - 65, y + 130, 0x004285F4, 42.0f);
            draw_string("o", cx - 35, y + 130, 0x00EA4335, 42.0f);
            draw_string("o", cx - 10, y + 130, 0x00FBBC05, 42.0f);
            draw_string("g", cx + 15, y + 130, 0x004285F4, 42.0f);
            draw_string("l", cx + 38, y + 130, 0x0034A853, 42.0f);
            draw_string("e", cx + 50, y + 130, 0x00EA4335, 42.0f);

            // Search bar (Highlight if focused)
            uint32_t search_border = safari_focused_search ? 0x004285F4 : 0x00D0D0D0;
            drivers::Framebuffer::draw_rounded_rect_alpha(cx - 160, y + 195, 320, 30, 15, search_border, 255);
            drivers::Framebuffer::draw_rounded_rect_alpha(cx - 159, y + 196, 318, 28, 14, C_WHITE, 255);

            // Search text
            if (safari_search_query[0] == '\0' && !safari_focused_search) {
                draw_string("Search Google or type a URL", cx - 100, y + 203, C_TEXT_MUTED, 14.0f);
            } else {
                char query_str[150];
                int q_len = 0;
                while (safari_search_query[q_len]) { query_str[q_len] = safari_search_query[q_len]; q_len++; }
                if (safari_focused_search && (frame_counter / 20) % 2 == 0) {
                    query_str[q_len++] = '|';
                }
                query_str[q_len] = '\0';
                draw_string(query_str, cx - 140, y + 203, C_TEXT, 14.0f);
            }
        } else if (safari_search_active || str_equal(safari_url, "google.com/search") || title_starts_with_custom(safari_url, "google.com/search")) {
            // Draw Search Header
            draw_string("Google", x + 20, y + 90, 0x004285F4, 20.0f);
            drivers::Framebuffer::draw_rounded_rect_alpha(x + 110, y + 85, 300, 26, 13, 0x00E0E0E0, 255);
            draw_string(safari_search_query, x + 125, y + 90, C_TEXT, 14.0f);
            drivers::Framebuffer::draw_rect_alpha(x + 1, y + 125, w - 2, 1, 0x00E0E0E0, 255);

            // Draw Search Results
            draw_string("Search results for:", x + 20, y + 145, C_TEXT_MUTED, 13.0f);
            draw_string(safari_search_query, x + 145, y + 145, 0x000F52BA, 13.0f);

            draw_string("1. Crecent OS on GitHub", x + 20, y + 180, 0x000F52BA, 16.0f);
            draw_string("https://github.com/NiO/AmazingOS", x + 20, y + 200, 0x0034A853, 12.0f);
            draw_string("The main repository containing CrecentOS source code...", x + 20, y + 218, C_TEXT, 13.0f);

            draw_string("2. Apple Sonoma features", x + 20, y + 250, 0x000F52BA, 16.0f);
            draw_string("https://apple.com/macos/sonoma", x + 20, y + 270, 0x0034A853, 12.0f);
            draw_string("Apple officially showcases macOS Sonoma highlights...", x + 20, y + 288, C_TEXT, 13.0f);
        } else {
            // Fallback site
            draw_string("HTTP 404 - Not Found", cx - 90, y + 120, 0x00EA4335, 18.0f);
            draw_string("Safari cannot open the page because it could not connect to", cx - 190, y + 160, C_TEXT_MUTED, 13.0f);
            draw_string(safari_url, cx - get_string_width(safari_url, 13.0f)/2, y + 180, C_TEXT, 13.0f);
        }
    }
}

void WindowManager::draw_mail_content(Window* win) {
    int x = win->rect.x;
    int y = win->rect.y;
    int w = win->rect.w;
    int h = win->rect.h;

    // Split Pane Sidebar
    drivers::Framebuffer::draw_rect_alpha(x + 1, y + 30, 160, h - 42, 0x00F3F3F3, 255);
    drivers::Framebuffer::draw_rect_alpha(x + 160, y + 30, 1, h - 42, 0x00D0D0D0, 255);

    // Mailboxes
    draw_string("Inbox", x + 16, y + 50, C_TEXT, 15.0f);
    drivers::Framebuffer::draw_rounded_rect_alpha(x + 115, y + 49, 28, 16, 8, 0x000F52BA, 255);
    draw_string("2", x + 125, y + 50, C_WHITE, 12.0f);

    draw_string("Sent", x + 16, y + 78, C_TEXT, 15.0f);
    draw_string("Drafts", x + 16, y + 106, C_TEXT, 15.0f);
    draw_string("Archive", x + 16, y + 134, C_TEXT, 15.0f);
    draw_string("Trash", x + 16, y + 162, C_TEXT, 15.0f);

    // Inbox Item list (top right)
    drivers::Framebuffer::draw_rect_alpha(x + 161, y + 30, w - 162, 90, C_WHITE, 255);
    draw_string("Steve Jobs", x + 176, y + 42, C_TEXT, 15.0f);
    draw_string("The new keyboard mockup designs", x + 176, y + 62, C_TEXT_MUTED, 13.0f);
    draw_string("Yesterday", x + w - 85, y + 42, C_TEXT_MUTED, 12.0f);

    drivers::Framebuffer::draw_rect_alpha(x + 161, y + 120, w - 162, 1, 0x00E5E5E5, 255);

    // Reading pane (bottom right)
    drivers::Framebuffer::draw_rect_alpha(x + 161, y + 121, w - 162, h - 133, 0x00FAFAFA, 255);
    draw_string("From: Steve Jobs <steve@apple.com>", x + 176, y + 140, C_TEXT, 14.0f);
    draw_string("Subject: The new keyboard mockup designs", x + 176, y + 160, C_TEXT, 14.0f);
    draw_string("Hey Team,", x + 176, y + 195, C_TEXT, 14.0f);
    draw_string("The vector render logic looks amazing. Let's push this", x + 176, y + 215, C_TEXT, 14.0f);
    draw_string("to the production build immediately.", x + 176, y + 235, C_TEXT, 14.0f);
    draw_string("Cheers, Steve", x + 176, y + 270, C_TEXT, 14.0f);
}

void WindowManager::draw_appstore_content(Window* win) {
    int x = win->rect.x;
    int y = win->rect.y;
    int w = win->rect.w;
    int h = win->rect.h;

    // Sidebar
    drivers::Framebuffer::draw_rect_alpha(x + 1, y + 30, 140, h - 42, 0x00F8F8F8, 255);
    drivers::Framebuffer::draw_rect_alpha(x + 140, y + 30, 1, h - 42, 0x00E0E0E0, 255);

    draw_string("Discover", x + 18, y + 55, C_HOVER, 15.0f);
    draw_string("Create", x + 18, y + 85, C_TEXT, 15.0f);
    draw_string("Work", x + 18, y + 115, C_TEXT, 15.0f);
    draw_string("Play", x + 18, y + 145, C_TEXT, 15.0f);
    draw_string("Develop", x + 18, y + 175, C_TEXT, 15.0f);
    draw_string("Updates", x + 18, y + 205, C_TEXT, 15.0f);

    // Main App grid
    drivers::Framebuffer::draw_rect_alpha(x + 141, y + 30, w - 142, h - 42, C_WHITE, 255);
    draw_string("Create Anything", x + 165, y + 48, C_TEXT, 20.0f);

    struct AppCard { const char* name; const char* cat; uint32_t icon_c; const char* icon_txt; };
    AppCard apps[] = {
        {"Xcode", "Developer Tools", 0x001B72E8, "X"},
        {"Logic Pro", "Music Production", 0x0034A853, "L"},
        {"Keynote", "Productivity", 0x00FBBC05, "K"},
        {"Final Cut", "Video Editing", 0x00EA4335, "F"}
    };

    for (int i = 0; i < 4; i++) {
        int ax = x + 165 + (i % 2) * 165;
        int ay = y + 90 + (i / 2) * 110;

        drivers::Framebuffer::draw_rounded_rect_alpha(ax, ay, 48, 48, 12, apps[i].icon_c, 255);
        int lw = get_string_width(apps[i].icon_txt, 22.0f);
        draw_string(apps[i].icon_txt, ax + (48 - lw)/2, ay + 12, C_WHITE, 22.0f);

        draw_string(apps[i].name, ax + 60, ay + 6, C_TEXT, 14.0f);
        draw_string(apps[i].cat, ax + 60, ay + 24, C_TEXT_MUTED, 12.0f);

        drivers::Framebuffer::draw_rounded_rect_alpha(ax + 60, ay + 38, 50, 18, 9, 0x00F0F0F0, 255);
        draw_string("GET", ax + 73, ay + 41, 0x000F52BA, 11.0f);
    }
}

void WindowManager::draw_notes_content(Window* win) {
    int x = win->rect.x;
    int y = win->rect.y;
    int w = win->rect.w;
    int h = win->rect.h;

    // Sidebar (Folders)
    drivers::Framebuffer::draw_rect_alpha(x + 1, y + 30, 130, h - 42, 0x00F5EFE0, 255);
    drivers::Framebuffer::draw_rect_alpha(x + 130, y + 30, 1, h - 42, 0x00D9CFB6, 255);

    draw_string("Folders", x + 12, y + 45, C_TEXT_MUTED, 13.0f);
    draw_string("Notes", x + 12, y + 70, C_TEXT, 15.0f);
    draw_string("Quick Notes", x + 12, y + 95, C_TEXT, 15.0f);
    draw_string("Personal", x + 12, y + 120, C_TEXT, 15.0f);

    // Notepad grid texture (right area)
    drivers::Framebuffer::draw_rect_alpha(x + 131, y + 30, w - 132, h - 42, 0x00FFFCEB, 255); // Pale yellow paper
    
    // Draw ruled lines
    int line_spacing = 22;
    int num_lines = (h - 42) / line_spacing;
    for (int i = 1; i <= num_lines; i++) {
        drivers::Framebuffer::draw_rect_alpha(x + 131, y + 30 + i * line_spacing, w - 132, 1, 0x00E9DEC0, 255);
    }
    // Margin line (Red)
    drivers::Framebuffer::draw_rect_alpha(x + 165, y + 30, 1, h - 42, 0x00FFA6A6, 255);

    // Note handwritten-style mockup text
    draw_string("Crecent Notes", x + 175, y + 45, C_TEXT, 15.0f);

    char notes_line[300];
    int n_idx = 0;
    for (int i = 0; i < win->text_len; i++) {
        notes_line[n_idx++] = win->text_input[i];
    }
    
    // Blinking cursor
    bool cursor_visible = (frame_counter / 20) % 2 == 0;
    if (cursor_visible) {
        notes_line[n_idx++] = '|';
    }
    notes_line[n_idx] = '\0';
    
    draw_string(notes_line, x + 175, y + 67, C_TEXT_MUTED, 14.0f);
}

static void draw_syntax_highlighted_line(const char* line, int x_start, int y_start) {
    int cur_x = x_start;
    int i = 0;
    
    char word[64];
    int w_idx = 0;
    
    auto flush_word = [&](uint32_t color) {
        if (w_idx > 0) {
            word[w_idx] = '\0';
            uint32_t c_color = color;
            if (str_equal(word, "void") || str_equal(word, "int") || str_equal(word, "return") ||
                str_equal(word, "class") || str_equal(word, "if") || str_equal(word, "else") ||
                str_equal(word, "const") || str_equal(word, "bool") || str_equal(word, "char") ||
                str_equal(word, "static") || str_equal(word, "struct") || str_equal(word, "namespace") ||
                str_equal(word, "include") || str_equal(word, "#include") || str_equal(word, "define") ||
                str_equal(word, "#define") || str_equal(word, "using")) {
                c_color = 0x00569CD6; // VS Code Blue
            }
            WindowManager::draw_string(word, cur_x, y_start, c_color, 14.0f);
            cur_x += WindowManager::get_string_width(word, 14.0f);
            w_idx = 0;
        }
    };
    
    while (line[i]) {
        char c = line[i];
        
        if (c == '/' && line[i + 1] == '/') {
            flush_word(0x00D4D4D4);
            WindowManager::draw_string(line + i, cur_x, y_start, 0x006A9955, 14.0f);
            return;
        }
        
        if (c == '\"') {
            flush_word(0x00D4D4D4);
            word[w_idx++] = c;
            i++;
            while (line[i] && line[i] != '\"' && w_idx < 60) {
                word[w_idx++] = line[i++];
            }
            if (line[i] == '\"') word[w_idx++] = '\"';
            flush_word(0x00D69D85);
            i++;
            continue;
        }
        
        if (c == ' ' || c == '\t' || c == '(' || c == ')' || c == '{' || c == '}' || c == ';' || c == ',' || c == '<' || c == '>') {
            flush_word(0x00D4D4D4);
            char sep[2] = {c, '\0'};
            WindowManager::draw_string(sep, cur_x, y_start, 0x00D4D4D4, 14.0f);
            cur_x += WindowManager::get_string_width(sep, 14.0f);
        } else {
            word[w_idx++] = c;
        }
        i++;
    }
    flush_word(0x00D4D4D4);
}

void WindowManager::draw_code_editor_content(Window* win) {
    int x = win->rect.x;
    int y = win->rect.y;
    int w = win->rect.w;
    int h = win->rect.h;

    // 1. Toolbar area (Slate Dark)
    drivers::Framebuffer::draw_rect_alpha(x + 1, y + 30, w - 2, 35, 0x002D2D2D, 255);
    drivers::Framebuffer::draw_rect_alpha(x + 1, y + 65, w - 2, 1, 0x001A1A1A, 255);

    // Save Button
    drivers::Framebuffer::draw_rounded_rect_alpha(x + 10, y + 35, 60, 24, 6, 0x003C3C3C, 255);
    draw_string("Save", x + 24, y + 40, C_WHITE, 13.0f);

    // Display loaded filename
    draw_string("File: ", x + 90, y + 40, 0x00808080, 13.0f);
    draw_string(win->title + 14, x + 130, y + 40, 0x00CCCCCC, 13.0f);

    // 2. Client area background (Dark Theme Slate)
    drivers::Framebuffer::draw_rect_alpha(x + 1, y + 66, w - 2, h - 66 - 12, 0x001E1E1E, 255);

    // 3. Line Number Sidebar (Lighter Dark)
    drivers::Framebuffer::draw_rect_alpha(x + 1, y + 66, 40, h - 66 - 12, 0x00252526, 255);
    drivers::Framebuffer::draw_rect_alpha(x + 41, y + 66, 1, h - 66 - 12, 0x00333333, 255);

    // Render lines and syntax highlight them
    int cur_line = 1;
    int line_y = y + 76;
    
    char line_buf[256];
    int lb_idx = 0;
    
    for (int i = 0; i <= win->text_len; i++) {
        char c = (i == win->text_len) ? '\n' : win->text_input[i];
        
        if (c == '\n') {
            line_buf[lb_idx] = '\0';
            
            // Draw line number
            char num_str[16];
            int_to_str(cur_line, num_str, 16);
            int nw = get_string_width(num_str, 12.0f);
            draw_string(num_str, x + 20 - nw/2, line_y, 0x00858585, 12.0f);
            
            // Highlight and draw line text
            if (i == win->text_len) {
                bool cursor_visible = (frame_counter / 20) % 2 == 0;
                if (cursor_visible && lb_idx < 250) {
                    line_buf[lb_idx++] = '|';
                    line_buf[lb_idx] = '\0';
                }
            }
            draw_syntax_highlighted_line(line_buf, x + 50, line_y);
            
            line_y += 18;
            cur_line++;
            lb_idx = 0;
            if (line_y > y + h - 25) break; // Limit viewport lines
        } else {
            if (lb_idx < 250) line_buf[lb_idx++] = c;
        }
    }
}

void WindowManager::draw_window_client_area(Window* win) {
    if (!win->buffer) return;
    int w = win->rect.w - 2;
    int h = win->rect.h - TITLE_BAR_HEIGHT - 12;
    drivers::Framebuffer::blit_buffer(win->rect.x + 1, win->rect.y + TITLE_BAR_HEIGHT, w, h, win->buffer);
}

void Window::draw(bool is_active) {
    (void)is_active;
    if (is_minimized) return;

    // Occlusion Culling Check:
    // If this window is completely covered by an opaque, non-minimized window in front of it, skip drawing.
    bool is_occluded = false;
    for (Window* front = this->next; front; front = front->next) {
        if (!front->is_minimized && !front->is_dragging) {
            if (this->rect.x >= front->rect.x && this->rect.y >= front->rect.y &&
                this->rect.x + this->rect.w <= front->rect.x + front->rect.w &&
                this->rect.y + this->rect.h <= front->rect.y + front->rect.h) {
                is_occluded = true;
                break;
            }
        }
    }
    if (is_occluded) return;

    bool active = (this == WindowManager::active_window);
    int shadow_size = active ? 24 : 12;
    Rect shadow_rect = {
        rect.x - shadow_size, rect.y - shadow_size + 3,
        rect.w + shadow_size * 2, rect.h + shadow_size * 2
    };
    if (!drivers::Framebuffer::get_clip_rect().intersects(shadow_rect)) {
        return; // Early Discard Optimization
    }

    uint8_t alpha = 255;

    WindowManager::draw_window_shadow(this->rect, active, alpha);

    bool is_terminal = this->title_is("Terminal");
    WindowManager::draw_window_body(this, alpha, is_terminal);

    // Fast-path: Skip heavy application rendering during active resizing
    // This stops vector fonts and HTML loops from starving the system tick timer
    bool fast_mode = (active && is_resizing_window);
    
    if (fast_mode) {
        int cx = rect.x + rect.w / 2;
        int cy = rect.y + rect.h / 2;
        const char* msg = "Resizing...";
        WindowManager::draw_string(msg, cx - 40, cy, C_TEXT, 15.0f);
    } else {
        // Only draw client area content if it intersects the active clip rect
        Rect client_rect = {rect.x + 1, rect.y + TITLE_BAR_HEIGHT, rect.w - 2, rect.h - TITLE_BAR_HEIGHT - 12};
        if (drivers::Framebuffer::get_clip_rect().intersects(client_rect)) {
            if (is_terminal) {
                WindowManager::draw_terminal_content(this);
            } else if (this->title_is("Finder")) {
                WindowManager::draw_finder_content(this);
            } else if (this->title_is("System Log")) {
                WindowManager::draw_system_log_content(this);
            } else if (this->title_is("Performance Monitor")) {
                WindowManager::draw_perf_monitor_content(this);
            } else if (this->title_is("About This Mac")) {
                WindowManager::draw_about_content(this);
            } else if (this->title_is("Safari")) {
                WindowManager::draw_safari_content(this);
            } else if (this->title_is("Mail")) {
                WindowManager::draw_mail_content(this);
            } else if (this->title_is("App Store")) {
                WindowManager::draw_appstore_content(this);
            } else if (this->title_is("Notes")) {
                WindowManager::draw_notes_content(this);
            } else if (this->title_starts_with("Code Editor")) {
                WindowManager::draw_code_editor_content(this);
            } else if (this->title_is("System Settings")) {
                WindowManager::draw_settings_content(this);
            } else {
                WindowManager::draw_window_client_area(this);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// WindowManager init / creation
// ---------------------------------------------------------------------------
void WindowManager::init() {
    window_list_head = nullptr;
    next_window_id = 1;
    mouse_x = 960;
    mouse_y = 540;
    last_mouse_x = 960;
    last_mouse_y = 540;
    mouse_pressed = false;
    right_mouse_pressed = false;
    active_window = nullptr;
    active_menu.active = false;

    drivers::Serial::println("[INIT] Window Manager Compositor initialized.");

    // Initialize mock CPU load history
    for (int i = 0; i < 20; i++) {
        perf_cpu_history[i] = (i * 7 + 13) % 25 + 5;
    }

    bool loaded = false;
    fs::VFSNode* cfg_node = fs::VFS::open("/tar/desktop.cfg");
    if (cfg_node) {
        fs::File f;
        f.node = cfg_node;
        f.offset = 0;
        
        static char cfg_buf[4096];
        ssize_t bytes = fs::VFS::read(&f, cfg_buf, sizeof(cfg_buf) - 1);
        if (bytes > 0) {
            cfg_buf[bytes] = '\0';
            desktop_item_count = 0;
            loaded = true;
            
            int idx = 0;
            char line[256];
            int l_idx = 0;
            while (idx <= bytes) {
                char c = (idx == bytes) ? '\n' : cfg_buf[idx];
                if (c == '\n') {
                    line[l_idx] = '\0';
                    if (l_idx > 0) {
                        if (title_starts_with_custom(line, "SCALE ")) {
                            int scale_val = 100;
                            int v = 0;
                            int k = 6;
                            while (line[k] >= '0' && line[k] <= '9') {
                                v = v * 10 + (line[k] - '0');
                                k++;
                            }
                            if (v > 0) {
                                ui_scale = v / 100.0f;
                                set_ui_scale(ui_scale);
                            }
                        } else if (title_starts_with_custom(line, "THEME ")) {
                            bool is_dark = str_equal(line + 6, "DARK");
                            set_theme(is_dark);
                        } else if (title_starts_with_custom(line, "WALLPAPER ")) {
                            int wp_id = line[10] - '0';
                            drivers::Framebuffer::set_wallpaper_theme(wp_id);
                            wallpaper_theme_id = wp_id;
                        } else if (title_starts_with_custom(line, "ITEM ")) {
                            char name[64];
                            char type[16];
                            char path[64];
                            int x = 0, y = 0;
                            
                            int k = 5;
                            int dest = 0;
                            while (line[k] && line[k] != ' ' && dest < 63) { name[dest++] = line[k++]; }
                            name[dest] = '\0';
                            if (line[k] == ' ') k++;
                            
                            dest = 0;
                            while (line[k] && line[k] != ' ' && dest < 15) { type[dest++] = line[k++]; }
                            type[dest] = '\0';
                            if (line[k] == ' ') k++;
                            
                            dest = 0;
                            while (line[k] && line[k] != ' ' && dest < 63) { path[dest++] = line[k++]; }
                            path[dest] = '\0';
                            if (line[k] == ' ') k++;
                            
                            while (line[k] >= '0' && line[k] <= '9') { x = x * 10 + (line[k] - '0'); k++; }
                            if (line[k] == ' ') k++;
                            
                            while (line[k] >= '0' && line[k] <= '9') { y = y * 10 + (line[k] - '0'); k++; }
                            
                            bool is_dir = str_equal(type, "DIR");
                            bool is_term = str_equal(type, "TERM");
                            add_item(name, is_dir, is_term, path, x, y);
                        } else if (title_starts_with_custom(line, "FILE ")) {
                            char name[64];
                            int size = 0;
                            
                            int k = 5;
                            int dest = 0;
                            while (line[k] && line[k] != ' ' && dest < 63) { name[dest++] = line[k++]; }
                            name[dest] = '\0';
                            if (line[k] == ' ') k++;
                            
                            while (line[k] >= '0' && line[k] <= '9') { size = size * 10 + (line[k] - '0'); k++; }
                            if (line[k] == ' ') k++;
                            
                            char* file_data = new char[size + 1];
                            if (file_data) {
                                int h_idx = 0;
                                for (int j = 0; j < size; j++) {
                                    char c1 = line[k + h_idx];
                                    char c2 = line[k + h_idx + 1];
                                    h_idx += 2;
                                    
                                    auto decode_char = [](char ch) -> uint8_t {
                                        if (ch >= '0' && ch <= '9') return ch - '0';
                                        if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
                                        return 0;
                                    };
                                    file_data[j] = (decode_char(c1) << 4) | decode_char(c2);
                                }
                                file_data[size] = '\0';
                                
                                fs::VFS::create_file(name, file_data, size + 1);
                                fs::VFSNode* n = fs::VFS::open(name);
                                if (n) n->size = size;
                            }
                        }
                    }
                    l_idx = 0;
                } else {
                    if (l_idx < 250) {
                        line[l_idx++] = c;
                    }
                }
                idx++;
            }
        }
    }
    
    if (!loaded) {
        desktop_item_count = 0;
        add_item("Applications", true, false, "Desktop", 30, 60);
        add_item("Documents", true, false, "Desktop", 30, 160);
        add_item("System Log.txt", false, false, "Desktop", 30, 260);
        add_item("Terminal.app", false, true, "Desktop", 30, 360);

        add_item("Terminal", false, true, "Applications", 0, 0);
        add_item("Safari", false, false, "Applications", 0, 0);
        add_item("Mail", false, false, "Applications", 0, 0);
        add_item("Notes", false, false, "Applications", 0, 0);
        add_item("System Settings", false, false, "Applications", 0, 0);

        add_item("hello.txt", false, false, "Documents", 0, 0);
        add_item("info.txt", false, false, "Documents", 0, 0);
    }
}

Window* WindowManager::create_window(int x, int y, int w, int h, const char* title, uint32_t color) {
    // Fix 17: Handle OOM gracefully without exceptions (freestanding compatible)
    Window* win = new Window(next_window_id++, x, y, w, h, title, color);
    if (!win) {
        drivers::Serial::println("[WM ERROR] Failed to allocate window");
        next_window_id--;
        return nullptr;
    }

    if (!window_list_head) {
        window_list_head = win;
    } else {
        Window* curr = window_list_head;
        while (curr->next) curr = curr->next;
        curr->next = win;
    }

    drivers::Serial::print("[WM] Created window: ID=");
    char buf[16];
    int_to_str(win->id, buf, 16);
    drivers::Serial::print(buf);
    drivers::Serial::print(" Title=\"");
    drivers::Serial::print(win->title);
    drivers::Serial::println("\"");

    return win;
}

void WindowManager::close_window(int id) {
    if (!window_list_head) return;

    Window* prev = nullptr;
    Window* curr = window_list_head;
    while (curr) {
        if (curr->id == id) break;
        prev = curr;
        curr = curr->next;
    }
    if (!curr) return;

    if (prev) prev->next = curr->next;
    else window_list_head = curr->next;

    if (active_window == curr) {
        active_window = nullptr;
        Window* top = window_list_head;
        while (top && top->next) {
            top = top->next;
        }
        if (top) active_window = top;
    }
    if (curr->buffer) {
        delete[] curr->buffer;
    }
    delete curr;
}

Window* WindowManager::get_window_by_id(int id) {
    for (Window* w = window_list_head; w; w = w->next) {
        if (w->id == id) return w;
    }
    return nullptr;
}

void WindowManager::minimize_window_animated(Window* win) {
    if (!win) return;
    int width = (int)drivers::Framebuffer::get_width();
    int height = (int)drivers::Framebuffer::get_height();
    int dock_y = height - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM;
    int target_x = width / 2;
    int target_y = dock_y;

    Rect orig = win->rect;
    for (int frame = 0; frame <= 5; ++frame) {
        float t = (float)frame / 5.0f;
        int w = orig.w - (int)(orig.w * t * 0.85f);
        int h = orig.h - (int)(orig.h * t * 0.85f);
        int x = orig.x + (int)((target_x - (w / 2) - orig.x) * t);
        int y = orig.y + (int)((target_y - orig.y) * t);
        
        win->rect = {x, y, w, h};
        force_redraw_all();
        for (volatile int d = 0; d < 8000000; ) { d = d + 1; }
    }
    
    win->rect = orig;
    win->is_minimized = true;
    force_redraw_all();
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------
// Fix 7: Use explicit tail check to avoid skipping single-window no-ops unnecessarily
void WindowManager::focus_window(Window* win) {
    if (!win || !window_list_head) return;
    
    Window* tail = window_list_head;
    while (tail && tail->next) tail = tail->next;
    if (win == tail) return; // Already at the front

    bring_to_front(win);
}

void WindowManager::bring_to_front(Window* win) {
    // Fix: Added parentheses around && within || to satisfy -Wparentheses
    if (!win || !window_list_head || (win == window_list_head && !win->next)) return;

    if (win == window_list_head) {
        window_list_head = win->next;
    } else {
        Window* prev = window_list_head;
        while (prev && prev->next != win) prev = prev->next;
        if (prev) prev->next = win->next;
    }

    Window* tail = window_list_head;
    while (tail && tail->next) tail = tail->next;
    if (tail) tail->next = win;
    else window_list_head = win;
    win->next = nullptr;
}

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------
void WindowManager::draw_cursor() {
}

void WindowManager::erase_cursor() {
}

void WindowManager::blit_cursor() {
    // Deprecated stub kept for header ABI compatibility
}

// ---------------------------------------------------------------------------
// Desktop / wallpaper
// ---------------------------------------------------------------------------
void WindowManager::draw_wallpaper() {
    int w = drivers::Framebuffer::get_width();
    int h = drivers::Framebuffer::get_height();
    drivers::Framebuffer::draw_mac_wallpaper(0, 0, w, h);
}

void WindowManager::draw_desktop_icons() {
    Rect clip = drivers::Framebuffer::get_clip_rect();
    int size_bg_w = (int)(64 * ui_scale);
    int size_bg_h = (int)(80 * ui_scale);
    int size_ic = (int)(40 * ui_scale);
    int pad_ic = (int)(12 * ui_scale);
    int radius_ic = (int)(8 * ui_scale);

    for (int i = 0; i < desktop_item_count; i++) {
        DiskItem& it = desktop_items[i];
        if (str_equal(it.path, "Desktop")) {
            // Check if icon bounds (padded for wide labels) overlap active dirty region
            Rect icon_rect = {it.x - (int)(32 * ui_scale), it.y - (int)(10 * ui_scale), (int)(128 * ui_scale), (int)(90 * ui_scale)};
            if (!clip.intersects(icon_rect)) {
                continue;
            }

            if (it.selected) {
                drivers::Framebuffer::draw_rounded_rect_alpha(it.x, it.y - (int)(5 * ui_scale), size_bg_w, size_bg_h, (int)(8 * ui_scale), 0x0080B0F0, 100);
            }

            uint32_t icon_c = it.is_directory ? C_FOLDER : (it.is_terminal ? 0x001A1A1A : C_FILE);
            
            drivers::Framebuffer::draw_rounded_rect_alpha(it.x + pad_ic, it.y, size_ic, size_ic, radius_ic, icon_c, 255);
            
            if (it.is_directory) {
                int lw = get_string_width("F", 15.0f);
                draw_string("F", it.x + pad_ic + (size_ic - lw)/2, it.y + (int)(12 * ui_scale), C_WHITE, 15.0f);
            } else if (it.is_terminal) {
                int lw = get_string_width("T", 15.0f);
                draw_string("T", it.x + pad_ic + (size_ic - lw)/2, it.y + (int)(12 * ui_scale), 0x004AF02C, 15.0f);
            }
            
            int label_w = get_string_width(it.name, 13.0f);
            int label_x = it.x + (size_bg_w - label_w) / 2;
            draw_string(it.name, label_x, it.y + (int)(45 * ui_scale), C_WHITE, 13.0f);
        }
    }
}

void WindowManager::arrange_desktop() {
    int start_x = (int)(30 * ui_scale);
    int start_y = (int)(60 * ui_scale);
    int spacing_y = (int)(90 * ui_scale);
    int idx = 0;
    for (int i = 0; i < desktop_item_count; i++) {
        if (str_equal(desktop_items[i].path, "Desktop")) {
            desktop_items[i].x = start_x;
            desktop_items[i].y = start_y + idx * spacing_y;
            idx++;
        }
    }
}

void WindowManager::draw_desktop() {}

void WindowManager::draw_all_windows() {
    Window* curr = window_list_head;
    while (curr) {
        curr->draw(curr == active_window);
        curr = curr->next;
    }
}

// Fix 13 & 3: Correct back-buffer caching sequence and unified cursor cache logic
void WindowManager::force_redraw_all() {
    drivers::Framebuffer::clear_clip_rect();
    draw_wallpaper();
    draw_desktop_icons();
    draw_mac_decorations();
    draw_all_windows();
    
    // Draw cursor in backbuffer before swap
    plot_cursor_to_backbuffer(mouse_x, mouse_y);
    
    drivers::Framebuffer::swap_buffers();
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------
void WindowManager::draw_menu_bar() {
    int w = drivers::Framebuffer::get_width();
    drivers::Framebuffer::draw_rect_alpha(0, 0, w, MENU_BAR_HEIGHT, C_MENU_BG, 180);
    drivers::Framebuffer::draw_rect_alpha(0, MENU_BAR_HEIGHT, w, 1, C_MENU_LINE, 180);

    draw_string("Crecent", 18, 7, C_TEXT, 16.0f);
    draw_string("Finder    File    Edit    View    Go    Window    Help", 90, 7, C_TEXT, 15.0f);
    
    if (wifi_connected) {
        draw_string("Wifi: On", w - 280, 7, 0x000F52BA, 14.0f);
    } else {
        draw_string("Wifi: Off", w - 280, 7, 0x00808080, 14.0f);
    }

    if (usb_connected) {
        draw_string("USB: Mounted", w - 190, 7, 0x0034A853, 14.0f);
    } else {
        draw_string("USB: Off", w - 190, 7, 0x00808080, 14.0f);
    }

    draw_string("Wed 14:35", w - 100, 7, C_TEXT, 15.0f);
}

// ---------------------------------------------------------------------------
// Dock
// ---------------------------------------------------------------------------
void WindowManager::draw_dock() {
    int width = drivers::Framebuffer::get_width();
    int height = drivers::Framebuffer::get_height();
    int dock_y = height - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM;
    int dock_x = (width - DOCK_WIDTH) / 2;

    drivers::Framebuffer::draw_rounded_rect_alpha(dock_x, dock_y, DOCK_WIDTH, DOCK_HEIGHT, 20, C_DOCK_BG, 190);
    drivers::Framebuffer::draw_rounded_rect_alpha(dock_x, dock_y, DOCK_WIDTH, DOCK_HEIGHT, 20, C_WHITE, 50);
    drivers::Framebuffer::draw_rect_alpha(dock_x, dock_y, DOCK_WIDTH, 1, C_DOCK_RIM, 100);

    struct AppIcon { uint32_t color; const char* letter; uint32_t text; };
    AppIcon apps[] = {
        {0x001B72E8, "F", C_WHITE},
        {0x0000A2C9, "S", C_WHITE},
        {0x00E0E0E0, "M", 0x00FF3B30},
        {0x001A1A1A, "T", 0x004AF02C},
        {0x007E8E9F, "A", C_WHITE},
        {0x00FFCC00, "N", C_TEXT}
    };
    const int num_apps = 6;

    int total_icons_w = num_apps * DOCK_ICON_SIZE + (num_apps - 1) * DOCK_ICON_SPACING;
    int start_x = dock_x + (DOCK_WIDTH - total_icons_w) / 2;

    for (int i = 0; i < num_apps; ++i) {
        int icon_x = start_x + i * (DOCK_ICON_SIZE + DOCK_ICON_SPACING);
        int icon_y = dock_y + 8;
        int icon_size = DOCK_ICON_SIZE;

        int center_x = icon_x + DOCK_ICON_SIZE / 2;
        int dist = mouse_x - center_x;
        if (dist < 0) dist = -dist;

        if (mouse_y >= dock_y - 40 && mouse_y <= dock_y + DOCK_HEIGHT + 40 && dist < 120) {
            float scale = 1.0f - (dist / 120.0f);
            icon_size += (int)(32.0f * scale);
            icon_y -= (int)(24.0f * scale);
            icon_x -= (icon_size - DOCK_ICON_SIZE) / 2;
        }

        drivers::Framebuffer::draw_rounded_rect_alpha(icon_x, icon_y, icon_size, icon_size, 12, apps[i].color, 255);

        int font_size = icon_size / 2;
        int letter_w = get_string_width(apps[i].letter, (float)font_size);
        int letter_x = icon_x + (icon_size - letter_w) / 2;
        int letter_y = icon_y + (icon_size - font_size) / 2 + 2;
        draw_string(apps[i].letter, letter_x, letter_y, apps[i].text, (float)font_size);
    }

    bool finder_running = false, terminal_running = false;
    for (Window* w = window_list_head; w; w = w->next) {
        if (w->title_is("Finder")) finder_running = true;
        if (w->title_is("Terminal")) terminal_running = true;
    }
    if (finder_running) {
        int x = start_x + 0 * (DOCK_ICON_SIZE + DOCK_ICON_SPACING) + DOCK_ICON_SIZE / 2;
        drivers::Framebuffer::draw_circle_filled(x, dock_y + DOCK_HEIGHT - 4, 2, C_TEXT);
    }
    if (terminal_running) {
        int x = start_x + 3 * (DOCK_ICON_SIZE + DOCK_ICON_SPACING) + DOCK_ICON_SIZE / 2;
        drivers::Framebuffer::draw_circle_filled(x, dock_y + DOCK_HEIGHT - 4, 2, C_TEXT);
    }
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------
void WindowManager::draw_menu() {
    if (!active_menu.active) return;

    // Draw shadow
    for (int i = 0; i < 4; ++i) {
        drivers::Framebuffer::draw_rounded_rect_alpha(
            active_menu.x - i + 2, active_menu.y - i + 2,
            active_menu.w + i * 2, active_menu.h + i * 2,
            8 + i, C_BLACK, 25 - i * 5);
    }

    // Translucent menu background
    drivers::Framebuffer::draw_rounded_rect_alpha(
        active_menu.x, active_menu.y, active_menu.w, active_menu.h, 8, C_MENU_BG, 230);

    // Outer thin border for glassmorphism look
    uint32_t border_color = C_MENU_LINE;
    drivers::Framebuffer::draw_rounded_rect_alpha(
        active_menu.x, active_menu.y, active_menu.w, active_menu.h, 8, border_color, 120);

    const char** items = nullptr;
    int count = 0;

    if (active_menu.type == 0) {
        static const char* apple_items[] = {"About This Mac", "Restart", "Shut Down"};
        items = apple_items; count = 3;
    } else if (active_menu.type == 1) {
        static const char* finder_items[] = {"New Window", "Close All"};
        items = finder_items; count = 2;
    } else if (active_menu.type == 2) {
        static const char* desktop_items[] = {"New Folder", "New File", "Open Terminal", "Clean Up"};
        items = desktop_items; count = 4;
    } else if (active_menu.type == 3) {
        static const char* file_items[] = {"New Finder Window", "Close Window"};
        items = file_items; count = 2;
    } else if (active_menu.type == 4) {
        static const char* edit_items[] = {"Undo", "Redo", "Cut", "Copy"};
        items = edit_items; count = 4;
    } else if (active_menu.type == 5) {
        static const char* view_items[] = {"As Icons", "As List"};
        items = view_items; count = 2;
    } else if (active_menu.type == 6) {
        static const char* go_items[] = {"Applications", "Documents", "Downloads", "Home"};
        items = go_items; count = 4;
    } else if (active_menu.type == 7) {
        static const char* window_items[] = {"Minimize", "Zoom", "Bring All to Front"};
        items = window_items; count = 3;
    } else if (active_menu.type == 8) {
        static const char* help_items[] = {"Crecent Help", "Send Feedback"};
        items = help_items; count = 2;
    } else if (active_menu.type == 9) {
        static const char* finder_item_items[] = {"Copy", "Cut", "Rename", "Delete"};
        items = finder_item_items; count = 4;
    } else if (active_menu.type == 10) {
        static const char* finder_empty_items[] = {"Paste", "New Folder", "New File"};
        items = finder_empty_items; count = 3;
    } else if (active_menu.type == 11) {
        bool selected_usb = false;
        for (int j = 0; j < desktop_item_count; j++) {
            if (desktop_items[j].selected && str_equal(desktop_items[j].name, "USB_DRIVE")) {
                selected_usb = true;
                break;
            }
        }
        if (selected_usb) {
            static const char* desktop_usb_items[] = {"Open", "Rename", "Eject USB", "Clean Up"};
            items = desktop_usb_items; count = 4;
        } else {
            static const char* desktop_item_items[] = {"Open", "Rename", "Delete", "Clean Up"};
            items = desktop_item_items; count = 4;
        }
    }

    for (int i = 0; i < count; ++i) {
        int iy = active_menu.y + MENU_PADDING + i * MENU_ITEM_HEIGHT;
        
        // Draw elegant hover with rounded corners
        if (active_menu.hovered_item == i) {
            drivers::Framebuffer::draw_rounded_rect_alpha(
                active_menu.x + 4, iy, active_menu.w - 8, 24, 4, C_HOVER, 255);
            draw_string(items[i], active_menu.x + 12, iy + 4, C_WHITE, 15.0f);
        } else {
            draw_string(items[i], active_menu.x + 12, iy + 4, C_TEXT, 15.0f);
        }

        // Draw horizontal separators between groups
        bool separator_after = false;
        if (active_menu.type == 0 && i == 0) separator_after = true; // After About This Mac
        else if (active_menu.type == 2 && (i == 1 || i == 2)) separator_after = true; // After New File and Open Terminal
        else if (active_menu.type == 6 && i == 2) separator_after = true; // After Downloads

        if (separator_after && i < count - 1) {
            drivers::Framebuffer::draw_rect_alpha(
                active_menu.x + 8, iy + MENU_ITEM_HEIGHT - 1,
                active_menu.w - 16, 1, C_MENU_LINE, 100);
        }
    }
}

void WindowManager::draw_mac_decorations() {
    Rect clip = drivers::Framebuffer::get_clip_rect();
    int screen_w = (int)drivers::Framebuffer::get_width();
    int screen_h = (int)drivers::Framebuffer::get_height();

    // Menu bar is at the top
    Rect menu_bar_rect = {0, 0, screen_w, MENU_BAR_HEIGHT};
    if (clip.intersects(menu_bar_rect)) {
        draw_menu_bar();
    }

    // Dock is at the bottom (allow padding for vertical magnification shift)
    int dock_y = screen_h - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM;
    int dock_x = (screen_w - DOCK_WIDTH) / 2;
    Rect dock_rect = {dock_x, dock_y - 24, DOCK_WIDTH, DOCK_HEIGHT + 24};
    if (clip.intersects(dock_rect)) {
        draw_dock();
    }

    if (active_menu.active) {
        Rect menu_rect = {active_menu.x, active_menu.y, active_menu.w, active_menu.h};
        if (clip.intersects(menu_rect)) {
            draw_menu();
        }
    }
}

// ---------------------------------------------------------------------------
// Redraw utilities
// ---------------------------------------------------------------------------
// Fix 3 & 4: Unified cursor update guarantee and perfectly bounded rectangle clip
void WindowManager::redraw_dirty_rect(const Rect& dirty) {
    drivers::Framebuffer::set_clip_rect(dirty);
    drivers::Framebuffer::draw_mac_wallpaper(dirty.x, dirty.y, dirty.w, dirty.h);
    draw_desktop_icons();
    
    if (is_dragging_selection) {
        int x1 = click_start_x < mouse_x ? click_start_x : mouse_x;
        int y1 = click_start_y < mouse_y ? click_start_y : mouse_y;
        int x2 = click_start_x > mouse_x ? click_start_x : mouse_x;
        int y2 = click_start_y > mouse_y ? click_start_y : mouse_y;
        
        drivers::Framebuffer::draw_rect_alpha(x1, y1, x2 - x1, y2 - y1, 0x0080B0F0, 100);
        drivers::Framebuffer::draw_rect_alpha(x1, y1, x2 - x1, 1, 0x000F52BA, 255);
        drivers::Framebuffer::draw_rect_alpha(x1, y2, x2 - x1, 1, 0x000F52BA, 255);
        drivers::Framebuffer::draw_rect_alpha(x1, y1, 1, y2 - y1, 0x000F52BA, 255);
        drivers::Framebuffer::draw_rect_alpha(x2, y1, 1, y2 - y1 + 1, 0x000F52BA, 255);
    }

    draw_mac_decorations();
    draw_all_windows();
    draw_drag_preview();
    
    // Draw the cursor on the backbuffer inside the clipped region
    plot_cursor_to_backbuffer(mouse_x, mouse_y);
    
    drivers::Framebuffer::clear_clip_rect();
    drivers::Framebuffer::swap_dirty_rect_fast(dirty);
}

// ---------------------------------------------------------------------------
// Mouse handling
// ---------------------------------------------------------------------------
// Fix 14: Event handler cleanly separated into logical state transitions
void WindowManager::handle_mouse_move(int new_x, int new_y, bool left_pressed, bool right_pressed) {
    int width = (int)drivers::Framebuffer::get_width();
    int height = (int)drivers::Framebuffer::get_height();

    if (new_x < 0) new_x = 0;
    if (new_x >= width) new_x = width - 1;
    if (new_y < 0) new_y = 0;
    if (new_y >= height) new_y = height - 1;

    bool left_down = left_pressed && !mouse_pressed;
    bool right_down = right_pressed && !right_mouse_pressed;
    bool left_up = !left_pressed && mouse_pressed;
    bool position_changed = (new_x != mouse_x || new_y != mouse_y);

    if (left_down) {
        click_start_x = new_x;
        click_start_y = new_y;
    }

    int old_x = mouse_x;
    int old_y = mouse_y;

    // Fix 4: Apply mouse positions immediately before rendering ops to prevent stale coordinates
    mouse_x = new_x;
    mouse_y = new_y;

    bool needs_redraw = false;
    Rect dirty = {0, 0, 0, 0};
    bool state_updated = false;

    // 1. Right Click Desktop Menu
    if (right_down) {
        active_menu.active = false;
        Window* clicked = nullptr;
        for (Window* w = window_list_head; w; w = w->next) {
            if (!w->is_minimized && in_rect(new_x, new_y, w->rect)) clicked = w;
        }
        if (clicked && clicked->title_is("Finder")) {
            int rx = new_x - (clicked->rect.x + 1);
            int ry = new_y - (clicked->rect.y + TITLE_BAR_HEIGHT);
            if (rx >= 152 && ry >= 35) {
                const char* active_dir = clicked->text_input;
                if (clicked->text_len == 0) active_dir = "Desktop";
                
                int clicked_item_idx = -1;
                int grid_col = 0;
                int grid_row = 0;
                for (int i = 0; i < desktop_item_count; i++) {
                    DiskItem& it = desktop_items[i];
                    if (str_equal(it.path, active_dir)) {
                        int ix = 170 + grid_col * 90;
                        int iy = 55 + grid_row * 90;
                        if (rx >= ix && rx < ix + 64 && ry >= iy && ry < iy + 80) {
                            clicked_item_idx = i;
                            break;
                        }
                        grid_col++;
                        if (grid_col >= 3) {
                            grid_col = 0;
                            grid_row++;
                        }
                    }
                }
                
                if (clicked_item_idx != -1) {
                    clicked->selected_item_idx = clicked_item_idx;
                    is_renaming_item = false;
                    active_menu.active = true;
                    active_menu.x = new_x;
                    active_menu.y = new_y;
                    active_menu.w = MENU_WIDTH;
                    active_menu.h = 4 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING;
                    active_menu.type = 9; // Finder Item Context Menu
                    active_menu.hovered_item = -1;
                } else {
                    clicked->selected_item_idx = -1;
                    is_renaming_item = false;
                    active_menu.active = true;
                    active_menu.x = new_x;
                    active_menu.y = new_y;
                    active_menu.w = MENU_WIDTH;
                    active_menu.h = 3 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING;
                    active_menu.type = 10; // Finder Empty Context Menu
                    active_menu.hovered_item = -1;
                }
                force_redraw_all();
                state_updated = true;
            }
        }
        
        if (!state_updated) {
            bool clicked_something = (new_y <= MENU_BAR_HEIGHT);
            for (Window* w = window_list_head; w && !clicked_something; w = w->next) {
                if (in_rect(new_x, new_y, w->rect)) clicked_something = true;
            }
            if (!clicked_something) {
                int right_clicked_icon_idx = -1;
                for (int i = 0; i < desktop_item_count; i++) {
                    DiskItem& it = desktop_items[i];
                    if (str_equal(it.path, "Desktop")) {
                        if (new_x >= it.x && new_x < it.x + 64 && new_y >= it.y && new_y < it.y + 80) {
                            right_clicked_icon_idx = i;
                            break;
                        }
                    }
                }
                
                if (right_clicked_icon_idx != -1) {
                    for (int j = 0; j < desktop_item_count; j++) {
                        desktop_items[j].selected = false;
                    }
                    desktop_items[right_clicked_icon_idx].selected = true;
                    
                    active_menu.active = true;
                    active_menu.x = new_x;
                    active_menu.y = new_y;
                    active_menu.w = MENU_WIDTH;
                    active_menu.h = 4 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING;
                    active_menu.type = 11; // Desktop Item Context Menu
                    active_menu.hovered_item = -1;
                } else {
                    active_menu.active = true;
                    active_menu.x = new_x;
                    active_menu.y = new_y;
                    active_menu.w = MENU_WIDTH;
                    active_menu.h = 4 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING;
                    active_menu.type = 2; // Desktop Empty Context Menu
                    active_menu.hovered_item = -1;
                }
                force_redraw_all();
                state_updated = true;
            }
        }
    }

    // 2. Left Click Down Logic
    if (!state_updated && left_down) {
        if (active_menu.active) {
            if (in_rect(new_x, new_y, active_menu.x, active_menu.y, active_menu.w, active_menu.h)) {
                int item = active_menu.hovered_item;
                active_menu.active = false;
                if (item != -1) {
                    if (active_menu.type == 0) {
                        if (item == 0) create_window((width - 500) / 2, (height - 320) / 2, 500, 320, "About This Mac", C_FINDER);
                        else {
                            drivers::Serial::println("[SYSTEM] Initiating shutdown sequence...");
                            __asm__ volatile("cli; hlt");
                        }
                    } else if (active_menu.type == 1) {
                        if (item == 0) create_window(200, 150, 500, 350, "Finder", C_FINDER);
                        else {
                            // Fix 24: O(n^2) list clear replaced with O(n)
                            while (window_list_head) close_window(window_list_head->id);
                        }
                    } else if (active_menu.type == 2) {
                        if (item == 0) { // New Folder
                            char folder_name[32] = "New Folder";
                            int count = 1;
                            bool exists = true;
                            while (exists) {
                                exists = false;
                                for (int i = 0; i < desktop_item_count; i++) {
                                    if (str_equal(desktop_items[i].name, folder_name)) {
                                        exists = true;
                                        break;
                                    }
                                }
                                if (exists) {
                                    char num_buf[16];
                                    int_to_str(count, num_buf, 16);
                                    folder_name[0] = '\0';
                                    const char* prefix = "New Folder ";
                                    int p_idx = 0;
                                    while (prefix[p_idx]) { folder_name[p_idx] = prefix[p_idx]; p_idx++; }
                                    int n_idx = 0;
                                    while (num_buf[n_idx]) { folder_name[p_idx + n_idx] = num_buf[n_idx]; n_idx++; }
                                    folder_name[p_idx + n_idx] = '\0';
                                    count++;
                                }
                            }
                            int next_y = 60;
                            for (int i = 0; i < desktop_item_count; i++) {
                                if (str_equal(desktop_items[i].path, "Desktop") && desktop_items[i].y >= next_y) {
                                    next_y = desktop_items[i].y + 90;
                                }
                            }
                            add_item(folder_name, true, false, "Desktop", 30, next_y);
                        } else if (item == 1) { // New File
                            char file_name[32] = "New File.txt";
                            int count = 1;
                            bool exists = true;
                            while (exists) {
                                exists = false;
                                for (int i = 0; i < desktop_item_count; i++) {
                                    if (str_equal(desktop_items[i].name, file_name)) {
                                        exists = true;
                                        break;
                                    }
                                }
                                if (exists) {
                                    char num_buf[16];
                                    int_to_str(count, num_buf, 16);
                                    file_name[0] = '\0';
                                    const char* prefix = "New File ";
                                    int p_idx = 0;
                                    while (prefix[p_idx]) { file_name[p_idx] = prefix[p_idx]; p_idx++; }
                                    int n_idx = 0;
                                    while (num_buf[n_idx]) { file_name[p_idx + n_idx] = num_buf[n_idx]; n_idx++; }
                                    int s_idx = p_idx + n_idx;
                                    file_name[s_idx++] = '.'; file_name[s_idx++] = 't'; file_name[s_idx++] = 'x'; file_name[s_idx++] = 't'; file_name[s_idx] = '\0';
                                    count++;
                                }
                            }
                            int next_y = 60;
                            for (int i = 0; i < desktop_item_count; i++) {
                                if (str_equal(desktop_items[i].path, "Desktop") && desktop_items[i].y >= next_y) {
                                    next_y = desktop_items[i].y + 90;
                                }
                            }
                            add_item(file_name, false, false, "Desktop", 30, next_y);
                        } else if (item == 2) { // Open Terminal
                            create_window((width - 500)/2, (height - 350)/2, 500, 350, "Terminal", C_TERMINAL);
                        } else if (item == 3) { // Clean Up
                            arrange_desktop();
                        }
                    } else if (active_menu.type == 3) { // File
                        if (item == 0) create_window(200, 150, 500, 350, "Finder", C_FINDER);
                        else if (item == 1 && active_window) close_window(active_window->id);
                    } else if (active_menu.type == 6) { // Go
                        if (item == 0) create_window((width - 600) / 2, (height - 400) / 2, 600, 400, "App Store", 0x001B72E8);
                        else if (item == 3) create_window(200, 150, 500, 350, "Finder", C_FINDER);
                    } else if (active_menu.type == 7) { // Window
                        if (item == 0 && active_window) {
                            minimize_window_animated(active_window);
                        } else if (item == 1 && active_window) {
                            if (active_window->is_maximized) {
                                active_window->rect = active_window->orig_rect;
                                active_window->is_maximized = false;
                            } else {
                                active_window->orig_rect = active_window->rect;
                                active_window->rect = {0, MENU_BAR_HEIGHT, width, height - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM - MENU_BAR_HEIGHT - 16};
                                active_window->is_maximized = true;
                            }
                        }
                    } else if (active_menu.type == 8) { // Help
                        if (item == 0) create_window(250, 180, 600, 400, "Safari", C_FINDER);
                    } else if (active_menu.type == 11) { // Desktop Item Context Menu
                        int sel = -1;
                        for (int j = 0; j < desktop_item_count; j++) {
                            if (desktop_items[j].selected) {
                                sel = j;
                                break;
                            }
                        }
                        if (sel != -1) {
                            DiskItem& it = desktop_items[sel];
                            if (item == 0) { // Open
                                if (it.is_directory) {
                                    Window* w = create_window(200, 150, 500, 350, "Finder", C_FINDER);
                                    if (w) {
                                        int len = 0;
                                        while (it.name[len] && len < 250) {
                                            w->text_input[len] = it.name[len];
                                            len++;
                                        }
                                        w->text_input[len] = '\0';
                                        w->text_len = len;
                                    }
                                } else if (it.is_terminal) {
                                    create_window((width - 500)/2, (height - 350)/2, 500, 350, "Terminal", C_TERMINAL);
                                } else if (str_equal(it.name, "System Settings")) {
                                    create_window((width - 500)/2, (height - 420)/2, 500, 420, "System Settings", 0x00FFFFFF);
                                } else {
                                    char edit_title[128] = "Code Editor - /";
                                    int t_idx = 15;
                                    int n_idx = 0;
                                    while (it.name[n_idx]) { edit_title[t_idx++] = it.name[n_idx++]; }
                                    edit_title[t_idx] = '\0';
                                    
                                    Window* code_win = create_window((width - 650)/2, (height - 450)/2, 650, 450, edit_title, 0x001E1E1E);
                                    if (code_win) {
                                        char full_path[128] = "/";
                                        if (str_equal(it.name, "hello.txt")) {
                                            const char* hello_p = "tar/hello.txt";
                                            int hp_idx = 0;
                                            while (hello_p[hp_idx]) { full_path[1 + hp_idx] = hello_p[hp_idx]; hp_idx++; }
                                            full_path[1 + hp_idx] = '\0';
                                        } else {
                                            int n_idx2 = 0;
                                            while (it.name[n_idx2]) { full_path[1 + n_idx2] = it.name[n_idx2]; n_idx2++; }
                                            full_path[1 + n_idx2] = '\0';
                                        }
                                        fs::VFSNode* node = fs::VFS::open(full_path);
                                        if (node) {
                                            fs::File f;
                                            f.node = node;
                                            f.offset = 0;
                                            ssize_t bytes = fs::VFS::read(&f, code_win->text_input, sizeof(code_win->text_input) - 1);
                                            if (bytes > 0) {
                                                code_win->text_input[bytes] = '\0';
                                                code_win->text_len = bytes;
                                            }
                                        }
                                    }
                                }
                            } else if (item == 1) { // Rename
                                is_renaming_item = true;
                                active_window = nullptr;
                            } else if (item == 2) { // Delete or Eject
                                if (str_equal(it.name, "USB_DRIVE")) {
                                    usb_connected = false;
                                    drivers::Serial::println("[USB] Mass Storage device safely removed.");
                                    for (int j = 0; j < desktop_item_count; ) {
                                        if (str_equal(desktop_items[j].name, "USB_DRIVE") || str_equal(desktop_items[j].path, "USB_DRIVE")) {
                                            for (int k = j; k < desktop_item_count - 1; k++) {
                                                desktop_items[k] = desktop_items[k + 1];
                                            }
                                            desktop_item_count--;
                                        } else {
                                            j++;
                                        }
                                    }
                                } else {
                                    for (int j = sel; j < desktop_item_count - 1; j++) {
                                        desktop_items[j] = desktop_items[j + 1];
                                    }
                                    desktop_item_count--;
                                }
                                trigger_host_persist();
                            } else if (item == 3) { // Clean Up
                                arrange_desktop();
                                trigger_host_persist();
                            }
                        }
                    } else if (active_menu.type == 9) { // Finder Item Context Menu
                        if (active_window && active_window->title_is("Finder") && active_window->selected_item_idx != -1) {
                            int sel = active_window->selected_item_idx;
                            if (item == 0) { // Copy
                                clipboard_item_idx = sel;
                                clipboard_is_cut = false;
                            } else if (item == 1) { // Cut
                                clipboard_item_idx = sel;
                                clipboard_is_cut = true;
                            } else if (item == 2) { // Rename
                                is_renaming_item = true;
                            } else if (item == 3) { // Delete
                                for (int j = sel; j < desktop_item_count - 1; j++) {
                                    desktop_items[j] = desktop_items[j + 1];
                                }
                                desktop_item_count--;
                                active_window->selected_item_idx = -1;
                            }
                        }
                    } else if (active_menu.type == 10) { // Finder Empty Context Menu
                        if (active_window && active_window->title_is("Finder")) {
                            const char* active_dir = active_window->text_input;
                            if (active_window->text_len == 0) active_dir = "Desktop";
                            
                            if (item == 0) { // Paste
                                if (clipboard_item_idx != -1 && clipboard_item_idx < desktop_item_count) {
                                    DiskItem source = desktop_items[clipboard_item_idx];
                                    
                                    int dup_idx = -1;
                                    for (int j = 0; j < desktop_item_count; j++) {
                                        if (str_equal(desktop_items[j].path, active_dir) && str_equal(desktop_items[j].name, source.name)) {
                                            dup_idx = j;
                                            break;
                                        }
                                    }
                                    if (dup_idx != -1) {
                                        for (int j = dup_idx; j < desktop_item_count - 1; j++) {
                                            desktop_items[j] = desktop_items[j + 1];
                                        }
                                        desktop_item_count--;
                                        if (clipboard_item_idx > dup_idx) clipboard_item_idx--;
                                    }
                                    
                                    if (clipboard_is_cut) {
                                        desktop_items[clipboard_item_idx].path = active_dir;
                                        clipboard_item_idx = -1;
                                    } else {
                                        add_item(source.name, source.is_directory, source.is_terminal, active_dir, 0, 0);
                                    }
                                }
                            } else if (item == 1) { // New Folder
                                char folder_name[64];
                                int count = 1;
                                bool exists = true;
                                while (exists) {
                                    exists = false;
                                    char num_buf[16];
                                    int_to_str(count, num_buf, 16);
                                    int n_idx = 0;
                                    const char* prefix = "New Folder ";
                                    int p_idx = 0;
                                    while (prefix[p_idx]) { folder_name[n_idx++] = prefix[p_idx++]; }
                                    p_idx = 0;
                                    while (num_buf[p_idx]) { folder_name[n_idx++] = num_buf[p_idx++]; }
                                    folder_name[n_idx] = '\0';
                                    
                                    for (int i = 0; i < desktop_item_count; i++) {
                                        if (str_equal(desktop_items[i].path, active_dir) && str_equal(desktop_items[i].name, folder_name)) {
                                            exists = true;
                                            break;
                                        }
                                    }
                                    count++;
                                }
                                add_item(folder_name, true, false, active_dir, 0, 0);
                            } else if (item == 2) { // New File
                                char file_name[64];
                                int count = 1;
                                bool exists = true;
                                while (exists) {
                                    exists = false;
                                    char num_buf[16];
                                    int_to_str(count, num_buf, 16);
                                    int n_idx = 0;
                                    const char* prefix = "New File ";
                                    int p_idx = 0;
                                    while (prefix[p_idx]) { file_name[n_idx++] = prefix[p_idx++]; }
                                    p_idx = 0;
                                    while (num_buf[p_idx]) { file_name[n_idx++] = num_buf[p_idx++]; }
                                    file_name[n_idx++] = '.'; file_name[n_idx++] = 't'; file_name[n_idx++] = 'x'; file_name[n_idx++] = 't';
                                    file_name[n_idx] = '\0';
                                    
                                    for (int i = 0; i < desktop_item_count; i++) {
                                        if (str_equal(desktop_items[i].path, active_dir) && str_equal(desktop_items[i].name, file_name)) {
                                            exists = true;
                                            break;
                                        }
                                    }
                                    count++;
                                }
                                add_item(file_name, false, false, active_dir, 0, 0);
                            }
                        }
                    }
                }
                if (active_menu.type == 2 || active_menu.type == 9 || active_menu.type == 10 || active_menu.type == 11) {
                    trigger_host_persist();
                }
                force_redraw_all();
            } else {
                active_menu.active = false;
                force_redraw_all();
            }
            state_updated = true;
        } else if (new_y <= MENU_BAR_HEIGHT) {
            if (new_x >= 10 && new_x <= 80) { // Crecent
                active_menu = {true, 10, MENU_BAR_HEIGHT, MENU_WIDTH, 3 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 0, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 85 && new_x <= 135) { // Finder
                active_menu = {true, 85, MENU_BAR_HEIGHT, MENU_WIDTH, 2 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 1, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 145 && new_x <= 185) { // File
                active_menu = {true, 145, MENU_BAR_HEIGHT, MENU_WIDTH, 2 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 3, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 185 && new_x <= 225) { // Edit
                active_menu = {true, 185, MENU_BAR_HEIGHT, MENU_WIDTH, 4 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 4, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 225 && new_x <= 270) { // View
                active_menu = {true, 225, MENU_BAR_HEIGHT, MENU_WIDTH, 2 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 5, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 270 && new_x <= 305) { // Go
                active_menu = {true, 270, MENU_BAR_HEIGHT, MENU_WIDTH, 4 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 6, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 305 && new_x <= 365) { // Window
                active_menu = {true, 305, MENU_BAR_HEIGHT, MENU_WIDTH, 3 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 7, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 365 && new_x <= 410) { // Help
                active_menu = {true, 365, MENU_BAR_HEIGHT, MENU_WIDTH, 2 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 8, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= width - 280 && new_x <= width - 210) { // Wifi toggle
                wifi_connected = !wifi_connected;
                drivers::Serial::print("[WIFI] Status changed: ");
                drivers::Serial::println(wifi_connected ? "Enabled. Connected to CrecentSecure_5G." : "Disabled.");
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= width - 190 && new_x <= width - 110) { // USB toggle
                usb_connected = !usb_connected;
                drivers::Serial::print("[USB] Status changed: ");
                if (usb_connected) {
                    drivers::Serial::println("Mass Storage device connected. Mounted at /usb.");
                    add_item("USB_DRIVE", true, false, "Desktop", 200, 60);
                    add_item("firmware.bin", false, false, "USB_DRIVE", 0, 0);
                    add_item("backup.zip", false, false, "USB_DRIVE", 0, 0);
                } else {
                    drivers::Serial::println("Mass Storage device safely removed.");
                    for (int j = 0; j < desktop_item_count; ) {
                        if (str_equal(desktop_items[j].name, "USB_DRIVE") || str_equal(desktop_items[j].path, "USB_DRIVE")) {
                            for (int k = j; k < desktop_item_count - 1; k++) {
                                desktop_items[k] = desktop_items[k + 1];
                            }
                            desktop_item_count--;
                        } else {
                            j++;
                        }
                    }
                }
                force_redraw_all();
                state_updated = true;
            }
        } else {
            // Fix 8: Accurate Z-order window picking. Check exact windows first, fallback to title bar.
            Window* clicked = nullptr;
            for (Window* w = window_list_head; w; w = w->next) {
                if (!w->is_minimized && in_rect(new_x, new_y, w->rect)) clicked = w;
            }
            if (!clicked) {
                for (Window* w = window_list_head; w; w = w->next) {
                    if (!w->is_minimized && in_rect(new_x, new_y, w->rect.x, w->rect.y, w->rect.w, TITLE_BAR_HEIGHT)) clicked = w;
                }
            }

            if (clicked) {
                int wx = clicked->rect.x;
                int wy = clicked->rect.y;
                if (!clicked->is_maximized && in_rect(new_x, new_y, wx + clicked->rect.w - 16, wy + clicked->rect.h - 16, 16, 16)) {
                    active_window = clicked;
                    is_resizing_window = true;
                    resize_start_w = clicked->rect.w;
                    resize_start_h = clicked->rect.h;
                    click_start_x = new_x;
                    click_start_y = new_y;
                    focus_window(clicked);
                    force_redraw_all();
                } else if (in_rect(new_x, new_y, wx + 13, wy + 9, 12, 12)) {
                    close_window(clicked->id);
                    force_redraw_all();
                } else if (in_rect(new_x, new_y, wx + 33, wy + 9, 12, 12)) {
                    clicked->is_minimized = true;
                    force_redraw_all();
                } else if (in_rect(new_x, new_y, wx + 53, wy + 9, 12, 12)) {
                    if (clicked->is_maximized) {
                        clicked->rect = clicked->orig_rect;
                        clicked->is_maximized = false;
                    } else {
                        clicked->orig_rect = clicked->rect;
                        clicked->rect = {0, MENU_BAR_HEIGHT, width, height - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM - MENU_BAR_HEIGHT - 16};
                        clicked->is_maximized = true;
                    }
                    focus_window(clicked);
                    force_redraw_all();
                } else if (in_rect(new_x, new_y, wx, wy, clicked->rect.w, TITLE_BAR_HEIGHT)) {
                    active_window = clicked;
                    clicked->is_dragging = true;
                    if (clicked->is_maximized) {
                        // Fix 6: Restore window gracefully from maximized state while preserving drag proportion
                        clicked->is_maximized = false;
                        clicked->rect = clicked->orig_rect;
                        clicked->rect.x = new_x - clicked->rect.w / 2;
                        clicked->rect.y = new_y - 20; 
                        if (clicked->rect.x < 0) clicked->rect.x = 0;
                        if (clicked->rect.x + clicked->rect.w > width) clicked->rect.x = width - clicked->rect.w;
                        if (clicked->rect.y < 0) clicked->rect.y = 0;
                    }
                    drag_offset_x = new_x - clicked->rect.x;
                    drag_offset_y = new_y - clicked->rect.y;
                    
                    drag_preview_x = clicked->rect.x;
                    drag_preview_y = clicked->rect.y;
                    drag_preview_w = clicked->rect.w;
                    drag_preview_h = clicked->rect.h;
                    
                    int t_len = 0;
                    while (clicked->title[t_len] && t_len < 120) {
                        drag_preview_title[t_len] = clicked->title[t_len];
                        t_len++;
                    }
                    drag_preview_title[t_len] = '\0';
                    
                    focus_window(clicked);
                    force_redraw_all();
                } else {
                    active_window = clicked;
                    focus_window(clicked);
                    
                    int rx = new_x - (clicked->rect.x + 1);
                    int ry = new_y - (clicked->rect.y + TITLE_BAR_HEIGHT);
                    
                    if (clicked->title_is("System Settings")) {
                        float scales[] = {1.0f, 1.25f, 1.5f, 2.0f};
                        for (int i = 0; i < 4; ++i) {
                            int bx = scale_dim(20 + i * 80);
                            int by = scale_dim(40);
                            int bw = scale_dim(70);
                            int bh = scale_dim(26);
                            if (rx >= bx && rx < bx + bw && ry >= by && ry < by + bh) {
                                set_ui_scale(scales[i]);
                                trigger_host_persist();
                                break;
                            }
                        }
                        
                        for (int i = 0; i < 2; ++i) {
                            int bx = scale_dim(20 + i * 130);
                            int by = scale_dim(120);
                            int bw = scale_dim(110);
                            int bh = scale_dim(26);
                            if (rx >= bx && rx < bx + bw && ry >= by && ry < by + bh) {
                                set_theme(i == 1);
                                trigger_host_persist();
                                break;
                            }
                        }
                        
                        for (int i = 0; i < 5; ++i) {
                            int bx = scale_dim(20 + i * 90);
                            int by = scale_dim(200);
                            int bw = scale_dim(80);
                            int bh = scale_dim(26);
                            if (rx >= bx && rx < bx + bw && ry >= by && ry < by + bh) {
                                drivers::Framebuffer::set_wallpaper_theme(i);
                                wallpaper_theme_id = i;
                                trigger_host_persist();
                                break;
                            }
                        }

                        if (rx >= scale_dim(380) && rx < scale_dim(460) && ry >= scale_dim(310) && ry < scale_dim(332)) {
                            wifi_connected = !wifi_connected;
                            drivers::Serial::print("[WIFI] Network CrecentSecure_5G status changed: ");
                            drivers::Serial::println(wifi_connected ? "Connected." : "Forgotten.");
                            trigger_host_persist();
                        }
                        if (!wifi_connected && rx >= scale_dim(380) && rx < scale_dim(460) && ry >= scale_dim(340) && ry < scale_dim(362)) {
                            wifi_connected = true;
                            drivers::Serial::println("[WIFI] Network AmazingGuest status changed: Connected.");
                            trigger_host_persist();
                        }
                        if (!wifi_connected && rx >= scale_dim(380) && rx < scale_dim(460) && ry >= scale_dim(370) && ry < scale_dim(392)) {
                            wifi_connected = true;
                            drivers::Serial::println("[WIFI] Network Direct-OS-Printer status changed: Connected.");
                            trigger_host_persist();
                        }
                    } else if (clicked->title_is("Safari")) {
                        int client_w = clicked->rect.w - 2;
                        bool link_clicked = false;
                        for (int i = 0; i < safari_link_count; i++) {
                            if (in_rect(new_x, new_y, safari_links[i].rect)) {
                                int l_idx = 0;
                                while (safari_links[i].target[l_idx] && l_idx < 120) {
                                    safari_url[l_idx] = safari_links[i].target[l_idx];
                                    l_idx++;
                                }
                                safari_url[l_idx] = '\0';
                                safari_search_active = false;
                                safari_focused_address = false;
                                safari_focused_search = false;
                                link_clicked = true;
                                break;
                            }
                        }
                        if (!link_clicked) {
                            if (rx >= 84 && rx < 84 + client_w - 110 && ry >= 7 && ry < 7 + 22) {
                                safari_focused_address = true;
                                safari_focused_search = false;
                            } else if ((str_equal(safari_url, "google.com") || str_equal(safari_url, "www.google.com")) &&
                                       rx >= client_w / 2 - 160 && rx < client_w / 2 + 160 && ry >= 165 && ry < 195) {
                                safari_focused_address = false;
                                safari_focused_search = true;
                            }
                        }
                        force_redraw_all();
                    } else if (clicked->title_is("Finder")) {
                        if (ry >= 0 && ry < 35) {
                            const char* active_dir = clicked->text_input;
                            if (clicked->text_len == 0) active_dir = "Desktop";

                            if (rx >= 10 && rx < 70) { // Back button
                                int len = clicked->text_len;
                                int last_slash = -1;
                                for (int i = 0; i < len; i++) {
                                    if (clicked->text_input[i] == '/') last_slash = i;
                                }
                                if (last_slash != -1) {
                                    clicked->text_input[last_slash] = '\0';
                                    clicked->text_len = last_slash;
                                }
                            } else if (rx >= clicked->rect.w - 180 && rx < clicked->rect.w - 100) { // + Folder
                                char folder_name[64];
                                int count = 1;
                                bool exists = true;
                                while (exists) {
                                    exists = false;
                                    char num_buf[16];
                                    int_to_str(count, num_buf, 16);
                                    int n_idx = 0;
                                    const char* prefix = "New Folder ";
                                    int p_idx = 0;
                                    while (prefix[p_idx]) { folder_name[n_idx++] = prefix[p_idx++]; }
                                    p_idx = 0;
                                    while (num_buf[p_idx]) { folder_name[n_idx++] = num_buf[p_idx++]; }
                                    folder_name[n_idx] = '\0';
                                    
                                    for (int i = 0; i < desktop_item_count; i++) {
                                        if (str_equal(desktop_items[i].path, active_dir) && str_equal(desktop_items[i].name, folder_name)) {
                                            exists = true;
                                            break;
                                        }
                                    }
                                    count++;
                                }
                                add_item(folder_name, true, false, active_dir, 0, 0);
                            } else if (rx >= clicked->rect.w - 90 && rx < clicked->rect.w - 10) { // + File
                                char file_name[64];
                                int count = 1;
                                bool exists = true;
                                while (exists) {
                                    exists = false;
                                    char num_buf[16];
                                    int_to_str(count, num_buf, 16);
                                    int n_idx = 0;
                                    const char* prefix = "New File ";
                                    int p_idx = 0;
                                    while (prefix[p_idx]) { file_name[n_idx++] = prefix[p_idx++]; }
                                    p_idx = 0;
                                    while (num_buf[p_idx]) { file_name[n_idx++] = num_buf[p_idx++]; }
                                    file_name[n_idx++] = '.'; file_name[n_idx++] = 't'; file_name[n_idx++] = 'x'; file_name[n_idx++] = 't';
                                    file_name[n_idx] = '\0';
                                    
                                    for (int i = 0; i < desktop_item_count; i++) {
                                        if (str_equal(desktop_items[i].path, active_dir) && str_equal(desktop_items[i].name, file_name)) {
                                            exists = true;
                                            break;
                                        }
                                    }
                                    count++;
                                }
                                add_item(file_name, false, false, active_dir, 0, 0);
                            }
                            force_redraw_all();
                        } else if (rx >= 8 && rx < 142) {
                            for (int i = 0; i < 3; i++) {
                                int iy = 40 + i * 30;
                                if (ry >= iy - 4 && ry < iy + 20) {
                                    const char* favorites[] = {"Desktop", "Applications", "Documents"};
                                    int len = 0;
                                    while (favorites[i][len] && len < 250) {
                                        clicked->text_input[len] = favorites[i][len];
                                        len++;
                                    }
                                    clicked->text_input[len] = '\0';
                                    clicked->text_len = len;
                                    force_redraw_all();
                                    break;
                                }
                            }
                        } else if (rx >= 152) {
                            const char* active_dir = clicked->text_input;
                            if (clicked->text_len == 0) active_dir = "Desktop";
                            
                            int grid_col = 0;
                            int grid_row = 0;
                            bool clicked_item = false;
                            for (int i = 0; i < desktop_item_count; i++) {
                                DiskItem& it = desktop_items[i];
                                if (str_equal(it.path, active_dir)) {
                                    int ix = 170 + grid_col * 90;
                                    int iy = 55 + grid_row * 90;
                                    if (rx >= ix && rx < ix + 64 && ry >= iy && ry < iy + 80) {
                                        clicked_item = true;
                                        
                                        bool is_double_click = (last_finder_clicked_item_idx == i && (frame_counter - last_finder_click_time) < 30);
                                        last_finder_click_time = frame_counter;
                                        last_finder_clicked_item_idx = i;
                                        
                                        if (is_double_click) {
                                            if (it.is_directory) {
                                                int len = 0;
                                                while (clicked->text_input[len]) len++;
                                                if (len > 0) {
                                                    clicked->text_input[len++] = '/';
                                                }
                                                int name_idx = 0;
                                                while (it.name[name_idx] && len < 250) {
                                                    clicked->text_input[len++] = it.name[name_idx++];
                                                }
                                                clicked->text_input[len] = '\0';
                                                clicked->text_len = len;
                                                clicked->selected_item_idx = -1;
                                            } else {
                                                char full_path[256];
                                                int fp_idx = 0;
                                                if (str_equal(active_dir, "Desktop") || str_equal(active_dir, "Applications") || str_equal(active_dir, "Documents")) {
                                                    if (str_equal(it.name, "hello.txt")) {
                                                        const char* hello_p = "/tar/hello.txt";
                                                        while (hello_p[fp_idx]) { full_path[fp_idx] = hello_p[fp_idx]; fp_idx++; }
                                                    } else if (str_equal(it.name, "info.txt")) {
                                                        const char* info_p = "/tar/docs/info.txt";
                                                        while (info_p[fp_idx]) { full_path[fp_idx] = info_p[fp_idx]; fp_idx++; }
                                                    } else {
                                                        const char* prefix = "/";
                                                        while (prefix[fp_idx]) { full_path[fp_idx] = prefix[fp_idx]; fp_idx++; }
                                                        int n_idx = 0;
                                                        while (it.name[n_idx]) { full_path[fp_idx++] = it.name[n_idx++]; }
                                                    }
                                                } else {
                                                    const char* prefix = "/";
                                                    while (prefix[fp_idx]) { full_path[fp_idx] = prefix[fp_idx]; fp_idx++; }
                                                    int d_idx = 0;
                                                    while (active_dir[d_idx]) { full_path[fp_idx++] = active_dir[d_idx++]; }
                                                    full_path[fp_idx++] = '/';
                                                    int n_idx = 0;
                                                    while (it.name[n_idx]) { full_path[fp_idx++] = it.name[n_idx++]; }
                                                }
                                                full_path[fp_idx] = '\0';
                                                
                                                char edit_title[128] = "Code Editor - ";
                                                int t_idx = 14;
                                                int f_len = 0;
                                                while (full_path[f_len]) {
                                                    edit_title[t_idx++] = full_path[f_len++];
                                                }
                                                edit_title[t_idx] = '\0';
                                                
                                                if (it.is_terminal) {
                                                    create_window((width - 500)/2, (height - 350)/2, 500, 350, "Terminal", C_TERMINAL);
                                                } else if (str_equal(it.name, "System Settings")) {
                                                    create_window((width - 500)/2, (height - 420)/2, 500, 420, "System Settings", 0x00FFFFFF);
                                                } else {
                                                    Window* code_win = create_window((width - 650)/2, (height - 450)/2, 650, 450, edit_title, 0x001E1E1E);
                                                    if (code_win) {
                                                        fs::VFSNode* node = fs::VFS::open(full_path);
                                                        if (node) {
                                                            fs::File f;
                                                            f.node = node;
                                                            f.offset = 0;
                                                            ssize_t bytes = fs::VFS::read(&f, code_win->text_input, sizeof(code_win->text_input) - 1);
                                                            if (bytes > 0) {
                                                                code_win->text_input[bytes] = '\0';
                                                                code_win->text_len = bytes;
                                                            } else {
                                                                code_win->text_input[0] = '\0';
                                                                code_win->text_len = 0;
                                                            }
                                                        } else {
                                                            code_win->text_input[0] = '\0';
                                                            code_win->text_len = 0;
                                                        }
                                                    }
                                                }
                                            }
                                        } else {
                                            clicked->selected_item_idx = i;
                                            is_renaming_item = false;
                                        }
                                        break;
                                    }
                                    grid_col++;
                                    if (grid_col >= 3) {
                                        grid_col = 0;
                                        grid_row++;
                                    }
                                }
                            }
                            if (!clicked_item) {
                                clicked->selected_item_idx = -1;
                                is_renaming_item = false;
                            }
                        }
                    } else if (clicked->title_starts_with("Code Editor")) {
                        if (rx >= 10 && rx < 70 && ry >= 5 && ry < 29) {
                            const char* full_path = clicked->title + 14;
                            fs::VFSNode* node = fs::VFS::open(full_path);
                            if (node) {
                                fs::File f;
                                f.node = node;
                                f.offset = 0;
                                fs::VFS::write(&f, clicked->text_input, clicked->text_len);
                                drivers::Serial::print("[EDITOR] Saved back to ");
                                drivers::Serial::println(full_path);
                                trigger_host_persist();
                            }
                        }
                    } else {
                        int client_w = clicked->rect.w - 2;
                        int client_h = clicked->rect.h - TITLE_BAR_HEIGHT - 12;
                        if (rx >= 0 && rx < client_w && ry >= 0 && ry < client_h) {
                            clicked->pending_event.type = 1; // EVENT_MOUSE_CLICK
                            clicked->pending_event.mx = rx;
                            clicked->pending_event.my = ry;
                        }
                    }

                    force_redraw_all();
                }
                state_updated = true;
            } else {
                // Fix 9: Dock hitbox encompasses the magnified area extending vertically
                int dock_y = height - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM;
                if (in_rect(new_x, new_y, (width - DOCK_WIDTH) / 2, dock_y - 40, DOCK_WIDTH, DOCK_HEIGHT + 40)) {
                    int total_icons_w = 6 * DOCK_ICON_SIZE + 5 * DOCK_ICON_SPACING;
                    int start_x = (width - DOCK_WIDTH) / 2 + (DOCK_WIDTH - total_icons_w) / 2;

                    auto dock_app = [&](int idx, const char* title, uint32_t color) {
                        int ix = start_x + idx * (DOCK_ICON_SIZE + DOCK_ICON_SPACING);
                        if (in_rect(new_x, new_y, ix, dock_y - 40, DOCK_ICON_SIZE, DOCK_HEIGHT + 40)) {
                            bool open = false;
                            for (Window* w = window_list_head; w; w = w->next) {
                                if (w->title_is(title)) {
                                    open = true;
                                    w->is_minimized = false;
                                    focus_window(w);
                                    break;
                                }
                            }
                            if (!open) create_window(200 + idx * 40, 150 + idx * 20, 500, 350, title, color);
                        }
                    };
                    dock_app(0, "Finder", C_FINDER);
                    dock_app(1, "Safari", C_FINDER);
                    dock_app(2, "Mail", C_FINDER);
                    dock_app(3, "Terminal", C_TERMINAL);
                    dock_app(4, "App Store", 0x001B72E8);
                    dock_app(5, "Notes", 0x00FFFCEB);
                    force_redraw_all();
                    state_updated = true;
                } else {
                    active_menu.active = false;
                    
                    active_window = nullptr; // Unfocus windows when clicking desktop
                    
                    bool icon_clicked = false;
                    for (int i = 0; i < desktop_item_count; i++) {
                        DiskItem& it = desktop_items[i];
                        if (str_equal(it.path, "Desktop")) {
                            if (in_rect(new_x, new_y, it.x, it.y, 64, 80)) {
                                bool is_double_click = (last_finder_clicked_item_idx == i && (frame_counter - last_finder_click_time) < 30);
                                last_finder_click_time = frame_counter;
                                last_finder_clicked_item_idx = i;

                                for (int j = 0; j < desktop_item_count; j++) {
                                    desktop_items[j].selected = false;
                                }
                                it.selected = true;

                                if (is_double_click) {
                                    if (it.is_terminal) {
                                        create_window((width - 500)/2, (height - 350)/2, 500, 350, "Terminal", C_TERMINAL);
                                    } else if (str_equal(it.name, "System Settings")) {
                                        create_window((width - 500)/2, (height - 420)/2, 500, 420, "System Settings", 0x00FFFFFF);
                                    } else {
                                        char edit_title[128] = "Code Editor - /";
                                        int t_idx = 15;
                                        int n_idx = 0;
                                        while (it.name[n_idx]) { edit_title[t_idx++] = it.name[n_idx++]; }
                                        edit_title[t_idx] = '\0';
                                        
                                        Window* code_win = create_window((width - 650)/2, (height - 450)/2, 650, 450, edit_title, 0x001E1E1E);
                                        if (code_win) {
                                            char full_path[128] = "/";
                                            if (str_equal(it.name, "hello.txt")) {
                                                const char* hello_p = "tar/hello.txt";
                                                int hp_idx = 0;
                                                while (hello_p[hp_idx]) { full_path[1 + hp_idx] = hello_p[hp_idx]; hp_idx++; }
                                                full_path[1 + hp_idx] = '\0';
                                            } else {
                                                int n_idx2 = 0;
                                                while (it.name[n_idx2]) { full_path[1 + n_idx2] = it.name[n_idx2]; n_idx2++; }
                                                full_path[1 + n_idx2] = '\0';
                                            }
                                            fs::VFSNode* node = fs::VFS::open(full_path);
                                            if (node) {
                                                fs::File f;
                                                f.node = node;
                                                f.offset = 0;
                                                ssize_t bytes = fs::VFS::read(&f, code_win->text_input, sizeof(code_win->text_input) - 1);
                                                if (bytes > 0) {
                                                    code_win->text_input[bytes] = '\0';
                                                    code_win->text_len = bytes;
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    dragged_desktop_item_idx = i;
                                    drag_offset_x = new_x - it.x;
                                    drag_offset_y = new_y - it.y;
                                }
                                icon_clicked = true;
                                break;
                            }
                        }
                    }
                    if (!icon_clicked) {
                        for (int j = 0; j < desktop_item_count; j++) {
                            desktop_items[j].selected = false;
                        }
                        is_dragging_selection = true;
                        force_redraw_all();
                    }
                }
            }
        }
    }

    // 3. Dragging Logic
    if (!state_updated && left_pressed && position_changed) {
        if (active_window && active_window->is_dragging) {
            Rect old_rect = active_window->rect;
            int next_x = new_x - drag_offset_x;
            int next_y = new_y - drag_offset_y;

            if (next_y < MENU_BAR_HEIGHT) next_y = MENU_BAR_HEIGHT;
            if (next_y > height - 40) next_y = height - 40;
            if (next_x < -active_window->rect.w + 40) next_x = -active_window->rect.w + 40;
            if (next_x > width - 40) next_x = width - 40;
            
            if (next_x != old_rect.x || next_y != old_rect.y) {
                active_window->rect.x = next_x;
                active_window->rect.y = next_y;

                // Combine old and new positions into a single union bounding box.
                // This prevents tearing (where the background is drawn, screen refreshes, then window is drawn)
                // and cuts the rendering workload in half for overlapping regions.
                int min_x = old_rect.x < next_x ? old_rect.x : next_x;
                int min_y = old_rect.y < next_y ? old_rect.y : next_y;
                int max_x = (old_rect.x + old_rect.w) > (next_x + active_window->rect.w) ? 
                            (old_rect.x + old_rect.w) : (next_x + active_window->rect.w);
                int max_y = (old_rect.y + old_rect.h) > (next_y + active_window->rect.h) ? 
                            (old_rect.y + old_rect.h) : (next_y + active_window->rect.h);

                // Add 32px padding for shadows
                Rect union_dirty = {
                    min_x - 32, 
                    min_y - 32, 
                    (max_x - min_x) + 64, 
                    (max_y - min_y) + 64
                };

                redraw_dirty_rect(union_dirty);
                
                state_updated = true;
            }
        } else if (active_window && is_resizing_window) {
            int next_w = resize_start_w + (new_x - click_start_x);
            int next_h = resize_start_h + (new_y - click_start_y);
            
            if (next_w < 200) next_w = 200;
            if (next_h < 150) next_h = 150;
            
            if (next_w != active_window->rect.w || next_h != active_window->rect.h) {
                int old_w = active_window->rect.w;
                int old_h = active_window->rect.h;
                
                active_window->rect.w = next_w;
                active_window->rect.h = next_h;
                
                int max_w = old_w > next_w ? old_w : next_w;
                int max_h = old_h > next_h ? old_h : next_h;
                
                dirty = {active_window->rect.x - 32, active_window->rect.y - 32, max_w + 64, max_h + 64};
                needs_redraw = true;
            }
        } else if (dragged_desktop_item_idx != -1) {
            DiskItem& it = desktop_items[dragged_desktop_item_idx];
            int old_ix = it.x;
            int old_iy = it.y;
            int next_ix = new_x - drag_offset_x;
            int next_iy = new_y - drag_offset_y;
            
            if (next_ix < 0) next_ix = 0;
            if (next_ix > width - 64) next_ix = width - 64;
            if (next_iy < MENU_BAR_HEIGHT) next_iy = MENU_BAR_HEIGHT;
            if (next_iy > height - 80) next_iy = height - 80;
            
            if (next_ix != old_ix || next_iy != old_iy) {
                it.x = next_ix;
                it.y = next_iy;
                
                int x_min = old_ix < next_ix ? old_ix : next_ix;
                int y_min = old_iy < next_iy ? old_iy : next_iy;
                int x_max = (old_ix + 64 > next_ix + 64) ? old_ix + 64 : next_ix + 64;
                int y_max = (old_iy + 80 > next_iy + 80) ? old_iy + 80 : next_iy + 80;
                
                x_min -= 16; y_min -= 16;
                x_max += 16; y_max += 16;
                
                if (x_min < 0) x_min = 0;
                if (y_min < 0) y_min = 0;
                if (x_max > width) x_max = width;
                if (y_max > height) y_max = height;
                
                dirty = {x_min, y_min, x_max - x_min, y_max - y_min};
                needs_redraw = true;
            }
        } else if (is_dragging_selection) {
            int x1 = click_start_x < mouse_x ? click_start_x : mouse_x;
            int y1 = click_start_y < mouse_y ? click_start_y : mouse_y;
            int x2 = click_start_x > mouse_x ? click_start_x : mouse_x;
            int y2 = click_start_y > mouse_y ? click_start_y : mouse_y;
            
            for (int i = 0; i < desktop_item_count; i++) {
                DiskItem& it = desktop_items[i];
                if (str_equal(it.path, "Desktop")) {
                    int ix = it.x;
                    int iy = it.y;
                    if (ix + 64 > x1 && ix < x2 && iy + 80 > y1 && iy < y2) {
                        it.selected = true;
                    } else {
                        it.selected = false;
                    }
                }
            }
            force_redraw_all();
            state_updated = true;
        }
    }

    // 4. Left Release
    if (!state_updated && left_up) {
        if (active_window && active_window->is_dragging) {
            active_window->is_dragging = false;
            force_redraw_all();
            state_updated = true;
            trigger_host_persist();
        }
        if (active_window && is_resizing_window) {
            is_resizing_window = false;
            force_redraw_all();
            state_updated = true;
            trigger_host_persist();
        }
        if (dragged_desktop_item_idx != -1) {
            int dx = new_x - click_start_x;
            int dy = new_y - click_start_y;
            if (dx * dx + dy * dy < 25) {
                DiskItem& it = desktop_items[dragged_desktop_item_idx];
                if (it.is_directory) {
                    Window* w = create_window(200, 150, 500, 350, "Finder", C_FINDER);
                    if (w) {
                        int len = 0;
                        while (it.name[len] && len < 250) {
                            w->text_input[len] = it.name[len];
                            len++;
                        }
                        w->text_input[len] = '\0';
                        w->text_len = len;
                    }
                } else {
                    if (it.is_terminal) {
                        create_window((width - 500)/2, (height - 350)/2, 500, 350, "Terminal", C_TERMINAL);
                    } else {
                        create_window((width - 600)/2, (height - 400)/2, 600, 400, "Notes", 0x00FFFCEB);
                    }
                }
            }
            dragged_desktop_item_idx = -1;
            force_redraw_all();
            state_updated = true;
            trigger_host_persist();
        }
        if (is_dragging_selection) {
            is_dragging_selection = false;
            force_redraw_all();
            state_updated = true;
        }
    }

    // 5. Menu Hover Tracking
    if (!state_updated && active_menu.active) {
        int old_hover = active_menu.hovered_item;
        if (in_rect(new_x, new_y, active_menu.x, active_menu.y, active_menu.w, active_menu.h)) {
            int max_idx = 2;
            if (active_menu.type == 0) max_idx = 3;
            else if (active_menu.type == 4 || active_menu.type == 6 || active_menu.type == 2 || active_menu.type == 11) max_idx = 4;
            else if (active_menu.type == 7) max_idx = 3;
            int idx = -1;
            // Fix 21: Iterate over exact item boxes to avoid off-by-one in 2px gaps
            for (int i = 0; i < max_idx; i++) {
                int iy = active_menu.y + MENU_PADDING + i * MENU_ITEM_HEIGHT;
                if (in_rect(new_x, new_y, active_menu.x, iy, active_menu.w, 24)) {
                    idx = i; break;
                }
            }
            active_menu.hovered_item = idx;
        } else {
            active_menu.hovered_item = -1;
        }
        if (active_menu.hovered_item != old_hover) {
            dirty = {active_menu.x - 4, active_menu.y - 4, active_menu.w + 8, active_menu.h + 8};
            needs_redraw = true;
        }
    }

    // 5.5 Dock Hover Tracking
    if (!state_updated && !active_window && !active_menu.active) {
        int dock_y = height - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM;
        bool old_hover = (old_y >= dock_y - 40);
        bool new_hover = (new_y >= dock_y - 40);
        if (new_hover || old_hover) {
            dirty = {0, dock_y - 40, width, 160};
            needs_redraw = true;
        }
    }

    // 6. Execute Redraw If Required
    if (!state_updated) {
        if (needs_redraw) {
            if (dirty.w > 0 && dirty.h > 0) {
                redraw_dirty_rect(dirty);
                state_updated = true;
            }
        } else if (position_changed) {
            // 7. Pure cursor move optimization
            int x_min = old_x < mouse_x ? old_x : mouse_x;
            int y_min = old_y < mouse_y ? old_y : mouse_y;
            int x_max = (old_x + 24 > mouse_x + 24) ? old_x + 24 : mouse_x + 24;
            int y_max = (old_y + 28 > mouse_y + 28) ? old_y + 28 : mouse_y + 28;

            dirty = {x_min, y_min, x_max - x_min, y_max - y_min};
            redraw_dirty_rect(dirty);
            state_updated = true;
        }
    }

    mouse_pressed = left_pressed;
    right_mouse_pressed = right_pressed;
    last_mouse_x = mouse_x;
    last_mouse_y = mouse_y;
}

void WindowManager::tick() {
    frame_counter++;
    if (frame_counter % 60 == 0) {
        for (int i = 0; i < 19; i++) {
            perf_cpu_history[i] = perf_cpu_history[i + 1];
        }
        int noise = (frame_counter * 13 + 7) % 15 - 7;
        perf_current_cpu = perf_current_cpu + noise;
        if (perf_current_cpu < 5) perf_current_cpu = 5;
        if (perf_current_cpu > 95) perf_current_cpu = 95;
        perf_cpu_history[19] = (perf_current_cpu * 50) / 100;

        for (Window* w = window_list_head; w; w = w->next) {
            if (w->title_is("Performance Monitor") && !w->is_minimized) {
                Rect dirty = {
                    w->rect.x - 24, w->rect.y - 24,
                    w->rect.w + 48, w->rect.h + 48
                };
                redraw_dirty_rect(dirty);
            }
        }
    }

    if (frame_counter % 20 == 0) {
        if (active_window && (active_window->title_is("Terminal") || active_window->title_is("Notes"))) {
            Rect dirty = {
                active_window->rect.x - 24, active_window->rect.y - 24,
                active_window->rect.w + 48, active_window->rect.h + 48
            };
            redraw_dirty_rect(dirty);
        }
    }
}

void WindowManager::handle_key_press(char c) {
    if (active_window) {
        active_window->pending_event.type = 2; // EVENT_KEYBOARD
        active_window->pending_event.key = c;

        if (active_window->title_is("Terminal")) {
            if (c == '\b') {
                if (active_window->text_len > 0) {
                    active_window->text_len--;
                    active_window->text_input[active_window->text_len] = '\0';
                }
            } else if (c == '\n') {
                char cmd[256];
                int l = 0;
                while (active_window->text_input[l] && l < 250) {
                    cmd[l] = active_window->text_input[l];
                    l++;
                }
                cmd[l] = '\0';

                char prompt_hist[300] = "crecent@macos:~$ ";
                int p_idx = 17;
                l = 0;
                while (cmd[l]) { prompt_hist[p_idx++] = cmd[l++]; }
                prompt_hist[p_idx] = '\0';
                terminal_println(prompt_hist);

                if (str_equal(cmd, "help")) {
                    terminal_println("Available commands: ls, cat, neofetch, clear, help");
                } else if (str_equal(cmd, "neofetch")) {
                    terminal_println("OS: CrecentOS Whitesur 2.0 (x86_64)");
                    terminal_println("Kernel: Freestanding microkernel core");
                    terminal_println("Memory: 42MB used / 512MB physical RAM");
                } else if (str_equal(cmd, "ls")) {
                    terminal_println("hello.txt    info.txt    Applications    Documents");
                } else if (str_equal(cmd, "cat hello.txt")) {
                    terminal_println("TarFS validation hello world message!");
                } else if (str_equal(cmd, "cat info.txt")) {
                    terminal_println("Hierarchical directories in TarFS are fully working.");
                } else if (str_equal(cmd, "clear")) {
                    terminal_line_count = 0;
                } else if (cmd[0] != '\0') {
                    char err[300] = "";
                    int e_idx = 0;
                    l = 0;
                    while (cmd[l]) { err[e_idx++] = cmd[l++]; }
                    const char* suffix = ": command not found";
                    l = 0;
                    while (suffix[l]) { err[e_idx++] = suffix[l++]; }
                    err[e_idx] = '\0';
                    terminal_println(err);
                }

                active_window->text_len = 0;
                active_window->text_input[0] = '\0';
            } else if (c >= 32 && c <= 126 && active_window->text_len < 250) {
                active_window->text_input[active_window->text_len++] = c;
                active_window->text_input[active_window->text_len] = '\0';
            }
            Rect dirty = {
                active_window->rect.x - 4, active_window->rect.y - 4,
                active_window->rect.w + 8, active_window->rect.h + 8
            };
            redraw_dirty_rect(dirty);
        } else if (active_window->title_is("Notes")) {
            if (c == '\b') {
                if (active_window->text_len > 0) {
                    active_window->text_len--;
                    active_window->text_input[active_window->text_len] = '\0';
                }
            } else if (c >= 32 && c <= 126 && active_window->text_len < 250) {
                active_window->text_input[active_window->text_len++] = c;
                active_window->text_input[active_window->text_len] = '\0';
            }
            Rect dirty = {
                active_window->rect.x - 4, active_window->rect.y - 4,
                active_window->rect.w + 8, active_window->rect.h + 8
            };
            redraw_dirty_rect(dirty);
        } else if (active_window->title_is("Safari")) {
            if (safari_focused_address) {
                int len = 0;
                while (safari_url[len]) len++;
                if (c == '\b') {
                    if (len > 0) safari_url[len - 1] = '\0';
                } else if (c == '\n') {
                    safari_search_active = false;
                } else if (c >= 32 && c <= 126 && len < 120) {
                    safari_url[len] = c;
                    safari_url[len + 1] = '\0';
                }
            } else if (safari_focused_search) {
                int len = 0;
                while (safari_search_query[len]) len++;
                if (c == '\b') {
                    if (len > 0) safari_search_query[len - 1] = '\0';
                } else if (c == '\n') {
                    safari_search_active = true;
                    const char* prefix = "google.com/search?q=";
                    int p_idx = 0;
                    while (prefix[p_idx]) { safari_url[p_idx] = prefix[p_idx]; p_idx++; }
                    int q_idx = 0;
                    while (safari_search_query[q_idx]) { safari_url[p_idx + q_idx] = safari_search_query[q_idx]; q_idx++; }
                    safari_url[p_idx + q_idx] = '\0';
                } else if (c >= 32 && c <= 126 && len < 120) {
                    safari_search_query[len] = c;
                    safari_search_query[len + 1] = '\0';
                }
            }
            Rect dirty = {
                active_window->rect.x - 4, active_window->rect.y - 4,
                active_window->rect.w + 8, active_window->rect.h + 8
            };
            redraw_dirty_rect(dirty);
        } else if (active_window->title_is("Finder") && is_renaming_item && active_window->selected_item_idx != -1) {
            int sel = active_window->selected_item_idx;
            DiskItem& it = desktop_items[sel];
            int len = 0;
            while (it.name[len]) len++;
            
            if (c == '\b') {
                if (len > 0) {
                    it.name[len - 1] = '\0';
                }
            } else if (c == '\n') {
                is_renaming_item = false;
            } else if (c >= 32 && c <= 126 && len < 28) {
                it.name[len] = c;
                it.name[len + 1] = '\0';
            }
            Rect dirty = {
                active_window->rect.x - 4, active_window->rect.y - 4,
                active_window->rect.w + 8, active_window->rect.h + 8
            };
            redraw_dirty_rect(dirty);
        } else if (active_window->title_starts_with("Code Editor")) {
            if (c == '\b') {
                if (active_window->text_len > 0) {
                    active_window->text_len--;
                    active_window->text_input[active_window->text_len] = '\0';
                }
            } else if (c == '\n') {
                if (active_window->text_len < 1000) {
                    active_window->text_input[active_window->text_len++] = '\n';
                    active_window->text_input[active_window->text_len] = '\0';
                }
            } else if (c >= 32 && c <= 126 && active_window->text_len < 1000) {
                active_window->text_input[active_window->text_len++] = c;
                active_window->text_input[active_window->text_len] = '\0';
            }
            Rect dirty = {
                active_window->rect.x - 4, active_window->rect.y - 4,
                active_window->rect.w + 8, active_window->rect.h + 8
            };
            redraw_dirty_rect(dirty);
        }
    }
}

void WindowManager::set_ui_scale(float scale) {
    ui_scale = scale;
    
    // Scale layout parameters
    MENU_BAR_HEIGHT   = (int)(32 * scale);
    TITLE_BAR_HEIGHT  = (int)(30 * scale);
    DOCK_HEIGHT       = (int)(64 * scale);
    DOCK_ICON_SIZE    = (int)(48 * scale);
    DOCK_ICON_SPACING = (int)(36 * scale);
    DOCK_MARGIN_BOTTOM = (int)(24 * scale);
    DOCK_WIDTH        = (int)(640 * scale);
    
    // Scale all open window sizes and positions
    int width = (int)drivers::Framebuffer::get_width();
    int height = (int)drivers::Framebuffer::get_height();

    for (Window* w = window_list_head; w; w = w->next) {
        if (w->buffer) continue; // Skip user-space windows with static pixel buffers to prevent heap/page overflows
        w->rect.w = (int)(w->orig_rect.w * scale);
        w->rect.h = (int)(w->orig_rect.h * scale);
        w->rect.x = (int)(w->orig_rect.x * scale);
        w->rect.y = (int)(w->orig_rect.y * scale);
        // Clamp bounds to prevent sliding off-screen after scaling
        if (w->rect.x < 0) w->rect.x = 0;
        if (w->rect.x + w->rect.w > width) w->rect.x = width - w->rect.w;
        if (w->rect.y < MENU_BAR_HEIGHT) w->rect.y = MENU_BAR_HEIGHT;
        if (w->rect.y + w->rect.h > height) w->rect.y = height - w->rect.h;
    }
    force_redraw_all();
}

void WindowManager::set_theme(bool dark) {
    dark_mode = dark;
    if (dark) {
        C_WHITE      = 0x001E293B; // Slate 800
        C_BLACK      = 0x00FFFFFF; // White
        C_TEXT       = 0x00F1F5F9; // Slate 100
        C_TEXT_MUTED = 0x0094A3B8; // Slate 400
        C_MENU_BG    = 0x000F172A; // Slate 900
        C_MENU_LINE  = 0x00334155; // Slate 700
        C_DOCK_BG    = 0x001E293B; // Slate 800
        C_DOCK_RIM   = 0x00475569; // Slate 600
    } else {
        C_WHITE      = 0x00FFFFFF;
        C_BLACK      = 0x00000000;
        C_TEXT       = 0x00333333;
        C_TEXT_MUTED = 0x00666666;
        C_MENU_BG    = 0x00F5F5F5;
        C_MENU_LINE  = 0x00D2D2D2;
        C_DOCK_BG    = 0x00EBEBEB;
        C_DOCK_RIM   = 0x00FFFFFF;
    }

    // Dynamic background update for active mock windows
    for (Window* w = window_list_head; w; w = w->next) {
        if (!w->buffer) {
            if (w->title_is("Finder") || w->title_is("Safari") || w->title_is("Mail") || w->title_is("System Settings")) {
                w->bg_color = dark ? 0x001E293B : 0x00FFFFFF;
            } else if (w->title_is("Performance Monitor") || w->title_is("System Log")) {
                w->bg_color = dark ? 0x000F172A : 0x001B2E3C;
            }
        }
    }
}

void WindowManager::draw_settings_content(Window* win) {
    int x = win->rect.x;
    int y = win->rect.y;
    
    // Draw Section: UI Scaling
    draw_string("UI Scaling Factor", x + scale_dim(20), y + scale_dim(45), C_TEXT, 15.0f);
    
    float scales[] = {1.0f, 1.25f, 1.5f, 2.0f};
    const char* scale_labels[] = {"1.0x", "1.25x", "1.5x", "2.0x"};
    for (int i = 0; i < 4; ++i) {
        int bx = x + scale_dim(20 + i * 80);
        int by = y + scale_dim(70);
        int bw = scale_dim(70);
        int bh = scale_dim(26);
        bool is_active = (ui_scale == scales[i]);
        uint32_t bg = is_active ? 0x000F52BA : (dark_mode ? 0x00334155 : 0x00E2E8F0);
        uint32_t tc = is_active ? 0x00FFFFFF : C_TEXT;
        drivers::Framebuffer::draw_rounded_rect_alpha(bx, by, bw, bh, scale_dim(6), bg, 255);
        int tw = get_string_width(scale_labels[i], scale_font(13.0f));
        draw_string(scale_labels[i], bx + (bw - tw)/2, by + scale_dim(6), tc, 13.0f);
    }
    
    // Draw Section: System Theme
    draw_string("Appearance Mode", x + scale_dim(20), y + scale_dim(125), C_TEXT, 15.0f);
    
    const char* theme_labels[] = {"Light", "Dark"};
    for (int i = 0; i < 2; ++i) {
        int bx = x + scale_dim(20 + i * 130);
        int by = y + scale_dim(150);
        int bw = scale_dim(110);
        int bh = scale_dim(26);
        bool is_active = (dark_mode == (i == 1));
        uint32_t bg = is_active ? 0x000F52BA : (dark_mode ? 0x00334155 : 0x00E2E8F0);
        uint32_t tc = is_active ? 0x00FFFFFF : C_TEXT;
        drivers::Framebuffer::draw_rounded_rect_alpha(bx, by, bw, bh, scale_dim(6), bg, 255);
        int tw = get_string_width(theme_labels[i], scale_font(13.0f));
        draw_string(theme_labels[i], bx + (bw - tw)/2, by + scale_dim(6), tc, 13.0f);
    }
    
    // Draw Section: Desktop Wallpaper
    draw_string("Desktop Background", x + scale_dim(20), y + scale_dim(205), C_TEXT, 15.0f);
    
    const char* wp_labels[] = {"Aurora", "Sunset", "Forest", "Solid", "Abstract"};
    for (int i = 0; i < 5; ++i) {
        int bx = x + scale_dim(20 + i * 90);
        int by = y + scale_dim(230);
        int bw = scale_dim(80);
        int bh = scale_dim(26);
        bool is_active = (wallpaper_theme_id == i);
        uint32_t bg = is_active ? 0x000F52BA : (dark_mode ? 0x00334155 : 0x00E2E8F0);
        uint32_t tc = is_active ? 0x00FFFFFF : C_TEXT;
        drivers::Framebuffer::draw_rounded_rect_alpha(bx, by, bw, bh, scale_dim(6), bg, 255);
        int tw = get_string_width(wp_labels[i], scale_font(13.0f));
        draw_string(wp_labels[i], bx + (bw - tw)/2, by + scale_dim(6), tc, 13.0f);
    }

    // Draw Section: WiFi Network Connection
    draw_string("WiFi Networks (Scanning...)", x + scale_dim(20), y + scale_dim(285), C_TEXT, 15.0f);
    
    // Network 1: CrecentSecure_5G
    draw_string("CrecentSecure_5G (95% Secured)", x + scale_dim(30), y + scale_dim(315), C_TEXT, 14.0f);
    if (wifi_connected) {
        draw_string("Connected", x + scale_dim(280), y + scale_dim(315), 0x0034A853, 14.0f);
        
        int bx = x + scale_dim(380);
        int by = y + scale_dim(310);
        int bw = scale_dim(80);
        int bh = scale_dim(22);
        drivers::Framebuffer::draw_rounded_rect_alpha(bx, by, bw, bh, scale_dim(6), 0x00FF3B30, 255);
        int tw = get_string_width("Forget", scale_font(12.0f));
        draw_string("Forget", bx + (bw - tw)/2, by + scale_dim(4), C_WHITE, 12.0f);
    } else {
        int bx = x + scale_dim(380);
        int by = y + scale_dim(310);
        int bw = scale_dim(80);
        int bh = scale_dim(22);
        drivers::Framebuffer::draw_rounded_rect_alpha(bx, by, bw, bh, scale_dim(6), 0x000F52BA, 255);
        int tw = get_string_width("Connect", scale_font(12.0f));
        draw_string("Connect", bx + (bw - tw)/2, by + scale_dim(4), C_WHITE, 12.0f);
    }

    // Network 2: AmazingGuest (Open)
    draw_string("AmazingGuest (80% Open)", x + scale_dim(30), y + scale_dim(345), C_TEXT_MUTED, 14.0f);
    if (!wifi_connected) {
        int bx = x + scale_dim(380);
        int by = y + scale_dim(340);
        int bw = scale_dim(80);
        int bh = scale_dim(22);
        drivers::Framebuffer::draw_rounded_rect_alpha(bx, by, bw, bh, scale_dim(6), 0x000F52BA, 255);
        int tw = get_string_width("Connect", scale_font(12.0f));
        draw_string("Connect", bx + (bw - tw)/2, by + scale_dim(4), C_WHITE, 12.0f);
    }

    // Network 3: Direct-OS-Printer
    draw_string("Direct-OS-Printer (45% Secured)", x + scale_dim(30), y + scale_dim(375), C_TEXT_MUTED, 14.0f);
    if (!wifi_connected) {
        int bx = x + scale_dim(380);
        int by = y + scale_dim(370);
        int bw = scale_dim(80);
        int bh = scale_dim(22);
        drivers::Framebuffer::draw_rounded_rect_alpha(bx, by, bw, bh, scale_dim(6), 0x000F52BA, 255);
        int tw = get_string_width("Connect", scale_font(12.0f));
        draw_string("Connect", bx + (bw - tw)/2, by + scale_dim(4), C_WHITE, 12.0f);
    }
}

} // namespace wm