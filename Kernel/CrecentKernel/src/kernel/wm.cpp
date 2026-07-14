#include "wm.hpp"
#include "cursor_data.hpp"
#include "fs/vfs.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/heap.hpp"
#include "../drivers/framebuffer.hpp"
#include "../drivers/serial.hpp"
#include "../drivers/ttf.hpp"
#include "../drivers/ac97.hpp"

extern "C" void* memcpy(void* dest, const void* src, size_t n);

namespace wm {

static void get_vfs_path(const char* active_dir, const char* name, char* out_path);

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
static uint32_t C_BORDER       = 0x00D1D5DB;
static uint32_t C_TITLE_BG     = 0x00F3F4F6;
static uint32_t C_DIVIDER      = 0x00E5E7EB;
static constexpr uint32_t C_HOVER        = 0x000F52BA;
static constexpr uint32_t C_CLOSE        = 0x00FF5F56;
static constexpr uint32_t C_MINIMIZE     = 0x00FFBD2E;
static constexpr uint32_t C_MAXIMIZE     = 0x0027C93F;
static constexpr uint32_t C_TERMINAL     = 0x001A1A1A;
static constexpr uint32_t C_FINDER       = 0x00FFFFFF;
static constexpr uint32_t C_FOLDER       = 0x0000A2C9;
static constexpr uint32_t C_FILE         = 0x00E0E0E0;

static void draw_vector_icon(const char* name, int x, int y, int size, bool is_directory, bool is_terminal, bool selected);

static int MENU_BAR_HEIGHT   = 28;
static int TITLE_BAR_HEIGHT  = 30;
static int TASKBAR_HEIGHT    = 88; // DOCK_HEIGHT (64) + DOCK_MARGIN_BOTTOM (24)
static bool start_menu_active = false;
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
static char clipboard_path[256] = "";
static bool is_renaming_item = false;
static char rename_original_name[128] = "";

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
    this->finder_item_count = 0;
    this->finder_needs_reload = true;
    this->finder_list_view_mode = false;
    this->finder_editing_path = false;
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
    char path[64]; // Parent path (e.g. "Desktop", "Applications")
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
static int str_len(const char* s) {
    if (!s) return 0;
    int len = 0;
    while (s[len]) len++;
    return len;
}
static bool ends_with(const char* s, const char* suffix) {
    int s_len = str_len(s);
    int suf_len = str_len(suffix);
    if (s_len < suf_len) return false;
    return str_equal(s + s_len - suf_len, suffix);
}

static void str_copy(char* dest, const char* src, size_t max_len) {
    if (!dest || !src) return;
    size_t i = 0;
    for (; i < max_len - 1 && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}
static Rect union_rects(const Rect& a, const Rect& b) {
    int x1 = a.x < b.x ? a.x : b.x;
    int y1 = a.y < b.y ? a.y : b.y;
    int ax2 = a.x + a.w;
    int ay2 = a.y + a.h;
    int bx2 = b.x + b.w;
    int by2 = b.y + b.h;
    int x2 = ax2 > bx2 ? ax2 : bx2;
    int y2 = ay2 > by2 ? ay2 : by2;
    return {x1, y1, x2 - x1, y2 - y1};
}



static Rect expanded_rect(const Rect& r, int pad) {
    return {r.x - pad, r.y - pad, r.w + pad * 2, r.h + pad * 2};
}

void DirtyList::add(const Rect& r) {
    if (r.w <= 0 || r.h <= 0) return;
    
    Rect cur = r;
    bool merged = true;
    while (merged) {
        merged = false;
        for (int i = 0; i < count; ++i) {
            if (rects[i].intersects(cur)) {
                cur = union_rects(rects[i], cur);
                // Remove rects[i] by shifting remaining rects down
                for (int j = i; j < count - 1; ++j) {
                    rects[j] = rects[j + 1];
                }
                count--;
                merged = true;
                break;
            }
        }
    }
    
    if (count < 16) {
        rects[count++] = cur;
    } else {
        rects[0] = union_rects(rects[0], cur);
    }
}

Rect DirtyList::get_bounding_box() const {
    if (count == 0) return {0, 0, 0, 0};
    Rect bbox = rects[0];
    for (int i = 1; i < count; ++i) {
        bbox = union_rects(bbox, rects[i]);
    }
    return bbox;
}

static DirtyList pending_dirty;

static void enqueue_pending_dirty(const Rect& r) {
    pending_dirty.add(r);
}

static void add_item(const char* name, bool is_dir, bool is_term, const char* path, int x, int y) {
    if (desktop_item_count >= 64) return;
    DiskItem& it = desktop_items[desktop_item_count++];
    int i = 0;
    while (name[i] && i < 31) { it.name[i] = name[i]; i++; }
    it.name[i] = '\0';
    it.is_directory = is_dir;
    it.is_terminal = is_term;
    str_copy(it.path, path, 64);
    it.x = x;
    it.y = y;
    it.selected = false;

    // Create the file in VFS if it doesn't exist
    char vfs_path[256];
    if (str_equal(path, "Desktop")) {
        str_copy(vfs_path, "/Desktop/", 256);
    } else if (str_equal(path, "Documents")) {
        str_copy(vfs_path, "/Documents/", 256);
    } else if (str_equal(path, "Applications")) {
        str_copy(vfs_path, "/Applications/", 256);
    } else {
        str_copy(vfs_path, path, 256);
        size_t len = str_len(vfs_path);
        if (len > 0 && vfs_path[len - 1] != '/') {
            vfs_path[len] = '/';
            vfs_path[len + 1] = '\0';
        }
    }
    size_t len = str_len(vfs_path);
    str_copy(vfs_path + len, name, 256 - len);

    if (!fs::VFS::open(vfs_path)) {
        if (is_dir) {
            fs::VFSNode* n = fs::VFS::create_file(vfs_path, nullptr, 0);
            if (n) n->type = fs::NodeType::DIRECTORY;
        } else {
            char* buf = (char*)kernel::kmalloc(4096);
            if (buf) {
                buf[0] = '\0';
                if (str_equal(name, "hello.txt")) {
                    str_copy(buf, "TarFS validation hello world message!\n", 4096);
                } else if (str_equal(name, "info.txt")) {
                    str_copy(buf, "Hierarchical directories in TarFS are fully working.\n", 4096);
                } else if (str_equal(name, "System Log.txt")) {
                    str_copy(buf, "Crecent OS Kernel System Log initialized.", 4096);
                }
                fs::VFSNode* n = fs::VFS::create_file(vfs_path, buf, 4096);
                if (n) {
                    n->type = fs::NodeType::FILE;
                    n->size = str_len(buf);
                }
            }
        }
    }
}

void WindowManager::trigger_host_persist() {
    drivers::Serial::println("[PERSIST_BEGIN]");
    
    char f_buf[16];
    
    drivers::Serial::print("SCALE ");
    int scale_val = (int)(ui_scale * 100);
    int_to_str(scale_val, f_buf, 10);
    drivers::Serial::println(f_buf);
    
    drivers::Serial::print("THEME ");
    if (current_theme == 1) {
        drivers::Serial::println("DARK");
    } else if (current_theme == 2) {
        drivers::Serial::println("NORD");
    } else if (current_theme == 3) {
        drivers::Serial::println("WARM");
    } else {
        drivers::Serial::println("LIGHT");
    }
    
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
ContextMenu WindowManager::active_menu = {false, 0, 0, 0, 0, -1, -1};
SubMenu WindowManager::active_submenu = {false, 0, 0, 0, 0, -1, -1};
int WindowManager::perf_cpu_history[20] = {0};
int WindowManager::perf_current_cpu = 12;
int WindowManager::frame_counter = 0;
float WindowManager::ui_scale = 1.25f;
bool WindowManager::dark_mode = false;
int WindowManager::wallpaper_theme_id = 0;
int WindowManager::current_theme = 0;

bool WindowManager::audio_playing = false;
char WindowManager::audio_current_song[128] = "";
WindowManager::SongNote WindowManager::audio_notes[256] = {};
int WindowManager::audio_note_idx = 0;
int WindowManager::audio_note_count = 0;
uint32_t WindowManager::audio_note_end_frame = 0;
int WindowManager::audio_visualizer_seed = 0;

bool WindowManager::audio_is_wav = false;
uint8_t* WindowManager::audio_wav_data = nullptr;
uint32_t WindowManager::audio_wav_size = 0;
uint32_t WindowManager::audio_wav_offset = 0;
uint32_t WindowManager::audio_wav_sample_rate = 0;
uint16_t WindowManager::audio_wav_channels = 0;
uint16_t WindowManager::audio_wav_bits_per_sample = 0;
uint32_t WindowManager::audio_amplitude = 0;
uint32_t WindowManager::audio_wav_phase = 0;
uint8_t WindowManager::next_buffer_to_fill = 0;

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
    (void)alpha; // Solid circles for crisp rendering
    int x = win->rect.x;
    int y = win->rect.y;
    bool active = (win == active_window && !win->is_minimized);

    // Bounding box of the traffic lights cluster for hover detection
    int x_min = x + 10;
    int x_max = x + 62;
    int y_min = y + 9;
    int y_max = y + 21;
    bool hovered = (mouse_x >= x_min && mouse_x <= x_max && mouse_y >= y_min && mouse_y <= y_max);

    // Button centers (y-centered in 30px title bar)
    int cy = y + 15;
    int red_cx = x + 16;
    int yel_cx = x + 36;
    int grn_cx = x + 56;
    int r = 6; // 12px diameter

    // Colors
    uint32_t c_red = 0x00FF5F56;
    uint32_t c_yel = 0x00FFBD2E;
    uint32_t c_grn = 0x0027C93F;

    if (!active && !hovered) {
        // Desaturate inactive windows
        uint32_t inactive_c = dark_mode ? 0x004B5563 : 0x00D1D5DB;
        c_red = inactive_c;
        c_yel = inactive_c;
        c_grn = inactive_c;
    }

    // Draw solid circles
    drivers::Framebuffer::draw_circle_filled(red_cx, cy, r, c_red);
    drivers::Framebuffer::draw_circle_filled(yel_cx, cy, r, c_yel);
    drivers::Framebuffer::draw_circle_filled(grn_cx, cy, r, c_grn);

    // Draw micro-symbols on hover
    if (hovered) {
        // Red button: x
        drivers::Framebuffer::draw_line(red_cx - 2, cy - 2, red_cx + 2, cy + 2, 0x004C0000);
        drivers::Framebuffer::draw_line(red_cx - 2, cy + 2, red_cx + 2, cy - 2, 0x004C0000);

        // Yellow button: -
        drivers::Framebuffer::draw_line(yel_cx - 3, cy, yel_cx + 3, cy, 0x004C3C00);

        // Green button: +
        drivers::Framebuffer::draw_line(grn_cx - 2, cy, grn_cx + 2, cy, 0x00003C00);
        drivers::Framebuffer::draw_line(grn_cx, cy - 2, grn_cx, cy + 2, 0x00003C00);
    }
}

// Fix 10 & 11: Matched header signature. is_active is determined internally. Translucency restricted to dragging.
void WindowManager::draw_window_body(Window* win, uint8_t alpha, bool is_terminal) {
    (void)is_terminal;
    const Rect& r = win->rect;
    uint8_t body_alpha = alpha; 

    bool active = (win == active_window && !win->is_minimized);
    bool fast_mode = (win == active_window && (is_resizing_window || win->is_dragging));
    uint8_t decorator_alpha = fast_mode ? alpha : (uint8_t)(alpha * 180 / 255);

    // 1. Draw Bounding Rounded Window Frame (Outer Border)
    drivers::Framebuffer::draw_rounded_rect_alpha(r.x, r.y, r.w, r.h, CORNER_RADIUS, C_BORDER, decorator_alpha);
    
    // 2. Draw Title Bar (Rounded Top Corners, Flat Bottom)
    drivers::Framebuffer::draw_rounded_rect_alpha(r.x + 1, r.y + 1, r.w - 2, TITLE_BAR_HEIGHT + CORNER_RADIUS, CORNER_RADIUS, C_TITLE_BG, decorator_alpha);
    
    // 3. Draw Client Area (Rounded Bottom Corners, Flat Top)
    int client_y = r.y + TITLE_BAR_HEIGHT;
    int client_h = r.h - TITLE_BAR_HEIGHT - 1;
    drivers::Framebuffer::draw_rounded_rect_alpha(r.x + 1, client_y, r.w - 2, client_h, CORNER_RADIUS, win->bg_color, body_alpha);
    // Flatten top corners of the client area
    drivers::Framebuffer::draw_rect_alpha(r.x + 1, client_y, r.w - 2, CORNER_RADIUS, win->bg_color, body_alpha);

    // 4. Draw Header/Client Divider Line
    drivers::Framebuffer::draw_rect_alpha(r.x + 1, client_y - 1, r.w - 2, 1, C_DIVIDER, decorator_alpha);

    // 5. Centered Window Title Text
    int tw = get_string_width(win->title, scale_font(14.0f));
    int tx = r.x + (r.w - tw) / 2;
    draw_string(win->title, tx, r.y + 8, C_TEXT, 14.0f);

    // 6. Draw Traffic Light controls on top-left
    draw_traffic_lights(win, alpha);

    // 7. Resize grip affordance (bottom-right corner)
    if (!win->is_maximized && !win->is_minimized) {
        int rx = r.x + r.w - 14;
        int ry = r.y + r.h - 14;
        uint32_t grip_c = active ? (dark_mode ? 0x000A84FF : 0x000F52BA) : (dark_mode ? 0x004B5563 : 0x009CA3AF);
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

void WindowManager::rename_item(const char* parent_path, const char* old_name, const char* new_name) {
    for (int i = 0; i < desktop_item_count; i++) {
        if (str_equal(desktop_items[i].path, parent_path) && str_equal(desktop_items[i].name, old_name)) {
            str_copy(desktop_items[i].name, new_name, sizeof(desktop_items[i].name));
            break;
        }
    }

    char old_vfs_path[256];
    get_vfs_path(parent_path, old_name, old_vfs_path);
    const char* rel_old = old_vfs_path;
    if (old_vfs_path[0] == '/') rel_old = old_vfs_path + 1;

    char new_vfs_path[256];
    get_vfs_path(parent_path, new_name, new_vfs_path);
    const char* rel_new = new_vfs_path;
    if (new_vfs_path[0] == '/') rel_new = new_vfs_path + 1;

    for (size_t i = 0; i < fs::VFS::node_count; ++i) {
        if (str_equal(fs::VFS::child_nodes[i].name, rel_old)) {
            str_copy(fs::VFS::child_nodes[i].name, rel_new, sizeof(fs::VFS::child_nodes[i].name));
            break;
        }
    }
}

void WindowManager::delete_finder_item(const char* parent_path, const char* name, bool is_directory) {
    for (int i = 0; i < desktop_item_count; i++) {
        if (str_equal(desktop_items[i].path, parent_path) && str_equal(desktop_items[i].name, name)) {
            for (int j = i; j < desktop_item_count - 1; j++) {
                desktop_items[j] = desktop_items[j + 1];
            }
            desktop_item_count--;
            break;
        }
    }

    char vfs_path[256];
    get_vfs_path(parent_path, name, vfs_path);
    const char* rel_path = vfs_path;
    if (vfs_path[0] == '/') rel_path = vfs_path + 1;

    for (size_t i = 0; i < fs::VFS::node_count; ++i) {
        if (str_equal(fs::VFS::child_nodes[i].name, rel_path)) {
            if (fs::VFS::child_nodes[i].data) {
                kernel::kfree(fs::VFS::child_nodes[i].data);
            }
            for (size_t j = i; j < fs::VFS::node_count - 1; ++j) {
                fs::VFS::child_nodes[j] = fs::VFS::child_nodes[j + 1];
            }
            fs::VFS::node_count--;
            break;
        }
    }

    if (is_directory) {
        char prefix[256];
        str_copy(prefix, rel_path, 256);
        size_t len = str_len(prefix);
        prefix[len++] = '/';
        prefix[len] = '\0';
        for (size_t i = 0; i < fs::VFS::node_count; ) {
            if (title_starts_with_custom(fs::VFS::child_nodes[i].name, prefix)) {
                if (fs::VFS::child_nodes[i].data) {
                    kernel::kfree(fs::VFS::child_nodes[i].data);
                }
                for (size_t j = i; j < fs::VFS::node_count - 1; ++j) {
                    fs::VFS::child_nodes[j] = fs::VFS::child_nodes[j + 1];
                }
                fs::VFS::node_count--;
            } else {
                i++;
            }
        }

        char d_prefix[256];
        str_copy(d_prefix, parent_path, 256);
        size_t d_len = str_len(d_prefix);
        if (d_len > 0 && d_prefix[d_len - 1] != '/') {
            d_prefix[d_len++] = '/';
        }
        str_copy(d_prefix + d_len, name, 256 - d_len);
        for (int i = 0; i < desktop_item_count; ) {
            if (title_starts_with_custom(desktop_items[i].path, d_prefix)) {
                for (int j = i; j < desktop_item_count - 1; j++) {
                    desktop_items[j] = desktop_items[j + 1];
                }
                desktop_item_count--;
            } else {
                i++;
            }
        }
    }
}

void WindowManager::populate_finder_cache(Window* win) {
    if (!win) return;
    
    win->finder_item_count = 0;
    win->finder_needs_reload = false;

    const char* active_dir = win->text_input;
    if (win->text_len == 0) {
        active_dir = "Desktop";
    }

    if (active_dir[0] == '/') {
        // Real VFS directory
        fs::VFSNode* dir_node = fs::VFS::open(active_dir);
        if (dir_node && dir_node->type == fs::NodeType::DIRECTORY && dir_node->readdir) {
            fs::VFSNode entry;
            size_t idx = 0;
            while (idx < Window::FINDER_MAX_ITEMS) {
                int res = dir_node->readdir(dir_node, idx, &entry);
                if (res <= 0) break;
                
                win->finder_items[win->finder_item_count++] = entry;
                idx++;
            }
        }
    } else {
        // Virtual folder (Desktop, Applications, Documents, USB_DRIVE)
        for (int i = 0; i < desktop_item_count; i++) {
            DiskItem& it = desktop_items[i];
            if (str_equal(it.path, active_dir)) {
                if (win->finder_item_count >= Window::FINDER_MAX_ITEMS) break;
                
                fs::VFSNode& node = win->finder_items[win->finder_item_count++];
                str_copy(node.name, it.name, sizeof(node.name));
                node.type = it.is_directory ? fs::NodeType::DIRECTORY : fs::NodeType::FILE;
                node.size = 0;
                node.capacity = 0;
                node.data = nullptr;
                node.read = nullptr;
                node.write = nullptr;
                node.finddir = nullptr;
                node.readdir = nullptr;
            }
        }
    }
}

void WindowManager::draw_finder_content(Window* win) {
    if (win->finder_needs_reload) {
        populate_finder_cache(win);
    }

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
    if (win->finder_editing_path) {
        int box_x = x + 220;
        int box_w = w - 490;
        uint32_t box_border = 0x000F52BA;
        drivers::Framebuffer::draw_rounded_rect_alpha(box_x, toolbar_y + 5, box_w, 24, 6, box_border, 255);
        drivers::Framebuffer::draw_rounded_rect_alpha(box_x + 1, toolbar_y + 6, box_w - 2, 22, 5, dark_mode ? 0x000F172A : C_WHITE, 255);
        
        char path_edit_str[256];
        int pe_len = 0;
        while (win->text_input[pe_len] && pe_len < 250) {
            path_edit_str[pe_len] = win->text_input[pe_len];
            pe_len++;
        }
        if ((frame_counter / 20) % 2 == 0) {
            path_edit_str[pe_len++] = '|';
        }
        path_edit_str[pe_len] = '\0';
        draw_string(path_edit_str, box_x + 8, toolbar_y + 10, C_TEXT, 13.0f);
    } else {
        draw_string("Path: /", x + 225, toolbar_y + 10, C_TEXT_MUTED, 13.0f);
        draw_string(active_dir, x + 270, toolbar_y + 10, C_TEXT, 13.0f);
    }

    // View Toggle button (Grid / List)
    uint32_t toggle_color = win->finder_list_view_mode ? 0x00D0D0D0 : 0x00E0E0E0;
    drivers::Framebuffer::draw_rounded_rect_alpha(x + w - 260, toolbar_y + 5, 50, 24, 6, toggle_color, 255);
    draw_string(win->finder_list_view_mode ? "Grid" : "List", x + w - 248, toolbar_y + 10, C_TEXT, 13.0f);

    // + Folder Button
    drivers::Framebuffer::draw_rounded_rect_alpha(x + w - 190, toolbar_y + 5, 80, 24, 6, 0x00E0E0E0, 255);
    draw_string("+ Folder", x + w - 178, toolbar_y + 10, C_TEXT, 13.0f);

    // + File Button
    drivers::Framebuffer::draw_rounded_rect_alpha(x + w - 100, toolbar_y + 5, 80, 24, 6, 0x00E0E0E0, 255);
    draw_string("+ File", x + w - 84, toolbar_y + 10, C_TEXT, 13.0f);

    // Sidebar items
    draw_string("Favorites", x + 15, y + TITLE_BAR_HEIGHT + 15, C_TEXT_MUTED, 12.0f);
    
    const char* favorites[] = {"Desktop", "Applications", "Documents", "/", "/tar", "/disk"};
    const char* fav_labels[] = {"Desktop", "Applications", "Documents", "Root (/)    ", "ROM (/tar)  ", "Disk (/disk)"};
    for (int i = 0; i < 6; i++) {
        int iy = y + TITLE_BAR_HEIGHT + 40 + i * 30;
        bool is_selected = str_equal(active_dir, favorites[i]);
        if (is_selected) {
            drivers::Framebuffer::draw_rounded_rect_alpha(x + 8, iy - 4, 124, 24, 6, C_HOVER, 255);
        }
        uint32_t tc = is_selected ? C_WHITE : C_TEXT;
        draw_string(fav_labels[i], x + 18, iy, tc, 14.0f);
    }
    
    if (!win->finder_list_view_mode) {
        // Draw files in the active directory (Grid view)
        int grid_col = 0;
        int grid_row = 0;
        int columns = (w - 180) / 90;
        if (columns < 1) columns = 1;

        for (int i = 0; i < win->finder_item_count; i++) {
            fs::VFSNode& it = win->finder_items[i];
            int ix = x + 170 + grid_col * 90;
            int iy = y + TITLE_BAR_HEIGHT + 55 + grid_row * 90;
            
            if (iy + 80 > y + h - 12) break;

            bool is_item_selected = (win->selected_item_idx == i);
            if (is_item_selected) {
                drivers::Framebuffer::draw_rounded_rect_alpha(ix, iy - 5, 64, 80, 8, 0x0080B0F0, 100);
            }

            bool is_term = str_equal(it.name, "Terminal") || str_equal(it.name, "Terminal.app");
            draw_vector_icon(it.name, ix + 12, iy, 40, it.type == fs::NodeType::DIRECTORY, is_term, is_item_selected);
            
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
                if (label_w > 80) {
                    char truncated[16];
                    int char_count = 0;
                    while (it.name[char_count] && char_count < 8) {
                        truncated[char_count] = it.name[char_count];
                        char_count++;
                    }
                    truncated[char_count++] = '.';
                    truncated[char_count++] = '.';
                    truncated[char_count++] = '.';
                    truncated[char_count] = '\0';
                    label_w = get_string_width(truncated, 13.0f);
                    label_x = ix + (64 - label_w) / 2;
                    draw_string(truncated, label_x, iy + 45, C_TEXT, 13.0f);
                } else {
                    draw_string(it.name, label_x, iy + 45, C_TEXT, 13.0f);
                }
            }
            
            grid_col++;
            if (grid_col >= columns) {
                grid_col = 0;
                grid_row++;
            }
        }
    } else {
        // Draw List Header
        int list_y = y + TITLE_BAR_HEIGHT + 45;
        uint32_t header_bg = dark_mode ? 0x002B374A : 0x00EAEAEA;
        drivers::Framebuffer::draw_rect_alpha(x + 142, list_y, w - 143, 22, header_bg, 255);
        drivers::Framebuffer::draw_rect_alpha(x + 142, list_y + 21, w - 143, 1, border_c, 255);

        // Header Column Labels
        draw_string("Name", x + 180, list_y + 4, C_TEXT_MUTED, 12.0f);
        draw_string("Type", x + 340, list_y + 4, C_TEXT_MUTED, 12.0f);
        draw_string("Size", x + w - 80, list_y + 4, C_TEXT_MUTED, 12.0f);

        int row_h = 24;
        for (int i = 0; i < win->finder_item_count; i++) {
            fs::VFSNode& it = win->finder_items[i];
            int iy = list_y + 22 + i * row_h;
            
            if (iy + row_h > y + h - 12) break;

            bool is_item_selected = (win->selected_item_idx == i);
            if (is_item_selected) {
                drivers::Framebuffer::draw_rect_alpha(x + 142, iy, w - 143, row_h, C_HOVER, 255);
            } else if (i % 2 == 1) {
                uint32_t zebra_bg = dark_mode ? 0x00222F3E : 0x00FAFAFA;
                drivers::Framebuffer::draw_rect_alpha(x + 142, iy, w - 143, row_h, zebra_bg, 255);
            }

            bool is_term = str_equal(it.name, "Terminal") || str_equal(it.name, "Terminal.app");
            draw_vector_icon(it.name, x + 155, iy + 4, 16, it.type == fs::NodeType::DIRECTORY, is_term, is_item_selected);

            uint32_t tc = is_item_selected ? C_WHITE : C_TEXT;
            draw_string(it.name, x + 180, iy + 5, tc, 13.0f);

            const char* type_str = "File";
            if (it.type == fs::NodeType::DIRECTORY) {
                type_str = "Folder";
            } else if (ends_with(it.name, ".bmp")) {
                type_str = "BMP Image";
            } else if (ends_with(it.name, ".sng")) {
                type_str = "SNG Sheet";
            } else if (ends_with(it.name, ".wav")) {
                type_str = "WAV Audio";
            } else if (is_term) {
                type_str = "App";
            }
            draw_string(type_str, x + 340, iy + 5, is_item_selected ? C_WHITE : C_TEXT_MUTED, 12.0f);

            if (it.type == fs::NodeType::DIRECTORY) {
                draw_string("--", x + w - 80, iy + 5, is_item_selected ? C_WHITE : C_TEXT_MUTED, 12.0f);
            } else {
                char size_buf[32];
                if (it.size >= 1024 * 1024) {
                    int_to_str(it.size / (1024 * 1024), size_buf, 32);
                    int len = 0;
                    while (size_buf[len]) len++;
                    size_buf[len++] = ' '; size_buf[len++] = 'M'; size_buf[len++] = 'B';
                    size_buf[len] = '\0';
                } else if (it.size >= 1024) {
                    int_to_str(it.size / 1024, size_buf, 32);
                    int len = 0;
                    while (size_buf[len]) len++;
                    size_buf[len++] = ' '; size_buf[len++] = 'K'; size_buf[len++] = 'B';
                    size_buf[len] = '\0';
                } else {
                    int_to_str(it.size, size_buf, 32);
                    int len = 0;
                    while (size_buf[len]) len++;
                    size_buf[len++] = ' '; size_buf[len++] = 'B';
                    size_buf[len] = '\0';
                }
                draw_string(size_buf, x + w - 80, iy + 5, is_item_selected ? C_WHITE : C_TEXT_MUTED, 12.0f);
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

    if (this->is_dragging && this->buffer) {
        drivers::Framebuffer::blit_buffer(rect.x, rect.y, rect.w, rect.h, this->buffer);
        return;
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
            } else if (this->title_starts_with("Audio Player")) {
                WindowManager::draw_audio_player_content(this);
            } else if (this->title_starts_with("Picture Viewer")) {
                WindowManager::draw_picture_viewer_content(this);
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
                            int theme_id = 0;
                            if (str_equal(line + 6, "DARK")) {
                                theme_id = 1;
                            } else if (str_equal(line + 6, "NORD")) {
                                theme_id = 2;
                            } else if (str_equal(line + 6, "WARM")) {
                                theme_id = 3;
                            }
                            set_theme(theme_id);
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

    if (curr->title_starts_with("Audio Player")) {
        audio_stop();
        if (audio_wav_data) {
            kernel::kfree(audio_wav_data);
            audio_wav_data = nullptr;
        }
        audio_is_wav = false;
    }

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
    int target_x = width / 2;
    int target_y = height - TASKBAR_HEIGHT;

    Rect orig = win->rect;
    for (int frame = 0; frame <= 5; ++frame) {
        float t = (float)frame / 5.0f;
        int w = orig.w - (int)(orig.w * t * 0.85f);
        int h = orig.h - (int)(orig.h * t * 0.85f);
        int x = orig.x + (int)((target_x - (w / 2) - orig.x) * t);
        int y = orig.y + (int)((target_y - orig.y) * t);
        
        win->rect = {x, y, w, h};
        force_redraw_all();
        for (volatile int d = 0; d < 8000000; ) {
            d = d + 1;
            if (d % 2000000 == 0) {
                kernel::schedule();
            }
        }
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
    if (!drivers::Framebuffer::is_initialized()) return;

    const uint32_t* cursor_data = (ui_scale >= 1.5f) ? cursor_2x_data : cursor_1x_data;
    int cursor_size = (ui_scale >= 1.5f) ? cursor_2x_size : cursor_1x_size;
    int offset = (int)(ui_scale >= 1.5f ? 3 : 1);

    int start_x = mouse_x - offset;
    int start_y = mouse_y - offset;
    Rect cursor_rect = {start_x, start_y, cursor_size, cursor_size};
    Rect clip = drivers::Framebuffer::get_clip_rect();
    if (clip.w <= 0 || clip.h <= 0 || !clip.intersects(cursor_rect)) return;

    int screen_w = (int)drivers::Framebuffer::get_width();
    int screen_h = (int)drivers::Framebuffer::get_height();
    uint32_t pitch_words = drivers::Framebuffer::get_pitch_words();
    uint32_t* back = drivers::Framebuffer::get_back_buffer();
    if (!back) return;

    int copy_start_y = start_y;
    int copy_end_y = start_y + cursor_size;
    if (copy_start_y < 0) copy_start_y = 0;
    if (copy_start_y < clip.y) copy_start_y = clip.y;
    if (copy_end_y > screen_h) copy_end_y = screen_h;
    if (copy_end_y > clip.y + clip.h) copy_end_y = clip.y + clip.h;

    int copy_start_x = start_x;
    int copy_end_x = start_x + cursor_size;
    if (copy_start_x < 0) copy_start_x = 0;
    if (copy_start_x < clip.x) copy_start_x = clip.x;
    if (copy_end_x > screen_w) copy_end_x = screen_w;
    if (copy_end_x > clip.x + clip.w) copy_end_x = clip.x + clip.w;

    if (copy_start_x >= copy_end_x || copy_start_y >= copy_end_y) return;

    for (int sy = copy_start_y; sy < copy_end_y; sy++) {
        int cy = sy - start_y;
        uint32_t* back_row = back + (uint64_t)sy * pitch_words;

        for (int sx = copy_start_x; sx < copy_end_x; sx++) {
            int cx = sx - start_x;
            uint32_t px = cursor_data[cy * cursor_size + cx];
            uint8_t a = (px >> 24) & 0xFF;
            if (a == 0) continue;

            if (a == 255) {
                back_row[sx] = px & 0x00FFFFFF;
            } else {
                uint32_t bg = back_row[sx];
                uint32_t b_r = (bg >> 16) & 0xFF;
                uint32_t b_g = (bg >> 8) & 0xFF;
                uint32_t b_b = bg & 0xFF;

                uint32_t r = (px >> 16) & 0xFF;
                uint32_t g = (px >> 8) & 0xFF;
                uint32_t b = px & 0xFF;

                uint32_t final_r = (r * a + b_r * (255 - a)) >> 8;
                uint32_t final_g = (g * a + b_g * (255 - a)) >> 8;
                uint32_t final_b = (b * a + b_b * (255 - a)) >> 8;

                back_row[sx] = (final_r << 16) | (final_g << 8) | final_b;
            }
        }
    }
}

void WindowManager::erase_cursor() {
}

void WindowManager::blit_cursor() {
    // Deprecated stub kept for header ABI compatibility
}

void WindowManager::update_hardware_cursor_fast() {
    if (!drivers::Framebuffer::is_initialized()) return;

    uint32_t* vram = drivers::Framebuffer::get_active_vram_buffer();
    uint32_t* back = drivers::Framebuffer::get_back_buffer();
    if (!vram || !back) return;

    int screen_w = (int)drivers::Framebuffer::get_width();
    int screen_h = (int)drivers::Framebuffer::get_height();
    uint32_t pitch_words = drivers::Framebuffer::get_pitch_words();

    const uint32_t* cursor_data = (ui_scale >= 1.5f) ? cursor_2x_data : cursor_1x_data;
    int cursor_size = (ui_scale >= 1.5f) ? cursor_2x_size : cursor_1x_size;
    int offset = (int)(ui_scale >= 1.5f ? 3 : 1);

    // 1. Erase old cursor by copying from clean back_buffer to active VRAM
    int old_x = last_mouse_x - offset;
    int old_y = last_mouse_y - offset;
    int copy_start_y = old_y;
    int copy_end_y = old_y + cursor_size;
    if (copy_start_y < 0) copy_start_y = 0;
    if (copy_end_y > screen_h) copy_end_y = screen_h;

    int copy_start_x = old_x;
    int copy_end_x = old_x + cursor_size;
    if (copy_start_x < 0) copy_start_x = 0;
    if (copy_end_x > screen_w) copy_end_x = screen_w;

    if (copy_start_x < copy_end_x && copy_start_y < copy_end_y) {
        uint64_t bytes_per_line = (uint64_t)(copy_end_x - copy_start_x) * sizeof(uint32_t);
        for (int cy = copy_start_y; cy < copy_end_y; ++cy) {
            uint64_t line_offset = (uint64_t)cy * pitch_words;
            memcpy(&vram[line_offset + copy_start_x], &back[line_offset + copy_start_x], bytes_per_line);
        }
    }

    // 2. Draw new cursor directly to active VRAM, reading background from back_buffer
    int new_x = mouse_x - offset;
    int new_y = mouse_y - offset;
    int draw_start_y = new_y;
    int draw_end_y = new_y + cursor_size;
    if (draw_start_y < 0) draw_start_y = 0;
    if (draw_end_y > screen_h) draw_end_y = screen_h;

    int draw_start_x = new_x;
    int draw_end_x = new_x + cursor_size;
    if (draw_start_x < 0) draw_start_x = 0;
    if (draw_end_x > screen_w) draw_end_x = screen_w;

    if (draw_start_x < draw_end_x && draw_start_y < draw_end_y) {
        for (int sy = draw_start_y; sy < draw_end_y; ++sy) {
            int cy = sy - new_y;
            uint64_t line_offset = (uint64_t)sy * pitch_words;
            uint32_t* vram_row = vram + line_offset;
            const uint32_t* back_row = back + line_offset;

            for (int sx = draw_start_x; sx < draw_end_x; ++sx) {
                int cx = sx - new_x;
                uint32_t color = cursor_data[cy * cursor_size + cx];
                uint8_t alpha = (color >> 24) & 0xFF;
                if (alpha == 0) continue;

                if (alpha == 255) {
                    vram_row[sx] = color & 0x00FFFFFF;
                } else {
                    // Read background from cached back_buffer (standard RAM) instead of VRAM to avoid PCI bus stalls!
                    uint32_t bg = back_row[sx];
                    uint32_t b_r = (bg >> 16) & 0xFF;
                    uint32_t b_g = (bg >> 8) & 0xFF;
                    uint32_t b_b = bg & 0xFF;

                    uint32_t r = (color >> 16) & 0xFF;
                    uint32_t g = (color >> 8) & 0xFF;
                    uint32_t b = color & 0xFF;

                    uint32_t final_r = (r * alpha + b_r * (255 - alpha)) >> 8;
                    uint32_t final_g = (g * alpha + b_g * (255 - alpha)) >> 8;
                    uint32_t final_b = (b * alpha + b_b * (255 - alpha)) >> 8;

                    vram_row[sx] = (final_r << 16) | (final_g << 8) | final_b;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Desktop / wallpaper
// ---------------------------------------------------------------------------
static void draw_vector_icon(const char* name, int x, int y, int size, bool is_directory, bool is_terminal, bool selected) {
    if (is_directory) {
        if (str_equal(name, "USB_DRIVE")) {
            uint32_t body_c = 0x00ECEFF1; // Silver metal
            uint32_t tip_c = 0x0078909C; // Darker metal connector
            uint32_t active_c = selected ? 0x000A84FF : 0x00B0BEC5; // Selection color or default gray outline
            
            // Silver main body
            drivers::Framebuffer::draw_rounded_rect_alpha(x + size/4, y + size/3, size/2, size/2, size/12, body_c, 255);
            drivers::Framebuffer::draw_rounded_rect_alpha(x + size/4, y + size/3, size/2, size/2, size/12, active_c, 100);
            
            // Metal neck connector tip
            drivers::Framebuffer::draw_rect_alpha(x + size/3 + size/24, y + size/6, size/4, size/6, tip_c, 255);
            // Highlight connector edge
            drivers::Framebuffer::draw_rect_alpha(x + size/3 + size/24, y + size/6, size/4, size/24, 0x00CFD8DC, 255);
            
            // Green activity LED dot
            drivers::Framebuffer::draw_circle_filled(x + size/2, y + 2*size/3, 2, 0x0030D158);
        } else {
            uint32_t back_c   = selected ? 0x000F52BA : 0x000078D4;
            uint32_t tab_c    = selected ? 0x000E4EAA : 0x000063B1;
            uint32_t front_c  = selected ? 0x004CA0FF : 0x003A96DD;
            uint32_t border_c = selected ? 0x0090C8FF : 0x0078BAEC;
            
            // 1. Back flap
            drivers::Framebuffer::draw_rounded_rect_alpha(x, y + size/5, size, size - size/5, size/8, back_c, 255);
            // 2. Top tab
            drivers::Framebuffer::draw_rounded_rect_alpha(x + size/12, y + size/15, size/2, size/4, size/10, tab_c, 255);
            // Fill tab seam
            drivers::Framebuffer::draw_rect_alpha(x + size/12, y + size/5, size/2, size/12, back_c, 255);
            // 3. Front pocket flap
            drivers::Framebuffer::draw_rounded_rect_alpha(x, y + size/3, size, size - size/3, size/8, front_c, 255);
            // 4. Highlight front pocket upper border line
            drivers::Framebuffer::draw_line(x + 1, y + size/3 + 1, x + size - 2, y + size/3 + 1, border_c);
        }
    } else if (is_terminal) {
        uint32_t bg_c = 0x00171717;
        uint32_t bezel_c = selected ? 0x000A84FF : 0x00404040;
        
        // Console chassis screen
        drivers::Framebuffer::draw_rounded_rect_alpha(x, y, size, size, size/8, bg_c, 255);
        // Bezel outline
        drivers::Framebuffer::draw_rounded_rect_alpha(x, y, size, size, size/8, bezel_c, 120);
        
        // Prompt >
        int px = x + size/5;
        int py = y + size/3;
        int p_sz = size/6;
        drivers::Framebuffer::draw_line(px, py, px + p_sz, py + p_sz, 0x0030D158);
        drivers::Framebuffer::draw_line(px + p_sz, py + p_sz, px, py + 2*p_sz, 0x0030D158);
        
        // Cursor block _
        drivers::Framebuffer::draw_rect_alpha(px + p_sz + size/12, py + p_sz + size/10, size/5, size/10, 0x0030D158, 255);
    } else {
        uint32_t page_c = 0x00F8F9FA;
        uint32_t border_c = selected ? 0x000A84FF : 0x00CFD8DC;
        uint32_t line_c = 0x00ECEFF1;
        
        // Page base shape
        drivers::Framebuffer::draw_rounded_rect_alpha(x, y, size, size, size/10, page_c, 255);
        drivers::Framebuffer::draw_rounded_rect_alpha(x, y, size, size, size/10, border_c, 100);
        
        // Corner dog-ear fold
        int fold_sz = size/3;
        int fx = x + size - fold_sz;
        int fy = y;
        
        // Fold background
        drivers::Framebuffer::draw_rect_alpha(fx, fy, fold_sz, fold_sz, 0x00CFD8DC, 255);
        // Fold line boundary
        drivers::Framebuffer::draw_line(fx, fy, fx + fold_sz - 1, fy + fold_sz - 1, border_c);
        
        // Horizontal text lines
        int start_y = y + size/3 + size/24;
        int line_w = size - 2 * (size/6);
        int gap_y = size/8;
        for (int i = 0; i < 3; ++i) {
            drivers::Framebuffer::draw_rect_alpha(x + size/6, start_y + i * gap_y, line_w, size/16, line_c, 255);
        }
    }
}

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

            draw_vector_icon(it.name, it.x + pad_ic, it.y, size_ic, it.is_directory, it.is_terminal, it.selected);
            
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

void WindowManager::draw_desktop() {
    if (pending_dirty.count == 0) return;
    DirtyList copy = pending_dirty;
    pending_dirty.clear();
    redraw_dirty_list(copy);
}

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
    draw_all_windows();
    draw_mac_decorations();
    draw_cursor(); // Draw cursor to back buffer (RAM)
    drivers::Framebuffer::swap_buffers();
}

void WindowManager::invalidate_window(int id) {
    Window* win = get_window_by_id(id);
    if (!win || win->is_minimized) return;
    enqueue_pending_dirty(expanded_rect(win->rect, 32));
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------
void WindowManager::draw_menu_bar() {
    int w = (int)drivers::Framebuffer::get_width();
    
    Rect menu_rect = {0, 0, w, MENU_BAR_HEIGHT};
    if (!drivers::Framebuffer::get_clip_rect().intersects(menu_rect)) {
        return;
    }
    
    // 1. Translucent background and border
    drivers::Framebuffer::draw_rect_alpha(0, 0, w, MENU_BAR_HEIGHT, C_MENU_BG, 200);
    drivers::Framebuffer::draw_rect_alpha(0, MENU_BAR_HEIGHT - 1, w, 1, C_MENU_LINE, 200);

    // 2. Procedural Vector Apple Logo
    int ax = 16, ay = 14;
    drivers::Framebuffer::draw_circle_filled(ax, ay, 5, C_TEXT); // Left lobe
    drivers::Framebuffer::draw_circle_filled(ax + 5, ay, 5, C_TEXT); // Right lobe
    drivers::Framebuffer::draw_circle_filled(ax + 2, ay - 6, 2, C_TEXT); // Leaf
    drivers::Framebuffer::draw_circle_filled(ax + 8, ay - 1, 2, C_MENU_BG); // Bite overlay (alpha 255)

    // 3. Menu items
    draw_string("Finder    File    Edit    View    Go    Window    Help", 42, 7, C_TEXT, 14.0f);
    
    // 4. WiFi Icon
    int wx = w - 210;
    int wy = 14;
    uint32_t wifi_c = wifi_connected ? (dark_mode ? 0x000A84FF : 0x000F52BA) : 0x008E8E93;
    
    // Render Concentric WiFi Arcs
    drivers::Framebuffer::draw_circle_filled(wx, wy, 9, wifi_c);
    drivers::Framebuffer::draw_circle_filled(wx, wy, 7, C_MENU_BG);
    drivers::Framebuffer::draw_circle_filled(wx, wy, 5, wifi_c);
    drivers::Framebuffer::draw_circle_filled(wx, wy, 3, C_MENU_BG);
    drivers::Framebuffer::draw_circle_filled(wx, wy, 1, wifi_c);
    // Cover bottom half
    drivers::Framebuffer::draw_rect_alpha(wx - 10, wy + 1, 20, 10, C_MENU_BG, 255);

    // 5. Battery Icon
    int bx = w - 170;
    int by = 8;
    uint32_t bat_rim = C_TEXT;
    uint32_t bat_level = usb_connected ? 0x0030D158 : (dark_mode ? 0x00FFD60A : 0x00FF9F0A); // Green if charging, orange otherwise
    
    drivers::Framebuffer::draw_rounded_rect_alpha(bx, by, 20, 11, 3, bat_rim, 255);
    drivers::Framebuffer::draw_rect_alpha(bx + 1, by + 1, 18, 9, C_MENU_BG, 255);
    drivers::Framebuffer::draw_rect_alpha(bx + 20, by + 3, 2, 5, bat_rim, 255); // battery nub
    drivers::Framebuffer::draw_rect_alpha(bx + 3, by + 3, 12, 5, bat_level, 255); // 70% level

    // 6. Time String
    draw_string("Wed 14:35", w - 120, 7, C_TEXT, 14.0f);
}



void WindowManager::focus_or_create_app(const char* title, uint32_t bg_color, int w, int h) {
    for (Window* curr = window_list_head; curr; curr = curr->next) {
        if (curr->title_is(title)) {
            curr->is_minimized = false;
            focus_window(curr);
            force_redraw_all();
            return;
        }
    }
    int width = (int)drivers::Framebuffer::get_width();
    int height = (int)drivers::Framebuffer::get_height();
    int wx = (width - w) / 2;
    int wy = MENU_BAR_HEIGHT + (height - MENU_BAR_HEIGHT - TASKBAR_HEIGHT - h) / 2;
    if (wy < MENU_BAR_HEIGHT) wy = MENU_BAR_HEIGHT + 10;
    create_window(wx, wy, w, h, title, bg_color);
    force_redraw_all();
}

void WindowManager::draw_dock() {
    int screen_w = (int)drivers::Framebuffer::get_width();
    int screen_h = (int)drivers::Framebuffer::get_height();

    int dx = (screen_w - DOCK_WIDTH) / 2;
    int dy = screen_h - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM;

    Rect dock_rect = {dx, dy, DOCK_WIDTH, DOCK_HEIGHT};
    if (!drivers::Framebuffer::get_clip_rect().intersects(dock_rect)) {
        return;
    }

    // 1. Draw Translucent Dock Container
    drivers::Framebuffer::draw_rounded_rect_alpha(dx, dy, DOCK_WIDTH, DOCK_HEIGHT, 16, C_DOCK_BG, 190);
    drivers::Framebuffer::draw_rounded_rect_alpha(dx, dy, DOCK_WIDTH, DOCK_HEIGHT, 16, C_DOCK_RIM, 90);

    // 2. Centered App Icons
    const char* app_names[] = {"Finder", "Safari", "Terminal", "Notes", "System Settings"};
    int gap = (int)(16 * ui_scale);
    int icon_size = DOCK_ICON_SIZE;
    int total_icons_w = 5 * icon_size + 4 * gap;
    int start_x = dx + (DOCK_WIDTH - total_icons_w) / 2;

    int hovered_idx = -1;
    int hover_ix = 0, hover_iy = 0, hover_iw = 0;

    for (int i = 0; i < 5; ++i) {
        int ix = start_x + i * (icon_size + gap);
        int iy = dy + (DOCK_HEIGHT - icon_size) / 2;
        int iw = icon_size;
        int ih = icon_size;

        // Check hover magnification state
        bool is_hovered = (mouse_x >= ix && mouse_x < ix + icon_size && mouse_y >= dy && mouse_y < dy + DOCK_HEIGHT);
        if (is_hovered) {
            hovered_idx = i;
            iw = (int)(icon_size * 1.15f);
            ih = (int)(icon_size * 1.15f);
            iy = dy + (DOCK_HEIGHT - ih) / 2 - (int)(6 * ui_scale);
            ix = ix - (iw - icon_size) / 2;
            
            // Save hovered coords to draw tooltip on top after drawing all icons
            hover_ix = ix;
            hover_iy = iy;
            hover_iw = iw;
        }

        // Draw Icons
        if (i == 0) {
            // Finder: split light blue / dark blue face
            drivers::Framebuffer::draw_rounded_rect_alpha(ix, iy, iw, ih, 10, 0x005AC8FA, 255);
            drivers::Framebuffer::draw_rounded_rect_alpha(ix + iw/2, iy, iw/2, ih, 10, 0x00007AFF, 255);
            drivers::Framebuffer::draw_rect_alpha(ix + iw/2, iy, 4, ih, 0x00007AFF, 255); // Overwrite middle rounding gap
            
            // Nose, eyes, and smile
            int cx = ix + iw/2;
            int cy = iy + ih/2;
            drivers::Framebuffer::draw_line(cx, cy - ih/6, cx, cy + ih/6, 0x001C1C1E); // Nose line
            drivers::Framebuffer::draw_line(cx, cy + ih/6, cx - iw/8, cy + ih/6 + iw/12, 0x001C1C1E);
            drivers::Framebuffer::draw_line(cx - iw/8, cy + ih/6 + iw/12, cx, cy + ih/6 + iw/12, 0x001C1C1E);
            
            drivers::Framebuffer::draw_circle_filled(ix + iw/3, iy + ih/3 + 2, 3, 0x001C1C1E); // Left eye
            drivers::Framebuffer::draw_circle_filled(ix + 2*iw/3, iy + ih/3 + 2, 3, 0x001C1C1E); // Right eye
            
            // Smile
            drivers::Framebuffer::draw_line(ix + iw/4 + 2, iy + 2*ih/3, ix + 3*iw/4 - 2, iy + 2*ih/3, 0x001C1C1E);
        }
        else if (i == 1) {
            // Safari: blue/white compass
            int cx = ix + iw/2;
            int cy = iy + ih/2;
            drivers::Framebuffer::draw_circle_filled(cx, cy, iw/2 - 2, 0x00FFFFFF);
            drivers::Framebuffer::draw_circle_filled(cx, cy, iw/2 - 4, 0x000A84FF); // Bright macOS Blue
            
            // Needle
            int len = iw/2 - 6;
            drivers::Framebuffer::draw_line(cx, cy, cx + len/2, cy - len/2, 0x00FF453A); // Red dial half
            drivers::Framebuffer::draw_line(cx, cy, cx - len/2, cy + len/2, 0x00F2F2F7); // White dial half
        }
        else if (i == 2) {
            // Terminal: dark slate rounded rectangle with prompt symbols
            drivers::Framebuffer::draw_rounded_rect_alpha(ix, iy, iw, ih, 10, 0x001C1C1E, 255);
            drivers::Framebuffer::draw_rounded_rect_alpha(ix, iy, iw, ih, 10, 0x0048484A, 120); // Border rim
            
            // Prompt >
            drivers::Framebuffer::draw_line(ix + iw/6, iy + ih/3, ix + iw/3, iy + ih/2, 0x0030D158); // Green prompt
            drivers::Framebuffer::draw_line(ix + iw/3, iy + ih/2, ix + iw/6, iy + 2*ih/3, 0x0030D158);
            // Cursor block
            drivers::Framebuffer::draw_rect_alpha(ix + iw/3 + 4, iy + ih/2 + 2, iw/6, 3, 0x00FFFFFF, 255);
        }
        else if (i == 3) {
            // Notes: yellow header, cream sheet lines
            drivers::Framebuffer::draw_rounded_rect_alpha(ix, iy, iw, ih, 10, 0x00FFFDD0, 255);
            drivers::Framebuffer::draw_rounded_rect_alpha(ix, iy, iw, ih/4 + 4, 10, 0x00FF9F0A, 255); // Orange top band
            drivers::Framebuffer::draw_rect_alpha(ix, iy + ih/4, iw, 4, 0x00FF9F0A, 255);
            
            // Ruled lines
            drivers::Framebuffer::draw_line(ix + 6, iy + ih/2, ix + iw - 6, iy + ih/2, 0x00E5E5EA);
            drivers::Framebuffer::draw_line(ix + 6, iy + ih/2 + 6, ix + iw - 6, iy + ih/2 + 6, 0x00E5E5EA);
            drivers::Framebuffer::draw_line(ix + 6, iy + ih/2 + 12, ix + iw - 6, iy + ih/2 + 12, 0x00E5E5EA);
        }
        else if (i == 4) {
            // System Settings: grey background with gear teeth circles
            drivers::Framebuffer::draw_rounded_rect_alpha(ix, iy, iw, ih, 10, 0x008E8E93, 255);
            drivers::Framebuffer::draw_rounded_rect_alpha(ix, iy, iw, ih, 10, 0x00AEAEB2, 120);
            
            int cx = ix + iw/2;
            int cy = iy + ih/2;
            drivers::Framebuffer::draw_circle_filled(cx, cy, iw/4, 0x00E5E7EB);
            drivers::Framebuffer::draw_circle_filled(cx, cy, iw/8, 0x008E8E93);
            
            // Gear teeth
            for (int a = 0; a < 8; a++) {
                int ox = 0, oy = 0;
                int r_val = iw/3;
                if (a == 0) { ox = 0; oy = -r_val; }
                else if (a == 1) { ox = (int)(r_val * 0.7f); oy = -(int)(r_val * 0.7f); }
                else if (a == 2) { ox = r_val; oy = 0; }
                else if (a == 3) { ox = (int)(r_val * 0.7f); oy = (int)(r_val * 0.7f); }
                else if (a == 4) { ox = 0; oy = r_val; }
                else if (a == 5) { ox = -(int)(r_val * 0.7f); oy = (int)(r_val * 0.7f); }
                else if (a == 6) { ox = -r_val; oy = 0; }
                else if (a == 7) { ox = -(int)(r_val * 0.7f); oy = -(int)(r_val * 0.7f); }
                drivers::Framebuffer::draw_circle_filled(cx + ox, cy + oy, 3, 0x00E5E7EB);
            }
        }

        // Draw Active App Dot Indicator
        bool app_running = false;
        for (Window* w = window_list_head; w; w = w->next) {
            if (w->title_is(app_names[i])) {
                app_running = true;
                break;
            }
        }
        if (app_running) {
            int dot_x = ix + iw/2;
            int dot_y = dy + DOCK_HEIGHT - 6;
            drivers::Framebuffer::draw_circle_filled(dot_x, dot_y, 2, dark_mode ? 0x00FFFFFF : 0x001C1C1E);
        }
    }

    // 3. Draw tooltip above the hovered icon (ensuring it renders on top of adjacent icons)
    if (hovered_idx >= 0) {
        const char* label = app_names[hovered_idx];
        int tw = get_string_width(label, scale_font(12.0f));
        int t_w = tw + 16;
        int t_h = (int)(20 * ui_scale);
        int t_x = hover_ix + hover_iw / 2 - t_w / 2;
        int t_y = hover_iy - t_h - 4;

        drivers::Framebuffer::draw_rounded_rect_alpha(t_x, t_y, t_w, t_h, 6, dark_mode ? 0x001E293B : 0x001C1C1E, 220);
        draw_string(label, t_x + 8, t_y + (t_h - (int)(12 * ui_scale))/2, 0x00FFFFFF, 12.0f);
    }
}

void WindowManager::draw_taskbar() {
    int width = drivers::Framebuffer::get_width();
    int height = drivers::Framebuffer::get_height();
    int tb_y = height - TASKBAR_HEIGHT;

    // Base background: Blue Luna gradient (simulated using rectangles)
    drivers::Framebuffer::draw_rect_alpha(0, tb_y, width, TASKBAR_HEIGHT, 0x00245EDB, 255);
    drivers::Framebuffer::draw_rect_alpha(0, tb_y, width, 2, 0x003A80F2, 255); // Top highlight
    drivers::Framebuffer::draw_rect_alpha(0, tb_y + 2, width, 1, 0x001B469C, 255); // Inner shadow

    // Start Button (x: 0 to 100)
    drivers::Framebuffer::draw_rounded_rect_alpha(0, tb_y, 100, TASKBAR_HEIGHT, 8, 0x00388A3F, 255);
    drivers::Framebuffer::draw_rect_alpha(0, tb_y + TASKBAR_HEIGHT - 4, 100, 4, 0x00215A26, 255); // bottom highlight
    
    // Procedural XP Logo flag next to start text
    int fx = 12, fy = tb_y + 12;
    drivers::Framebuffer::draw_rect_alpha(fx, fy, 7, 7, 0x00FF4B2B, 255); // Red
    drivers::Framebuffer::draw_rect_alpha(fx + 8, fy, 7, 7, 0x00388A3F, 255); // Green
    drivers::Framebuffer::draw_rect_alpha(fx, fy + 8, 7, 7, 0x00245EDB, 255); // Blue
    drivers::Framebuffer::draw_rect_alpha(fx + 8, fy + 8, 7, 7, 0x00FFBD2E, 255); // Yellow

    draw_string("start", 34, tb_y + 9, C_WHITE, 17.0f);

    // Open windows tabs (start at x: 110, width 130 per tab, gap 5)
    int tab_idx = 0;
    for (Window* w = window_list_head; w; w = w->next) {
        int tab_x = 110 + tab_idx * 135;
        if (tab_x + 130 > width - 110) break; // Don't overlap system tray

        bool active = (w == active_window && !w->is_minimized);
        uint32_t tab_bg = active ? 0x001B469C : 0x003A80F2;
        drivers::Framebuffer::draw_rounded_rect_alpha(tab_x, tb_y + 4, 130, TASKBAR_HEIGHT - 8, 6, tab_bg, 255);
        drivers::Framebuffer::draw_rect_alpha(tab_x, tb_y + TASKBAR_HEIGHT - 8, 130, 1, 0x00163370, 255);
        
        char title_trunc[13];
        int j = 0;
        while (w->title[j] && j < 12) {
            title_trunc[j] = w->title[j];
            j++;
        }
        title_trunc[j] = '\0';
        draw_string(title_trunc, tab_x + 8, tb_y + 11, C_WHITE, 13.0f);
        tab_idx++;
    }

    // System Tray (x: width - 110 to width)
    int tray_x = width - 110;
    drivers::Framebuffer::draw_rect_alpha(tray_x, tb_y, 110, TASKBAR_HEIGHT, 0x000F52BA, 255);
    drivers::Framebuffer::draw_rect_alpha(tray_x, tb_y, 2, TASKBAR_HEIGHT, 0x001B469C, 255); // Divider line
    
    // Volume icon
    int vx = tray_x + 12;
    int vy = tb_y + 16;
    drivers::Framebuffer::draw_rect_alpha(vx, vy + 2, 4, 4, C_WHITE, 255);
    for (int i = 0; i < 8; i++) {
        drivers::Framebuffer::draw_rect_alpha(vx + 4, vy + 5 - i/2, 1, i + 1, C_WHITE, 255);
    }
    // Network icon
    int nx = tray_x + 30;
    drivers::Framebuffer::draw_rect_alpha(nx, vy, 10, 8, C_WHITE, 255);
    drivers::Framebuffer::draw_rect_alpha(nx + 1, vy + 1, 8, 6, 0x000F52BA, 255);
    drivers::Framebuffer::draw_rect_alpha(nx - 2, vy + 9, 14, 2, C_WHITE, 255);

    // Clock
    draw_string("14:35", tray_x + 55, tb_y + 11, C_WHITE, 14.0f);
}

void WindowManager::draw_start_menu() {
    int height = drivers::Framebuffer::get_height();
    int menu_w = 380;
    int menu_h = 450;
    int menu_x = 0;
    int menu_y = height - TASKBAR_HEIGHT - menu_h;

    // Outer shadow
    for (int i = 1; i <= 4; i++) {
        drivers::Framebuffer::draw_rect_alpha(menu_x + i, menu_y + i, menu_w, menu_h, 0, 40);
    }

    // Top blue banner (Height: 50)
    drivers::Framebuffer::draw_rect_alpha(menu_x, menu_y, menu_w, 50, 0x00245EDB, 255);
    drivers::Framebuffer::draw_rect_alpha(menu_x, menu_y + 49, menu_w, 1, 0x001B469C, 255);
    
    // User Profile Icon
    int avatar_x = menu_x + 12;
    int avatar_y = menu_y + 10;
    drivers::Framebuffer::draw_circle_filled(avatar_x + 15, avatar_y + 15, 16, C_WHITE);
    drivers::Framebuffer::draw_circle_filled(avatar_x + 15, avatar_y + 15, 14, 0x000F52BA);
    drivers::Framebuffer::draw_pixel(avatar_x + 11, avatar_y + 12, C_WHITE);
    drivers::Framebuffer::draw_pixel(avatar_x + 19, avatar_y + 12, C_WHITE);
    drivers::Framebuffer::draw_rect_alpha(avatar_x + 11, avatar_y + 18, 9, 2, C_WHITE, 255);
    drivers::Framebuffer::draw_pixel(avatar_x + 10, avatar_y + 17, C_WHITE);
    drivers::Framebuffer::draw_pixel(avatar_x + 20, avatar_y + 17, C_WHITE);

    draw_string("Windows User", menu_x + 50, menu_y + 15, C_WHITE, 16.0f);

    // Left Column (Programs list): Width 220, Height 355
    int col_l_w = 220;
    int col_h = 355;
    drivers::Framebuffer::draw_rect_alpha(menu_x, menu_y + 50, col_l_w, col_h, C_WHITE, 255);

    // Right Column (System paths): Width 160, Height 355
    int col_r_x = menu_x + col_l_w;
    int col_r_w = menu_w - col_l_w;
    drivers::Framebuffer::draw_rect_alpha(col_r_x, menu_y + 50, col_r_w, col_h, 0x00D3E5FA, 255);
    drivers::Framebuffer::draw_rect_alpha(col_r_x, menu_y + 50, 1, col_h, 0x009BBEE6, 255); // Column divider

    // Draw Left Column items (popular programs)
    struct StartProg { const char* name; uint32_t icon_c; const char* letter; };
    StartProg progs[] = {
        {"Internet Explorer", 0x0000A2C9, "IE"},
        {"Outlook Express", 0x00E0E0E0, "OE"},
        {"Code Editor", 0x001E1E1E, "CE"},
        {"Command Prompt", 0x001A1A1A, "CP"},
        {"Notepad", 0x00FFFCEB, "NP"},
        {"App Store", 0x001B72E8, "AS"}
    };
    for (int i = 0; i < 6; i++) {
        int iy = menu_y + 50 + i * 45;
        // Hover effect
        if (mouse_x >= menu_x && mouse_x < menu_x + col_l_w && mouse_y >= iy && mouse_y < iy + 45) {
            drivers::Framebuffer::draw_rect_alpha(menu_x + 2, iy + 2, col_l_w - 4, 41, 0x00E5F3FF, 255);
            drivers::Framebuffer::draw_rect_alpha(menu_x + 2, iy + 2, col_l_w - 4, 1, 0x00C4E2FF, 255);
        }

        // Draw small program icon
        drivers::Framebuffer::draw_rounded_rect_alpha(menu_x + 12, iy + 6, 32, 32, 6, progs[i].icon_c, 255);
        draw_string(progs[i].letter, menu_x + 20, iy + 16, C_WHITE, 12.0f);

        draw_string(progs[i].name, menu_x + 52, iy + 14, C_TEXT, 14.0f);
    }

    // Draw Right Column items (system paths and actions)
    struct StartShortcut { const char* name; int y_offset; bool bold; };
    StartShortcut shortcuts[] = {
        {"My Documents", 55, true},
        {"My Pictures", 90, false},
        {"My Music", 125, false},
        {"My Computer", 180, true},
        {"Control Panel", 235, false},
        {"Run...", 290, false}
    };
    
    // Draw right column background separators
    drivers::Framebuffer::draw_rect_alpha(col_r_x + 10, menu_y + 168, col_r_w - 20, 1, 0x009BBEE6, 255);
    drivers::Framebuffer::draw_rect_alpha(col_r_x + 10, menu_y + 224, col_r_w - 20, 1, 0x009BBEE6, 255);

    for (int i = 0; i < 6; i++) {
        int iy = menu_y + shortcuts[i].y_offset;
        // Hover
        if (mouse_x >= col_r_x && mouse_x < menu_x + menu_w && mouse_y >= iy && mouse_y < iy + 35) {
            drivers::Framebuffer::draw_rect_alpha(col_r_x + 2, iy, col_r_w - 4, 30, 0x00B5D3F7, 255);
        }
        uint32_t tc = shortcuts[i].bold ? 0x001B469C : C_TEXT;
        draw_string(shortcuts[i].name, col_r_x + 12, iy + 7, tc, 13.0f);
    }

    // Bottom blue footer (Height: 45)
    int footer_y = menu_y + 50 + col_h;
    drivers::Framebuffer::draw_rect_alpha(menu_x, footer_y, menu_w, 45, 0x00245EDB, 255);
    drivers::Framebuffer::draw_rect_alpha(menu_x, footer_y, menu_w, 1, 0x003A80F2, 255); // Top highlight

    // Log Off Button
    int logoff_x = menu_x + 160;
    if (mouse_x >= logoff_x && mouse_x < logoff_x + 90 && mouse_y >= footer_y && mouse_y < footer_y + 45) {
        drivers::Framebuffer::draw_rect_alpha(logoff_x, footer_y + 4, 86, 37, 0x001B469C, 255);
    }
    // Orange logoff key icon
    drivers::Framebuffer::draw_rect_alpha(logoff_x + 8, footer_y + 16, 12, 12, 0x00FF8E00, 255);
    draw_string("Log Off", logoff_x + 26, footer_y + 15, C_WHITE, 14.0f);

    // Turn Off Computer Button
    int turnoff_x = menu_x + 265;
    if (mouse_x >= turnoff_x && mouse_x < turnoff_x + 110 && mouse_y >= footer_y && mouse_y < footer_y + 45) {
        drivers::Framebuffer::draw_rect_alpha(turnoff_x, footer_y + 4, 110, 37, 0x001B469C, 255);
    }
    // Red power button icon
    drivers::Framebuffer::draw_circle_filled(turnoff_x + 10, footer_y + 22, 6, 0x00FF4B2B);
    draw_string("Turn Off", turnoff_x + 24, footer_y + 15, C_WHITE, 14.0f);
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------
static void get_vfs_path(const char* active_dir, const char* name, char* out_path) {
    if (str_equal(active_dir, "Desktop")) {
        str_copy(out_path, "/Desktop/", 256);
    } else if (str_equal(active_dir, "Documents")) {
        str_copy(out_path, "/Documents/", 256);
    } else if (str_equal(active_dir, "Applications")) {
        str_copy(out_path, "/Applications/", 256);
    } else {
        str_copy(out_path, active_dir, 256);
        size_t len = str_len(out_path);
        if (len > 0 && out_path[len - 1] != '/') {
            out_path[len] = '/';
            out_path[len + 1] = '\0';
        }
    }
    size_t len = str_len(out_path);
    str_copy(out_path + len, name, 256 - len);
}

void WindowManager::remove_vfs_node(const char* path) {
    const char* rel_path = path;
    if (path[0] == '/') rel_path = path + 1;
    for (size_t i = 0; i < fs::VFS::node_count; ++i) {
        if (str_equal(fs::VFS::child_nodes[i].name, rel_path)) {
            if (fs::VFS::child_nodes[i].data) {
                kernel::kfree(fs::VFS::child_nodes[i].data);
            }
            for (size_t j = i; j < fs::VFS::node_count - 1; ++j) {
                fs::VFS::child_nodes[j] = fs::VFS::child_nodes[j + 1];
            }
            fs::VFS::node_count--;
            break;
        }
    }
}

void WindowManager::delete_desktop_item(int sel) {
    if (sel < 0 || sel >= desktop_item_count) return;
    DiskItem it = desktop_items[sel];
    
    // 1. Delete from VFS
    char vfs_path[256];
    get_vfs_path(it.path, it.name, vfs_path);
    
    const char* rel_path = vfs_path;
    if (vfs_path[0] == '/') rel_path = vfs_path + 1;
    
    for (size_t i = 0; i < fs::VFS::node_count; ++i) {
        if (str_equal(fs::VFS::child_nodes[i].name, rel_path)) {
            if (fs::VFS::child_nodes[i].data) {
                kernel::kfree(fs::VFS::child_nodes[i].data);
            }
            for (size_t j = i; j < fs::VFS::node_count - 1; ++j) {
                fs::VFS::child_nodes[j] = fs::VFS::child_nodes[j + 1];
            }
            fs::VFS::node_count--;
            break;
        }
    }

    // If it's a directory, recursively delete children from VFS
    if (it.is_directory) {
        char prefix[256];
        str_copy(prefix, rel_path, 256);
        size_t len = str_len(prefix);
        prefix[len++] = '/';
        prefix[len] = '\0';
        for (size_t i = 0; i < fs::VFS::node_count; ) {
            if (title_starts_with_custom(fs::VFS::child_nodes[i].name, prefix)) {
                if (fs::VFS::child_nodes[i].data) {
                    kernel::kfree(fs::VFS::child_nodes[i].data);
                }
                for (size_t j = i; j < fs::VFS::node_count - 1; ++j) {
                    fs::VFS::child_nodes[j] = fs::VFS::child_nodes[j + 1];
                }
                fs::VFS::node_count--;
            } else {
                i++;
            }
        }
    }

    // 2. Remove from desktop_items
    for (int j = sel; j < desktop_item_count - 1; j++) {
        desktop_items[j] = desktop_items[j + 1];
    }
    desktop_item_count--;

    // If it was a directory, also remove all items inside it from desktop_items
    if (it.is_directory) {
        for (int j = 0; j < desktop_item_count; ) {
            if (str_equal(desktop_items[j].path, it.name)) {
                for (int k = j; k < desktop_item_count - 1; k++) {
                    desktop_items[k] = desktop_items[k + 1];
                }
                desktop_item_count--;
            } else {
                j++;
            }
        }
    }
}

void WindowManager::execute_menu_action(int action_id) {
    int width = (int)drivers::Framebuffer::get_width();
    int height = (int)drivers::Framebuffer::get_height();

    const char* active_dir = "Desktop";
    if (active_window && active_window->title_is("Finder")) {
        active_dir = active_window->text_input;
        if (active_window->text_len == 0) active_dir = "Desktop";
    }

    if (action_id == 0) { // About This Mac
        create_window((width - 500) / 2, (height - 320) / 2, 500, 320, "About This Mac", C_FINDER);
    } else if (action_id == 1 || action_id == 2) { // Restart or Shut Down
        drivers::Serial::println("[SYSTEM] Initiating shutdown sequence...");
        __asm__ volatile("cli; hlt");
    } else if (action_id == 3) { // New Finder Window
        create_window(200, 150, 500, 350, "Finder", C_FINDER);
    } else if (action_id == 4) { // Close All Windows
        while (window_list_head) close_window(window_list_head->id);
    } else if (action_id == 5) { // New Folder
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
        int next_y = 60;
        if (str_equal(active_dir, "Desktop")) {
            for (int i = 0; i < desktop_item_count; i++) {
                if (str_equal(desktop_items[i].path, "Desktop") && desktop_items[i].y >= next_y) {
                    next_y = desktop_items[i].y + 90;
                }
            }
        }
        add_item(folder_name, true, false, active_dir, 30, next_y);
        for (Window* w = window_list_head; w; w = w->next) {
            if (w->title_is("Finder")) w->finder_needs_reload = true;
        }
        if (active_window && active_window->title_is("Finder")) {
            populate_finder_cache(active_window);
            for (int i = 0; i < active_window->finder_item_count; i++) {
                if (str_equal(active_window->finder_items[i].name, folder_name)) {
                    active_window->selected_item_idx = i;
                    is_renaming_item = true;
                    str_copy(rename_original_name, folder_name, sizeof(rename_original_name));
                    break;
                }
            }
        }
        force_redraw_all();
    } else if (action_id == 6) { // New Text File
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
        int next_y = 60;
        if (str_equal(active_dir, "Desktop")) {
            for (int i = 0; i < desktop_item_count; i++) {
                if (str_equal(desktop_items[i].path, "Desktop") && desktop_items[i].y >= next_y) {
                    next_y = desktop_items[i].y + 90;
                }
            }
        }
        add_item(file_name, false, false, active_dir, 30, next_y);
        for (Window* w = window_list_head; w; w = w->next) {
            if (w->title_is("Finder")) w->finder_needs_reload = true;
        }
        if (active_window && active_window->title_is("Finder")) {
            populate_finder_cache(active_window);
            for (int i = 0; i < active_window->finder_item_count; i++) {
                if (str_equal(active_window->finder_items[i].name, file_name)) {
                    active_window->selected_item_idx = i;
                    is_renaming_item = true;
                    str_copy(rename_original_name, file_name, sizeof(rename_original_name));
                    break;
                }
            }
        }
        force_redraw_all();
    } else if (action_id == 7) { // Open Terminal
        create_window((width - 500)/2, (height - 350)/2, 500, 350, "Terminal", C_TERMINAL);
    } else if (action_id == 8) { // Clean Up
        arrange_desktop();
    } else if (action_id == 9 || action_id == 10) { // Copy / Cut
        int sel = -1;
        if (active_window && active_window->title_is("Finder")) {
            sel = active_window->selected_item_idx;
            if (sel >= 0 && sel < active_window->finder_item_count) {
                clipboard_item_idx = sel;
                clipboard_is_cut = (action_id == 10);
                get_vfs_path(active_dir, active_window->finder_items[sel].name, clipboard_path);
            }
        } else {
            for (int j = 0; j < desktop_item_count; j++) {
                if (desktop_items[j].selected) { sel = j; break; }
            }
            if (sel != -1) {
                clipboard_item_idx = sel;
                clipboard_is_cut = (action_id == 10);
                get_vfs_path(desktop_items[sel].path, desktop_items[sel].name, clipboard_path);
            }
        }
    } else if (action_id == 11) { // Rename
        is_renaming_item = true;
        if (active_window && active_window->title_is("Finder")) {
            int sel = active_window->selected_item_idx;
            if (sel >= 0 && sel < active_window->finder_item_count) {
                str_copy(rename_original_name, active_window->finder_items[sel].name, sizeof(rename_original_name));
            }
        } else if (active_window && !active_window->title_is("Finder")) {
            active_window = nullptr;
        }
    } else if (action_id == 12) { // Delete
        if (active_window && active_window->title_is("Finder")) {
            int sel = active_window->selected_item_idx;
            active_window->selected_item_idx = -1;
            if (sel >= 0 && sel < active_window->finder_item_count) {
                delete_finder_item(active_dir, active_window->finder_items[sel].name, active_window->finder_items[sel].type == fs::NodeType::DIRECTORY);
                for (Window* w = window_list_head; w; w = w->next) {
                    if (w->title_is("Finder")) w->finder_needs_reload = true;
                }
            }
        } else {
            int sel = -1;
            for (int j = 0; j < desktop_item_count; j++) {
                if (desktop_items[j].selected) { sel = j; break; }
            }
            if (sel != -1) {
                delete_desktop_item(sel);
                for (Window* w = window_list_head; w; w = w->next) {
                    if (w->title_is("Finder")) w->finder_needs_reload = true;
                }
            }
        }
    } else if (action_id == 13) { // Paste
        if (clipboard_path[0] != '\0') {
            fs::VFSNode* src_node = fs::VFS::open(clipboard_path);
            if (src_node) {
                const char* base_name = src_node->name;
                int last_slash = -1;
                for (int j = 0; clipboard_path[j] != '\0'; j++) {
                    if (clipboard_path[j] == '/') last_slash = j;
                }
                if (last_slash != -1) base_name = clipboard_path + last_slash + 1;

                char dest_path[256];
                get_vfs_path(active_dir, base_name, dest_path);

                const char* rel_dest = dest_path;
                if (dest_path[0] == '/') rel_dest = dest_path + 1;
                
                for (size_t i = 0; i < fs::VFS::node_count; ++i) {
                    if (str_equal(fs::VFS::child_nodes[i].name, rel_dest)) {
                        if (fs::VFS::child_nodes[i].data) {
                            kernel::kfree(fs::VFS::child_nodes[i].data);
                        }
                        for (size_t j = i; j < fs::VFS::node_count - 1; ++j) {
                            fs::VFS::child_nodes[j] = fs::VFS::child_nodes[j + 1];
                        }
                        fs::VFS::node_count--;
                        break;
                    }
                }
                for (int j = 0; j < desktop_item_count; j++) {
                    if (str_equal(desktop_items[j].path, active_dir) && str_equal(desktop_items[j].name, base_name)) {
                        for (int k = j; k < desktop_item_count - 1; k++) {
                            desktop_items[k] = desktop_items[k + 1];
                        }
                        desktop_item_count--;
                        break;
                    }
                }

                char* new_data = nullptr;
                if (src_node->size > 0 && src_node->data) {
                    new_data = (char*)kernel::kmalloc(src_node->size + 4096);
                    if (new_data) {
                        memcpy(new_data, src_node->data, src_node->size);
                    }
                }
                
                fs::VFSNode* dst_node = fs::VFS::create_file(dest_path, new_data, src_node->size + 4096);
                if (dst_node) {
                    dst_node->type = src_node->type;
                    dst_node->size = src_node->size;
                }

                bool is_term = false;
                if (str_equal(base_name, "Terminal.app") || str_equal(base_name, "Terminal")) {
                    is_term = true;
                }
                add_item(base_name, src_node->type == fs::NodeType::DIRECTORY, is_term, active_dir, 0, 0);

                if (clipboard_is_cut) {
                    remove_vfs_node(clipboard_path);
                    for (int j = 0; j < desktop_item_count; j++) {
                        char src_dir[256];
                        str_copy(src_dir, clipboard_path, 256);
                        int last_s = -1;
                        for (int k = 0; src_dir[k] != '\0'; k++) {
                            if (src_dir[k] == '/') last_s = k;
                        }
                        if (last_s != -1) {
                            src_dir[last_s] = '\0';
                        }
                        const char* search_dir = src_dir;
                        if (src_dir[0] == '/') search_dir = src_dir + 1;
                        
                        if (str_equal(desktop_items[j].path, search_dir) && str_equal(desktop_items[j].name, base_name)) {
                            for (int k = j; k < desktop_item_count - 1; k++) {
                                desktop_items[k] = desktop_items[k + 1];
                            }
                            desktop_item_count--;
                            break;
                        }
                    }
                    clipboard_path[0] = '\0';
                }

                for (Window* w = window_list_head; w; w = w->next) {
                    if (w->title_is("Finder")) w->finder_needs_reload = true;
                }
            }
        }
    } else if (action_id == 14) { // Open (Desktop Item)
        int sel = -1;
        for (int j = 0; j < desktop_item_count; j++) {
            if (desktop_items[j].selected) { sel = j; break; }
        }
        if (sel != -1) {
            DiskItem& it = desktop_items[sel];
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
                    char full_path[256];
                    get_vfs_path(it.path, it.name, full_path);
                    
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
        }
    } else if (action_id == 15) { // Eject USB
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
    } else if (action_id >= 16 && action_id <= 19) { // Set Theme presets (0, 1, 2, 3)
        set_theme(action_id - 16);
    } else if (action_id == 20) { // Minimize
        if (active_window) minimize_window_animated(active_window);
    } else if (action_id == 21) { // Zoom
        if (active_window) {
            if (active_window->is_maximized) {
                active_window->rect = active_window->orig_rect;
                active_window->is_maximized = false;
            } else {
                active_window->orig_rect = active_window->rect;
                active_window->rect = {0, MENU_BAR_HEIGHT, width, height - TASKBAR_HEIGHT - MENU_BAR_HEIGHT};
                active_window->is_maximized = true;
            }
        }
    } else if (action_id == 23) { // Help
        create_window(250, 180, 600, 400, "Safari", C_FINDER);
    } else if (action_id == 24) { // Send Feedback
        create_window(250, 180, 600, 400, "Safari", C_FINDER);
    }
}

static int populate_menu_items(int type, MenuItem* list) {
    int count = 0;
    bool selected_usb = false;

    if (type == 0) { // Apple Menu
        list[count++] = {"About This Mac", 0, -1, true};
        list[count++] = {"Restart", 1, -1, true};
        list[count++] = {"Shut Down", 2, -1, true};
    } else if (type == 1) { // Finder Menu Bar
        list[count++] = {"New Window", 3, -1, true};
        list[count++] = {"Close All", 4, -1, true};
    } else if (type == 2) { // Desktop Empty Space
        list[count++] = {"New", -1, 100, true};
        list[count++] = {"Open Terminal", 7, -1, true};
        list[count++] = {"Theme", -1, 101, true};
        list[count++] = {"Clean Up", 8, -1, true};
    } else if (type == 3) { // Finder File Menu
        list[count++] = {"New Finder Window", 3, -1, true};
        list[count++] = {"Close Window", 4, -1, true};
    } else if (type == 4) { // Finder Edit Menu
        list[count++] = {"Undo", 1000, -1, false};
        list[count++] = {"Redo", 1001, -1, false};
        list[count++] = {"Cut", 10, -1, true};
        list[count++] = {"Copy", 9, -1, true};
    } else if (type == 5) { // Finder View Menu
        list[count++] = {"As Icons", 1002, -1, true};
        list[count++] = {"As List", 1003, -1, true};
    } else if (type == 6) { // Finder Go Menu
        list[count++] = {"Applications", 1004, -1, true};
        list[count++] = {"Documents", 1005, -1, true};
        list[count++] = {"Downloads", 1006, -1, true};
        list[count++] = {"Home", 1007, -1, true};
    } else if (type == 7) { // Finder Window Menu
        list[count++] = {"Minimize", 20, -1, true};
        list[count++] = {"Zoom", 21, -1, true};
        list[count++] = {"Bring All to Front", 22, -1, true};
    } else if (type == 8) { // Finder Help Menu
        list[count++] = {"Crecent Help", 23, -1, true};
        list[count++] = {"Send Feedback", 24, -1, true};
    } else if (type == 9) { // Finder Item Context Menu
        list[count++] = {"Copy", 9, -1, true};
        list[count++] = {"Cut", 10, -1, true};
        list[count++] = {"Rename", 11, -1, true};
        list[count++] = {"Delete", 12, -1, true};
    } else if (type == 10) { // Finder Empty Space
        bool has_clipboard = (clipboard_path[0] != '\0');
        list[count++] = {"Paste", 13, -1, has_clipboard};
        list[count++] = {"New", -1, 100, true};
    } else if (type == 11) { // Desktop Item Context Menu
        for (int j = 0; j < desktop_item_count; j++) {
            if (desktop_items[j].selected && str_equal(desktop_items[j].name, "USB_DRIVE")) {
                selected_usb = true;
                break;
            }
        }
        if (selected_usb) {
            list[count++] = {"Open", 14, -1, true};
            list[count++] = {"Rename", 11, -1, true};
            list[count++] = {"Eject USB", 15, -1, true};
            list[count++] = {"Clean Up", 8, -1, true};
        } else {
            list[count++] = {"Open", 14, -1, true};
            list[count++] = {"Rename", 11, -1, true};
            list[count++] = {"Delete", 12, -1, true};
            list[count++] = {"Clean Up", 8, -1, true};
        }
    } else if (type == 100) { // New Submenu
        list[count++] = {"Folder", 5, -1, true};
        list[count++] = {"Text File", 6, -1, true};
    } else if (type == 101) { // Theme Submenu
        list[count++] = {"Light", 16, -1, true};
        list[count++] = {"Dark", 17, -1, true};
        list[count++] = {"Nord", 18, -1, true};
        list[count++] = {"Warm", 19, -1, true};
    }
    return count;
}

static void draw_menu_block(int mx, int my, int mw, int mh, const MenuItem* items, int count, int hovered_idx) {
    // 1. Draw shadow
    for (int i = 0; i < 4; ++i) {
        drivers::Framebuffer::draw_rounded_rect_alpha(
            mx - i + 2, my - i + 2,
            mw + i * 2, mh + i * 2,
            8 + i, C_BLACK, 25 - i * 5);
    }

    // 2. Translucent menu background
    drivers::Framebuffer::draw_rounded_rect_alpha(
        mx, my, mw, mh, 8, C_MENU_BG, 230);

    // 3. Outer thin border
    uint32_t border_color = C_MENU_LINE;
    drivers::Framebuffer::draw_rounded_rect_alpha(
        mx, my, mw, mh, 8, border_color, 120);

    // 4. Render items
    for (int i = 0; i < count; ++i) {
        int iy = my + MENU_PADDING + i * MENU_ITEM_HEIGHT;
        
        if (!items[i].enabled) {
            // Grayed-out state
            WindowManager::draw_string(items[i].label, mx + 12, iy + 4, C_TEXT_MUTED, 15.0f);
        } else if (hovered_idx == i) {
            // Hover highlight
            drivers::Framebuffer::draw_rounded_rect_alpha(
                mx + 4, iy, mw - 8, 24, 4, C_HOVER, 255);
            WindowManager::draw_string(items[i].label, mx + 12, iy + 4, C_WHITE, 15.0f);
        } else {
            // Normal state
            WindowManager::draw_string(items[i].label, mx + 12, iy + 4, C_TEXT, 15.0f);
        }

        // Draw indicator if item has a submenu
        if (items[i].submenu_type != -1) {
            int rx = mx + mw - 16;
            int ry = iy + 12;
            drivers::Framebuffer::draw_line(rx, ry - 4, rx + 4, ry, items[i].enabled ? (hovered_idx == i ? C_WHITE : C_TEXT) : C_TEXT_MUTED);
            drivers::Framebuffer::draw_line(rx + 4, ry, rx, ry + 4, items[i].enabled ? (hovered_idx == i ? C_WHITE : C_TEXT) : C_TEXT_MUTED);
        }

        // Draw horizontal separators
        bool separator_after = false;
        if (str_equal(items[i].label, "About This Mac") ||
            str_equal(items[i].label, "Open Terminal") ||
            str_equal(items[i].label, "New File.txt") ||
            str_equal(items[i].label, "New Folder") ||
            str_equal(items[i].label, "Downloads") ||
            str_equal(items[i].label, "Eject USB") ||
            str_equal(items[i].label, "Delete")) {
            separator_after = true;
        }

        if (separator_after && i < count - 1) {
            drivers::Framebuffer::draw_rect_alpha(
                mx + 8, iy + MENU_ITEM_HEIGHT - 1,
                mw - 16, 1, C_MENU_LINE, 100);
        }
    }
}

void WindowManager::draw_menu() {
    if (!active_menu.active) return;

    Rect clip = drivers::Framebuffer::get_clip_rect();

    // 1. Draw main menu if within the clip boundary (include 8px shadow padding)
    Rect main_rect = {active_menu.x, active_menu.y, active_menu.w, active_menu.h};
    if (clip.intersects(expanded_rect(main_rect, 8))) {
        MenuItem main_items[16];
        int main_count = populate_menu_items(active_menu.type, main_items);
        draw_menu_block(active_menu.x, active_menu.y, active_menu.w, active_menu.h, main_items, main_count, active_menu.hovered_item);
    }

    // 2. Draw submenu if active and within the clip boundary (include 8px shadow padding)
    if (active_submenu.active) {
        Rect sub_rect = {active_submenu.x, active_submenu.y, active_submenu.w, active_submenu.h};
        if (clip.intersects(expanded_rect(sub_rect, 8))) {
            MenuItem sub_items[16];
            int sub_count = populate_menu_items(active_submenu.type, sub_items);
            draw_menu_block(active_submenu.x, active_submenu.y, active_submenu.w, active_submenu.h, sub_items, sub_count, active_submenu.hovered_item);
        }
    }
}

void WindowManager::draw_mac_decorations() {
    Rect clip = drivers::Framebuffer::get_clip_rect();
    int screen_w = (int)drivers::Framebuffer::get_width();
    int screen_h = (int)drivers::Framebuffer::get_height();

    // Top Menu Bar
    Rect menu_bar_rect = {0, 0, screen_w, MENU_BAR_HEIGHT};
    if (clip.intersects(menu_bar_rect)) {
        draw_menu_bar();
    }

    // Bottom Centered Dock
    Rect dock_rect = {(screen_w - DOCK_WIDTH) / 2, screen_h - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM, DOCK_WIDTH, DOCK_HEIGHT};
    if (clip.intersects(dock_rect)) {
        draw_dock();
    }

    // Context Menus (including Apple menu, submenus, and desktop right-clicks)
    if (active_menu.active) {
        draw_menu();
    }
}

// ---------------------------------------------------------------------------
// Redraw utilities
// ---------------------------------------------------------------------------
// Fix 3 & 4: Unified cursor update guarantee and perfectly bounded rectangle clip
void WindowManager::redraw_dirty_rect(const Rect& dirty) {
    if (dirty.w <= 0 || dirty.h <= 0) return;
    DirtyList list;
    list.add(dirty);
    redraw_dirty_list(list);
}

void WindowManager::redraw_dirty_list(const DirtyList& list) {
    if (list.count <= 0) return;

    // Run composition pass individually for each dirty region to avoid drawing the large bounding box union
    for (int i = 0; i < list.count; ++i) {
        const Rect& r = list.rects[i];
        if (r.w <= 0 || r.h <= 0) continue;

        drivers::Framebuffer::set_clip_rect(r);
        draw_wallpaper();
        draw_desktop_icons();
        draw_all_windows();
        draw_mac_decorations();
        draw_cursor();
    }
    drivers::Framebuffer::clear_clip_rect();

    // Copy only the specific dirty regions to VRAM and execute a single hardware page flip
    drivers::Framebuffer::swap_dirty_rects(list.rects, list.count);
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

    bool drag_active = (active_window && (active_window->is_dragging || is_resizing_window));
    if (!drag_active && position_changed) {
        update_hardware_cursor_fast();
    }

    bool needs_redraw = false;
    DirtyList dirty;
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
            if (rx >= 142 && ry >= 35) {
                int clicked_idx = -1;
                
                if (!clicked->finder_list_view_mode) {
                    int grid_col = 0;
                    int grid_row = 0;
                    int columns = (clicked->rect.w - 180) / 90;
                    if (columns < 1) columns = 1;

                    for (int i = 0; i < clicked->finder_item_count; i++) {
                        int ix = 170 + grid_col * 90;
                        int iy = 55 + grid_row * 90;
                        if (rx >= ix && rx < ix + 64 && ry >= iy && ry < iy + 80) {
                            clicked_idx = i;
                            break;
                        }
                        grid_col++;
                        if (grid_col >= columns) {
                            grid_col = 0;
                            grid_row++;
                        }
                    }
                } else {
                    int list_y = 45;
                    int row_h = 24;
                    for (int i = 0; i < clicked->finder_item_count; i++) {
                        int iy = list_y + 22 + i * row_h;
                        if (ry >= iy && ry < iy + row_h) {
                            clicked_idx = i;
                            break;
                        }
                    }
                }
                
                if (clicked_idx != -1) {
                    clicked->selected_item_idx = clicked_idx;
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
                    active_menu.h = 2 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING; // Paste, New
                    active_menu.type = 10; // Finder Empty Context Menu
                    active_menu.hovered_item = -1;
                }

                // Containment within screen boundaries
                if (active_menu.x + active_menu.w > width) {
                    active_menu.x = width - active_menu.w - 4;
                }
                if (active_menu.y + active_menu.h > height) {
                    active_menu.y = height - active_menu.h - 4;
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
            bool clicked_menu = false;
            
            // A. Check click on active submenu
            if (active_submenu.active && in_rect(new_x, new_y, active_submenu.x, active_submenu.y, active_submenu.w, active_submenu.h)) {
                int item = active_submenu.hovered_item;
                active_menu.active = false;
                active_submenu.active = false;
                if (item != -1) {
                    MenuItem sub_items[16];
                    populate_menu_items(active_submenu.type, sub_items);
                    execute_menu_action(sub_items[item].action_id);
                }
                clicked_menu = true;
            }
            // B. Check click on active main menu
            else if (in_rect(new_x, new_y, active_menu.x, active_menu.y, active_menu.w, active_menu.h)) {
                int item = active_menu.hovered_item;
                if (item != -1) {
                    MenuItem main_items[16];
                    populate_menu_items(active_menu.type, main_items);
                    if (main_items[item].submenu_type == -1) {
                        active_menu.active = false;
                        active_submenu.active = false;
                        execute_menu_action(main_items[item].action_id);
                    }
                }
                clicked_menu = true;
            }
            
            if (clicked_menu) {
                if (active_menu.type == 2 || active_menu.type == 9 || active_menu.type == 10 || active_menu.type == 11) {
                    trigger_host_persist();
                }
                force_redraw_all();
            } else {
                active_menu.active = false;
                active_submenu.active = false;
                force_redraw_all();
            }
            state_updated = true;
        } else if (new_y <= MENU_BAR_HEIGHT) {
            if (new_x >= 10 && new_x <= 35) { // Apple Logo
                active_menu = {true, 10, MENU_BAR_HEIGHT, MENU_WIDTH, 3 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 0, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 40 && new_x <= 90) { // Finder
                active_menu = {true, 40, MENU_BAR_HEIGHT, MENU_WIDTH, 2 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 1, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 95 && new_x <= 130) { // File
                active_menu = {true, 95, MENU_BAR_HEIGHT, MENU_WIDTH, 2 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 3, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 135 && new_x <= 170) { // Edit
                active_menu = {true, 135, MENU_BAR_HEIGHT, MENU_WIDTH, 4 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 4, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 175 && new_x <= 210) { // View
                active_menu = {true, 175, MENU_BAR_HEIGHT, MENU_WIDTH, 2 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 5, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 215 && new_x <= 245) { // Go
                active_menu = {true, 215, MENU_BAR_HEIGHT, MENU_WIDTH, 4 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 6, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 250 && new_x <= 300) { // Window
                active_menu = {true, 250, MENU_BAR_HEIGHT, MENU_WIDTH, 3 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 7, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= 305 && new_x <= 345) { // Help
                active_menu = {true, 305, MENU_BAR_HEIGHT, MENU_WIDTH, 2 * MENU_ITEM_HEIGHT + 2 * MENU_PADDING, 8, -1};
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= width - 220 && new_x <= width - 190) { // Wifi toggle
                wifi_connected = !wifi_connected;
                drivers::Serial::print("[WIFI] Status changed: ");
                drivers::Serial::println(wifi_connected ? "Enabled. Connected to CrecentSecure_5G." : "Disabled.");
                force_redraw_all();
                state_updated = true;
            } else if (new_x >= width - 180 && new_x <= width - 130) { // USB toggle
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
                } else if (in_rect(new_x, new_y, wx + 16 - 7, wy + 15 - 7, 14, 14)) {
                    close_window(clicked->id);
                    force_redraw_all();
                } else if (in_rect(new_x, new_y, wx + 36 - 7, wy + 15 - 7, 14, 14)) {
                    minimize_window_animated(clicked);
                } else if (in_rect(new_x, new_y, wx + 56 - 7, wy + 15 - 7, 14, 14)) {
                    if (clicked->is_maximized) {
                        clicked->rect = clicked->orig_rect;
                        clicked->is_maximized = false;
                    } else {
                        clicked->orig_rect = clicked->rect;
                        clicked->rect = {0, MENU_BAR_HEIGHT, width, height - TASKBAR_HEIGHT - MENU_BAR_HEIGHT};
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

                    // Allocate and pre-render window content into private buffer for smooth dragging
                    if (clicked->buffer) {
                        delete[] clicked->buffer;
                        clicked->buffer = nullptr;
                    }
                    clicked->buffer = new uint32_t[clicked->rect.w * clicked->rect.h];
                    if (clicked->buffer) {
                        drivers::Framebuffer::redirect_drawing(clicked->buffer, clicked->rect.w, clicked->rect.h, clicked->rect.w * 4);
                        
                        int saved_x = clicked->rect.x;
                        int saved_y = clicked->rect.y;
                        clicked->rect.x = 0;
                        clicked->rect.y = 0;

                        WindowManager::draw_window_body(clicked, 255, clicked->title_is("Terminal"));

                        bool is_terminal = clicked->title_is("Terminal");
                        if (is_terminal) {
                            WindowManager::draw_terminal_content(clicked);
                        } else if (clicked->title_is("Finder")) {
                            WindowManager::draw_finder_content(clicked);
                        } else if (clicked->title_is("System Log")) {
                            WindowManager::draw_system_log_content(clicked);
                        } else if (clicked->title_is("Performance Monitor")) {
                            WindowManager::draw_perf_monitor_content(clicked);
                        } else if (clicked->title_is("About This Mac")) {
                            WindowManager::draw_about_content(clicked);
                        } else if (clicked->title_is("Safari")) {
                            WindowManager::draw_safari_content(clicked);
                        } else if (clicked->title_is("Mail")) {
                            WindowManager::draw_mail_content(clicked);
                        } else if (clicked->title_is("App Store")) {
                            WindowManager::draw_appstore_content(clicked);
                        } else if (clicked->title_is("Notes")) {
                            WindowManager::draw_notes_content(clicked);
                        } else if (clicked->title_starts_with("Code Editor")) {
                            WindowManager::draw_code_editor_content(clicked);
                        } else if (clicked->title_starts_with("Audio Player")) {
                            WindowManager::draw_audio_player_content(clicked);
                        } else if (clicked->title_starts_with("Picture Viewer")) {
                            WindowManager::draw_picture_viewer_content(clicked);
                        } else if (clicked->title_is("System Settings")) {
                            WindowManager::draw_settings_content(clicked);
                        } else {
                            WindowManager::draw_window_client_area(clicked);
                        }

                        clicked->rect.x = saved_x;
                        clicked->rect.y = saved_y;
                        drivers::Framebuffer::restore_drawing();
                    }
                    
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
                        
                        for (int i = 0; i < 4; ++i) {
                            int bx = scale_dim(20 + i * 90);
                            int by = scale_dim(120);
                            int bw = scale_dim(80);
                            int bh = scale_dim(26);
                            if (rx >= bx && rx < bx + bw && ry >= by && ry < by + bh) {
                                set_theme(i);
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
                    } else if (clicked->title_starts_with("Audio Player")) {
                        int w = clicked->rect.w - 2;
                        int play_x = w / 2 - 90;
                        int pause_x = w / 2 - 25;
                        int stop_x = w / 2 + 40;
                        int btn_y = 115;
                        int btn_w = 50;
                        int btn_h = 30;

                        if (ry >= btn_y && ry < btn_y + btn_h) {
                            if (rx >= play_x && rx < play_x + btn_w) {
                                if (audio_is_wav) {
                                    audio_playing = true;
                                    drivers::AC97::play(0, 0);
                                } else if (audio_note_count > 0) {
                                    audio_playing = true;
                                    audio_play_frequency(audio_notes[audio_note_idx].freq);
                                    audio_note_end_frame = frame_counter + audio_notes[audio_note_idx].duration;
                                }
                            } else if (rx >= pause_x && rx < pause_x + btn_w) {
                                audio_playing = false;
                                audio_play_frequency(0);
                                drivers::AC97::stop();
                            } else if (rx >= stop_x && rx < stop_x + btn_w) {
                                audio_stop();
                                audio_note_idx = 0;
                                audio_wav_phase = 0;
                                fill_wav_buffer_slice(0);
                                fill_wav_buffer_slice(1);
                                next_buffer_to_fill = 0;
                            }
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
                                } else {
                                    clicked->text_input[0] = '\0';
                                    clicked->text_len = 0;
                                }
                                clicked->selected_item_idx = -1;
                                clicked->finder_needs_reload = true;
                            } else if (rx >= clicked->rect.w - 260 && rx < clicked->rect.w - 210) { // Toggle Mode
                                clicked->finder_list_view_mode = !clicked->finder_list_view_mode;
                                clicked->finder_needs_reload = true;
                             } else if (rx >= clicked->rect.w - 190 && rx < clicked->rect.w - 110) { // + Folder
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
                                    
                                    for (int i = 0; i < clicked->finder_item_count; i++) {
                                        if (str_equal(clicked->finder_items[i].name, folder_name)) {
                                            exists = true;
                                            break;
                                        }
                                    }
                                    count++;
                                }
                                add_item(folder_name, true, false, active_dir, 0, 0);
                                for (Window* w = window_list_head; w; w = w->next) {
                                    if (w->title_is("Finder")) w->finder_needs_reload = true;
                                }
                                populate_finder_cache(clicked);
                                for (int i = 0; i < clicked->finder_item_count; i++) {
                                    if (str_equal(clicked->finder_items[i].name, folder_name)) {
                                        clicked->selected_item_idx = i;
                                        is_renaming_item = true;
                                        str_copy(rename_original_name, folder_name, sizeof(rename_original_name));
                                        break;
                                    }
                                }
                                force_redraw_all();
                            } else if (rx >= clicked->rect.w - 100 && rx < clicked->rect.w - 20) { // + File
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
                                    
                                    for (int i = 0; i < clicked->finder_item_count; i++) {
                                        if (str_equal(clicked->finder_items[i].name, file_name)) {
                                            exists = true;
                                            break;
                                        }
                                    }
                                    count++;
                                }
                                add_item(file_name, false, false, active_dir, 0, 0);
                                for (Window* w = window_list_head; w; w = w->next) {
                                    if (w->title_is("Finder")) w->finder_needs_reload = true;
                                }
                                populate_finder_cache(clicked);
                                for (int i = 0; i < clicked->finder_item_count; i++) {
                                    if (str_equal(clicked->finder_items[i].name, file_name)) {
                                        clicked->selected_item_idx = i;
                                        is_renaming_item = true;
                                        str_copy(rename_original_name, file_name, sizeof(rename_original_name));
                                        break;
                                    }
                                }
                                force_redraw_all();
                            } else if (rx >= 220 && rx < clicked->rect.w - 270) {
                                clicked->finder_editing_path = true;
                                clicked->selected_item_idx = -1;
                            } else {
                                clicked->finder_editing_path = false;
                            }
                            force_redraw_all();
                        } else if (rx >= 8 && rx < 142) {
                            clicked->finder_editing_path = false;
                            for (int i = 0; i < 6; i++) {
                                int iy = 40 + i * 30;
                                if (ry >= iy - 4 && ry < iy + 20) {
                                    const char* favorites[] = {"Desktop", "Applications", "Documents", "/", "/tar", "/disk"};
                                    int len = 0;
                                    while (favorites[i][len] && len < 250) {
                                        clicked->text_input[len] = favorites[i][len];
                                        len++;
                                    }
                                    clicked->text_input[len] = '\0';
                                    clicked->text_len = len;
                                    clicked->selected_item_idx = -1;
                                    clicked->finder_needs_reload = true;
                                    force_redraw_all();
                                    break;
                                }
                            }
                        } else if (rx >= 142) {
                            clicked->finder_editing_path = false;
                            int clicked_idx = -1;
                            
                            if (!clicked->finder_list_view_mode) {
                                int grid_col = 0;
                                int grid_row = 0;
                                int columns = (clicked->rect.w - 180) / 90;
                                if (columns < 1) columns = 1;

                                for (int i = 0; i < clicked->finder_item_count; i++) {
                                    int ix = 170 + grid_col * 90;
                                    int iy = 55 + grid_row * 90;
                                    if (rx >= ix && rx < ix + 64 && ry >= iy && ry < iy + 80) {
                                        clicked_idx = i;
                                        break;
                                    }
                                    grid_col++;
                                    if (grid_col >= columns) {
                                        grid_col = 0;
                                        grid_row++;
                                    }
                                }
                            } else {
                                int list_y = 45;
                                int row_h = 24;
                                for (int i = 0; i < clicked->finder_item_count; i++) {
                                    int iy = list_y + 22 + i * row_h;
                                    if (ry >= iy && ry < iy + row_h) {
                                        clicked_idx = i;
                                        break;
                                    }
                                }
                            }

                            if (clicked_idx != -1) {
                                fs::VFSNode& it = clicked->finder_items[clicked_idx];
                                bool is_double_click = (last_finder_clicked_item_idx == clicked_idx && (frame_counter - last_finder_click_time) < 30);
                                last_finder_click_time = frame_counter;
                                last_finder_clicked_item_idx = clicked_idx;
                                
                                if (is_double_click) {
                                    if (it.type == fs::NodeType::DIRECTORY) {
                                        int len = 0;
                                        while (clicked->text_input[len]) len++;
                                        if (len > 0 && clicked->text_input[len - 1] != '/') {
                                            clicked->text_input[len++] = '/';
                                        }
                                        int name_idx = 0;
                                        while (it.name[name_idx] && len < 250) {
                                            clicked->text_input[len++] = it.name[name_idx++];
                                        }
                                        clicked->text_input[len] = '\0';
                                        clicked->text_len = len;
                                        clicked->selected_item_idx = -1;
                                        clicked->finder_needs_reload = true;
                                    } else {
                                        char full_path[256];
                                        int fp_idx = 0;
                                        const char* active_dir = clicked->text_input;
                                        if (clicked->text_len == 0) active_dir = "Desktop";
                                        
                                        if (active_dir[0] == '/') {
                                            str_copy(full_path, active_dir, sizeof(full_path));
                                            int len = 0;
                                            while (full_path[len]) len++;
                                            if (len > 0 && full_path[len - 1] != '/') {
                                                full_path[len++] = '/';
                                            }
                                            str_copy(full_path + len, it.name, sizeof(full_path) - len);
                                        } else {
                                            if (str_equal(it.name, "hello.txt")) {
                                                str_copy(full_path, "/tar/hello.txt", sizeof(full_path));
                                            } else if (str_equal(it.name, "info.txt")) {
                                                str_copy(full_path, "/tar/docs/info.txt", sizeof(full_path));
                                            } else {
                                                full_path[0] = '/';
                                                str_copy(full_path + 1, it.name, sizeof(full_path) - 1);
                                            }
                                        }

                                        char edit_title[128] = "Code Editor - ";
                                        int t_idx = 14;
                                        int f_len = 0;
                                        while (full_path[f_len]) {
                                            edit_title[t_idx++] = full_path[f_len++];
                                        }
                                        edit_title[t_idx] = '\0';

                                        bool is_term = str_equal(it.name, "Terminal") || str_equal(it.name, "Terminal.app");
                                        if (is_term) {
                                            create_window((width - 500)/2, (height - 350)/2, 500, 350, "Terminal", C_TERMINAL);
                                        } else if (str_equal(it.name, "System Settings")) {
                                            create_window((width - 500)/2, (height - 420)/2, 500, 420, "System Settings", 0x00FFFFFF);
                                        } else if (ends_with(it.name, ".bmp")) {
                                            char pic_title[256] = "Picture Viewer - ";
                                            str_copy(pic_title + 17, full_path, 230);
                                            create_window((width - 450)/2, (height - 380)/2, 450, 380, pic_title, 0x00FFFFFF);
                                        } else if (ends_with(it.name, ".sng")) {
                                            char audio_title[256] = "Audio Player - ";
                                            str_copy(audio_title + 15, full_path, 230);
                                            create_window((width - 400)/2, (height - 300)/2, 400, 300, audio_title, 0x00FFFFFF);
                                            load_sng_file(full_path);
                                        } else if (ends_with(it.name, ".wav")) {
                                            char audio_title[256] = "Audio Player - ";
                                            str_copy(audio_title + 15, full_path, 230);
                                            create_window((width - 400)/2, (height - 300)/2, 400, 300, audio_title, 0x00FFFFFF);
                                            load_wav_file(full_path);
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
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    clicked->selected_item_idx = clicked_idx;
                                    is_renaming_item = false;
                                }
                            } else {
                                clicked->selected_item_idx = -1;
                                is_renaming_item = false;
                            }
                            force_redraw_all();
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
                    } else {
                        int client_w = clicked->rect.w - 2;
                        int client_h = clicked->rect.h - TITLE_BAR_HEIGHT - 12;
                        if (rx >= 0 && rx < client_w && ry >= 0 && ry < client_h) {
                            clicked->pending_event.type = 1; // EVENT_MOUSE_CLICK
                            clicked->pending_event.mx = rx;
                            clicked->pending_event.my = ry;
                        }
                    }
                    }

                    force_redraw_all();
                }
                state_updated = true;
            } else if (start_menu_active && new_x >= 0 && new_x <= 380 && new_y >= height - TASKBAR_HEIGHT - 450 && new_y < height - TASKBAR_HEIGHT) {
                // Click inside Start Menu
                int y_start = height - TASKBAR_HEIGHT - 450;
                if (new_y >= y_start + 50 && new_y < y_start + 405) {
                    if (new_x < 220) {
                        // Left column: programs
                        int item_idx = (new_y - (y_start + 50)) / 45;
                        if (item_idx == 0) {
                            // Internet Explorer (Safari)
                            bool open = false;
                            for (Window* w = window_list_head; w; w = w->next) {
                                if (w->title_is("Safari")) { open = true; w->is_minimized = false; focus_window(w); break; }
                            }
                            if (!open) create_window((width - 640)/2, (height - 480)/2, 640, 480, "Safari", C_FINDER);
                        } else if (item_idx == 1) {
                            // Email (Mail)
                            bool open = false;
                            for (Window* w = window_list_head; w; w = w->next) {
                                if (w->title_is("Mail")) { open = true; w->is_minimized = false; focus_window(w); break; }
                            }
                            if (!open) create_window((width - 640)/2, (height - 480)/2, 640, 480, "Mail", C_FINDER);
                        } else if (item_idx == 2) {
                            // Code Editor
                            create_window((width - 650)/2, (height - 450)/2, 650, 450, "Code Editor - /tar/hello.txt", 0x001E1E1E);
                        } else if (item_idx == 3) {
                            // Terminal
                            bool open = false;
                            for (Window* w = window_list_head; w; w = w->next) {
                                if (w->title_is("Terminal")) { open = true; w->is_minimized = false; focus_window(w); break; }
                            }
                            if (!open) create_window((width - 500)/2, (height - 350)/2, 500, 350, "Terminal", C_TERMINAL);
                        } else if (item_idx == 4) {
                            // Notes
                            bool open = false;
                            for (Window* w = window_list_head; w; w = w->next) {
                                if (w->title_is("Notes")) { open = true; w->is_minimized = false; focus_window(w); break; }
                            }
                            if (!open) create_window((width - 500)/2, (height - 350)/2, 500, 350, "Notes", 0x00FFFCEB);
                        } else if (item_idx == 5) {
                            // App Store
                            bool open = false;
                            for (Window* w = window_list_head; w; w = w->next) {
                                if (w->title_is("App Store")) { open = true; w->is_minimized = false; focus_window(w); break; }
                            }
                            if (!open) create_window((width - 640)/2, (height - 480)/2, 640, 480, "App Store", 0x001B72E8);
                        }
                    } else {
                        // Right column: shortcuts
                        int ry_rel = new_y - y_start;
                        if (ry_rel >= 55 && ry_rel < 90) {
                            // My Documents
                            Window* win = create_window((width - 500)/2, (height - 350)/2, 500, 350, "Finder", C_FINDER);
                            if (win) {
                                win->text_len = 12;
                                memcpy(win->text_input, "My Documents", 13);
                            }
                        } else if (ry_rel >= 180 && ry_rel < 215) {
                            // My Computer
                            Window* win = create_window((width - 500)/2, (height - 350)/2, 500, 350, "Finder", C_FINDER);
                            if (win) {
                                win->text_len = 11;
                                memcpy(win->text_input, "My Computer", 12);
                            }
                        } else if (ry_rel >= 235 && ry_rel < 270) {
                            // Control Panel (System Settings)
                            bool open = false;
                            for (Window* w = window_list_head; w; w = w->next) {
                                if (w->title_is("System Settings")) { open = true; w->is_minimized = false; focus_window(w); break; }
                            }
                            if (!open) create_window((width - 500)/2, (height - 420)/2, 500, 420, "System Settings", 0x00FFFFFF);
                        }
                    }
                } else if (new_y >= y_start + 405) {
                    // Footer: Log Off or Shut Down
                    if (new_x >= 180 && new_x < 270) {
                        drivers::Serial::println("[SYSTEM] Log off triggered.");
                    } else if (new_x >= 270) {
                        drivers::Serial::println("[SYSTEM] Shutdown triggered.");
                    }
                }
                start_menu_active = false;
                force_redraw_all();
                state_updated = true;
            } else if (new_y >= height - TASKBAR_HEIGHT) {
                // Dock Click
                int dx = (width - DOCK_WIDTH) / 2;
                int dy = height - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM;
                if (new_x >= dx && new_x < dx + DOCK_WIDTH && new_y >= dy && new_y < dy + DOCK_HEIGHT) {
                    int gap = (int)(16 * ui_scale);
                    int icon_size = DOCK_ICON_SIZE;
                    int total_icons_w = 5 * icon_size + 4 * gap;
                    int start_x = dx + (DOCK_WIDTH - total_icons_w) / 2;

                    int clicked_idx = -1;
                    for (int i = 0; i < 5; i++) {
                        int ix = start_x + i * (icon_size + gap);
                        if (new_x >= ix && new_x < ix + icon_size) {
                            clicked_idx = i;
                            break;
                        }
                    }

                    if (clicked_idx == 0) {
                        focus_or_create_app("Finder", C_FINDER, 500, 350);
                    } else if (clicked_idx == 1) {
                        focus_or_create_app("Safari", 0x00FFFFFF, 800, 550);
                    } else if (clicked_idx == 2) {
                        focus_or_create_app("Terminal", C_TERMINAL, 600, 400);
                    } else if (clicked_idx == 3) {
                        focus_or_create_app("Notes", 0x00FFFDD0, 450, 350);
                    } else if (clicked_idx == 4) {
                        focus_or_create_app("System Settings", 0x00FFFFFF, 500, 420);
                    }
                }
                force_redraw_all();
                state_updated = true;
            } else {
                if (start_menu_active) {
                    start_menu_active = false;
                    force_redraw_all();
                }
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

                                        if (ends_with(it.name, ".bmp")) {
                                            char pic_title[256] = "Picture Viewer - ";
                                            str_copy(pic_title + 17, full_path, 230);
                                            create_window((width - 450)/2, (height - 380)/2, 450, 380, pic_title, 0x00FFFFFF);
                                        } else if (ends_with(it.name, ".sng")) {
                                            char audio_title[256] = "Audio Player - ";
                                            str_copy(audio_title + 15, full_path, 230);
                                            create_window((width - 400)/2, (height - 300)/2, 400, 300, audio_title, 0x00FFFFFF);
                                            load_sng_file(full_path);
                                        } else if (ends_with(it.name, ".wav")) {
                                            char audio_title[256] = "Audio Player - ";
                                            str_copy(audio_title + 15, full_path, 230);
                                            create_window((width - 400)/2, (height - 300)/2, 400, 300, audio_title, 0x00FFFFFF);
                                            load_wav_file(full_path);
                                        } else {
                                            char edit_title[128] = "Code Editor - /";
                                            int t_idx = 15;
                                            int n_idx = 0;
                                            while (it.name[n_idx]) { edit_title[t_idx++] = it.name[n_idx++]; }
                                            edit_title[t_idx] = '\0';
                                            
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
                                                    }
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

                int shadow = 32;
                Rect old_damage = expanded_rect(old_rect, shadow);
                Rect new_damage = expanded_rect(active_window->rect, shadow);
                dirty.add(old_damage);
                dirty.add(new_damage);
                needs_redraw = true;
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
                
                dirty.add({active_window->rect.x - 32, active_window->rect.y - 32, max_w + 64, max_h + 64});
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
                
                dirty.add({x_min, y_min, x_max - x_min, y_max - y_min});
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
            if (active_window->buffer) {
                delete[] active_window->buffer;
                active_window->buffer = nullptr;
            }
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
        int old_menu_hover = active_menu.hovered_item;
        int old_sub_hover = active_submenu.hovered_item;
        bool old_sub_active = active_submenu.active;

        MenuItem main_items[16];
        int main_count = populate_menu_items(active_menu.type, main_items);

        bool over_submenu = false;
        if (active_submenu.active && in_rect(new_x, new_y, active_submenu.x, active_submenu.y, active_submenu.w, active_submenu.h)) {
            over_submenu = true;
            MenuItem sub_items[16];
            int sub_count = populate_menu_items(active_submenu.type, sub_items);
            int idx = -1;
            for (int i = 0; i < sub_count; i++) {
                int iy = active_submenu.y + MENU_PADDING + i * MENU_ITEM_HEIGHT;
                if (in_rect(new_x, new_y, active_submenu.x, iy, active_submenu.w, 24)) {
                    if (sub_items[i].enabled) {
                        idx = i;
                    }
                    break;
                }
            }
            active_submenu.hovered_item = idx;
        } else {
            if (active_submenu.active) {
                if (!in_rect(new_x, new_y, active_menu.x, active_menu.y, active_menu.w, active_menu.h)) {
                    active_submenu.active = false;
                    dirty.add({active_submenu.x - 4, active_submenu.y - 4, active_submenu.w + 8, active_submenu.h + 8});
                    needs_redraw = true;
                }
            }
        }

        if (!over_submenu) {
            if (in_rect(new_x, new_y, active_menu.x, active_menu.y, active_menu.w, active_menu.h)) {
                int idx = -1;
                for (int i = 0; i < main_count; i++) {
                    int iy = active_menu.y + MENU_PADDING + i * MENU_ITEM_HEIGHT;
                    if (in_rect(new_x, new_y, active_menu.x, iy, active_menu.w, 24)) {
                        if (main_items[i].enabled) {
                            idx = i;
                        }
                        break;
                    }
                }
                active_menu.hovered_item = idx;

                if (idx != -1 && main_items[idx].submenu_type != -1) {
                    int sub_type = main_items[idx].submenu_type;
                    MenuItem sub_items[16];
                    int sub_count = populate_menu_items(sub_type, sub_items);
                    
                    int sub_x = active_menu.x + active_menu.w - 4;
                    int sub_y = active_menu.y + MENU_PADDING + idx * MENU_ITEM_HEIGHT;
                    int sub_w = MENU_WIDTH;
                    int sub_h = sub_count * MENU_ITEM_HEIGHT + 2 * MENU_PADDING;

                    if (sub_x + sub_w > (int)width) {
                        sub_x = active_menu.x - sub_w + 4;
                    }

                    if (!active_submenu.active || active_submenu.type != sub_type || active_submenu.y != sub_y) {
                        if (active_submenu.active) {
                            dirty.add({active_submenu.x - 4, active_submenu.y - 4, active_submenu.w + 8, active_submenu.h + 8});
                        }
                        active_submenu.active = true;
                        active_submenu.type = sub_type;
                        active_submenu.x = sub_x;
                        active_submenu.y = sub_y;
                        active_submenu.w = sub_w;
                        active_submenu.h = sub_h;
                        active_submenu.hovered_item = -1;
                        
                        dirty.add({active_submenu.x - 4, active_submenu.y - 4, active_submenu.w + 8, active_submenu.h + 8});
                        needs_redraw = true;
                    }
                } else if (idx != -1 && main_items[idx].submenu_type == -1 && active_submenu.active) {
                    active_submenu.active = false;
                    dirty.add({active_submenu.x - 4, active_submenu.y - 4, active_submenu.w + 8, active_submenu.h + 8});
                    needs_redraw = true;
                }
            } else {
                active_menu.hovered_item = -1;
            }
        }

        if (active_menu.hovered_item != old_menu_hover) {
            dirty.add({active_menu.x - 4, active_menu.y - 4, active_menu.w + 8, active_menu.h + 8});
            needs_redraw = true;
        }
        if (active_submenu.hovered_item != old_sub_hover || active_submenu.active != old_sub_active) {
            if (active_submenu.active) {
                dirty.add({active_submenu.x - 4, active_submenu.y - 4, active_submenu.w + 8, active_submenu.h + 8});
            }
            needs_redraw = true;
        }
    }

    // 5.5 Dock Hover Tracking
    if (!state_updated) {
        int dx = (width - DOCK_WIDTH) / 2;
        int dy = height - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM;
        
        // Calculate dock hover state for old mouse position
        int old_hover_idx = -1;
        if (old_x >= dx && old_x < dx + DOCK_WIDTH && old_y >= dy && old_y < dy + DOCK_HEIGHT) {
            int gap = (int)(16 * ui_scale);
            int icon_size = DOCK_ICON_SIZE;
            int total_icons_w = 5 * icon_size + 4 * gap;
            int start_x = dx + (DOCK_WIDTH - total_icons_w) / 2;
            for (int i = 0; i < 5; i++) {
                int ix = start_x + i * (icon_size + gap);
                if (old_x >= ix && old_x < ix + icon_size) {
                    old_hover_idx = i;
                    break;
                }
            }
        }
        
        // Calculate dock hover state for new mouse position
        int new_hover_idx = -1;
        if (new_x >= dx && new_x < dx + DOCK_WIDTH && new_y >= dy && new_y < dy + DOCK_HEIGHT) {
            int gap = (int)(16 * ui_scale);
            int icon_size = DOCK_ICON_SIZE;
            int total_icons_w = 5 * icon_size + 4 * gap;
            int start_x = dx + (DOCK_WIDTH - total_icons_w) / 2;
            for (int i = 0; i < 5; i++) {
                int ix = start_x + i * (icon_size + gap);
                if (new_x >= ix && new_x < ix + icon_size) {
                    new_hover_idx = i;
                    break;
                }
            }
        }
        
        if (old_hover_idx != new_hover_idx) {
            // The hovered icon changed! Redraw the Dock region.
            // Bounding box of Dock (incorporating tooltip padding and scale-up margin)
            int d_dirty_x = dx - 20;
            int d_dirty_y = dy - 40;
            int d_dirty_w = DOCK_WIDTH + 40;
            int d_dirty_h = DOCK_HEIGHT + DOCK_MARGIN_BOTTOM + 40;
            dirty.add({d_dirty_x, d_dirty_y, d_dirty_w, d_dirty_h});
            needs_redraw = true;
        }
    }

    // 5.6 Window Traffic Lights Hover Tracking
    if (!state_updated) {
        for (Window* w = window_list_head; w; w = w->next) {
            if (w->is_minimized) continue;
            int wx = w->rect.x;
            int wy = w->rect.y;
            
            int x_min = wx + 10;
            int x_max = wx + 62;
            int y_min = wy + 9;
            int y_max = wy + 21;
            
            bool old_inside = (old_x >= x_min && old_x <= x_max && old_y >= y_min && old_y <= y_max);
            bool new_inside = (new_x >= x_min && new_x <= x_max && new_y >= y_min && new_y <= y_max);
            
            if (old_inside != new_inside) {
                // Hover state of traffic lights changed for this window
                dirty.add({wx + 8, wy + 5, 58, 20});
                needs_redraw = true;
            }
        }
    }

    // 6. Queue all calculated dirty regions to the global pending_dirty list for consolidated redraw.
    if (!state_updated) {
        if (position_changed) {
            int cursor_size = (ui_scale >= 1.5f) ? cursor_2x_size : cursor_1x_size;
            int cursor_offset = (int)(ui_scale >= 1.5f ? 3 : 1);
            Rect old_cursor = {old_x - cursor_offset - 3, old_y - cursor_offset - 3, cursor_size + 6, cursor_size + 6};
            Rect new_cursor = {new_x - cursor_offset - 3, new_y - cursor_offset - 3, cursor_size + 6, cursor_size + 6};
            dirty.add(old_cursor);
            dirty.add(new_cursor);
            needs_redraw = true;
        }
    }

    if (needs_redraw || dirty.count > 0) {
        for (int i = 0; i < dirty.count; ++i) {
            enqueue_pending_dirty(dirty.rects[i]);
        }
    }

    mouse_pressed = left_pressed;
    right_mouse_pressed = right_pressed;
    last_mouse_x = mouse_x;
    last_mouse_y = mouse_y;
}

void WindowManager::tick() {
    audio_tick();
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

bool WindowManager::is_drag_in_progress() {
    return (active_window && (active_window->is_dragging || is_resizing_window)) || is_dragging_selection;
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
            if (sel >= 0 && sel < active_window->finder_item_count) {
                fs::VFSNode& it = active_window->finder_items[sel];
                int len = 0;
                while (it.name[len]) len++;
                
                if (c == '\b') {
                    if (len > 0) {
                        it.name[len - 1] = '\0';
                    }
                } else if (c == '\n') {
                    is_renaming_item = false;
                    const char* active_dir = active_window->text_input;
                    if (active_window->text_len == 0) active_dir = "Desktop";
                    rename_item(active_dir, rename_original_name, it.name);
                    active_window->finder_needs_reload = true;
                } else if (c >= 32 && c <= 126 && len < 28) {
                    it.name[len] = c;
                    it.name[len + 1] = '\0';
                }
            }
            Rect dirty = {
                active_window->rect.x - 4, active_window->rect.y - 4,
                active_window->rect.w + 8, active_window->rect.h + 8
            };
            redraw_dirty_rect(dirty);
        } else if (active_window->title_is("Finder") && active_window->finder_editing_path) {
            if (c == '\b') {
                if (active_window->text_len > 0) {
                    active_window->text_len--;
                    active_window->text_input[active_window->text_len] = '\0';
                }
            } else if (c == '\n') {
                active_window->finder_editing_path = false;
                active_window->selected_item_idx = -1;
                active_window->finder_needs_reload = true;
            } else if (c >= 32 && c <= 126 && active_window->text_len < 250) {
                active_window->text_input[active_window->text_len++] = c;
                active_window->text_input[active_window->text_len] = '\0';
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
    MENU_BAR_HEIGHT   = (int)(28 * scale);
    TITLE_BAR_HEIGHT  = (int)(30 * scale);
    DOCK_HEIGHT       = (int)(64 * scale);
    DOCK_ICON_SIZE    = (int)(48 * scale);
    DOCK_ICON_SPACING = (int)(36 * scale);
    DOCK_MARGIN_BOTTOM = (int)(24 * scale);
    DOCK_WIDTH        = (int)(380 * scale);
    TASKBAR_HEIGHT    = DOCK_HEIGHT + DOCK_MARGIN_BOTTOM;
    
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
        if (w->rect.y < 0) w->rect.y = 0;
        if (w->rect.y + w->rect.h > height - TASKBAR_HEIGHT) w->rect.y = height - TASKBAR_HEIGHT - w->rect.h;
    }
    force_redraw_all();
}

void WindowManager::set_theme(int theme_id) {
    current_theme = theme_id;
    dark_mode = (theme_id == 1 || theme_id == 2); // Dark and Nord are dark backgrounds

    if (theme_id == 1) { // Dark
        C_WHITE      = 0x001E293B; // Slate 800
        C_BLACK      = 0x00FFFFFF; // White
        C_TEXT       = 0x00F1F5F9; // Slate 100
        C_TEXT_MUTED = 0x0094A3B8; // Slate 400
        C_MENU_BG    = 0x000F172A; // Slate 900
        C_MENU_LINE  = 0x00334155; // Slate 700
        C_DOCK_BG    = 0x001E293B; // Slate 800
        C_DOCK_RIM   = 0x00475569; // Slate 600
        C_BORDER     = 0x00374151; // Slate 700
        C_TITLE_BG   = 0x001F2937; // Slate 800
        C_DIVIDER    = 0x002D3748; // Slate 600
    } else if (theme_id == 2) { // Nord
        C_WHITE      = 0x002E3440; // Nord 0
        C_BLACK      = 0x00ECEFF4; // Nord 6
        C_TEXT       = 0x00ECEFF4; // Nord 6
        C_TEXT_MUTED = 0x00D8DEE9; // Nord 4
        C_MENU_BG    = 0x00242933; // Darker Nord
        C_MENU_LINE  = 0x003B4252; // Nord 1
        C_DOCK_BG    = 0x002E3440; // Nord 0
        C_DOCK_RIM   = 0x004C566A; // Nord 3
        C_BORDER     = 0x003B4252; // Nord 1
        C_TITLE_BG   = 0x002E3440; // Nord 0
        C_DIVIDER    = 0x00434C5E; // Nord 2
    } else if (theme_id == 3) { // Warm / Solarized
        C_WHITE      = 0x00FDF6E3; // Cream
        C_BLACK      = 0x00073642; // Dark brown
        C_TEXT       = 0x00586E75; // Warm gray
        C_TEXT_MUTED = 0x0093A1A1;
        C_MENU_BG    = 0x00EEE8D5;
        C_MENU_LINE  = 0x0093A1A1;
        C_DOCK_BG    = 0x00EEE8D5;
        C_DOCK_RIM   = 0x0093A1A1;
        C_BORDER     = 0x00D3C6AA; // Soft border
        C_TITLE_BG   = 0x00F6F0DD; // Warm title
        C_DIVIDER    = 0x00DFD7C3;
    } else { // Light
        C_WHITE      = 0x00FFFFFF;
        C_BLACK      = 0x00000000;
        C_TEXT       = 0x00333333;
        C_TEXT_MUTED = 0x00666666;
        C_MENU_BG    = 0x00F5F5F5;
        C_MENU_LINE  = 0x00D2D2D2;
        C_DOCK_BG    = 0x00EBEBEB;
        C_DOCK_RIM   = 0x00FFFFFF;
        C_BORDER     = 0x00D1D5DB;
        C_TITLE_BG   = 0x00F3F4F6;
        C_DIVIDER    = 0x00E5E7EB;
    }

    // Dynamic background update for active mock windows
    for (Window* w = window_list_head; w; w = w->next) {
        if (!w->buffer) {
            uint32_t bg_normal = 0x00FFFFFF;
            uint32_t bg_terminal = 0x001B2E3C;
            if (theme_id == 1) { // Dark
                bg_normal = 0x001E293B;
                bg_terminal = 0x000F172A;
            } else if (theme_id == 2) { // Nord
                bg_normal = 0x002E3440;
                bg_terminal = 0x00242933;
            } else if (theme_id == 3) { // Warm
                bg_normal = 0x00FDF6E3;
                bg_terminal = 0x00EEE8D5;
            }

            if (w->title_is("Finder") || w->title_is("Safari") || w->title_is("Mail") || w->title_is("System Settings")) {
                w->bg_color = bg_normal;
            } else if (w->title_is("Performance Monitor") || w->title_is("System Log")) {
                w->bg_color = bg_terminal;
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
    
    const char* theme_labels[] = {"Light", "Dark", "Nord", "Warm"};
    for (int i = 0; i < 4; ++i) {
        int bx = x + scale_dim(20 + i * 90);
        int by = y + scale_dim(150);
        int bw = scale_dim(80);
        int bh = scale_dim(26);
        bool is_active = (current_theme == i);
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

void WindowManager::audio_play_frequency(uint32_t freq) {
    if (freq == 0) {
        uint8_t tmp = drivers::inb(0x61) & 0xFC;
        drivers::outb(0x61, tmp);
    } else {
        uint32_t div = 1193180 / freq;
        drivers::outb(0x43, 0xB6);
        drivers::outb(0x42, (uint8_t)(div & 0xFF));
        drivers::outb(0x42, (uint8_t)((div >> 8) & 0xFF));
        
        uint8_t tmp = drivers::inb(0x61);
        if ((tmp & 3) != 3) {
            drivers::outb(0x61, tmp | 3);
        }
    }
}

void WindowManager::audio_stop() {
    audio_playing = false;
    audio_play_frequency(0);
}

void WindowManager::audio_tick() {
    if (!audio_playing) return;
    
    if ((uint32_t)frame_counter >= audio_note_end_frame) {
        audio_note_idx++;
        if (audio_note_idx >= audio_note_count) {
            audio_stop();
        } else {
            audio_play_frequency(audio_notes[audio_note_idx].freq);
            audio_note_end_frame = frame_counter + audio_notes[audio_note_idx].duration;
        }
    }
}

void WindowManager::load_sng_file(const char* path) {
    audio_stop();
    audio_note_idx = 0;
    audio_note_count = 0;
    
    // Extract base name for song details
    int last_s = -1;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') last_s = i;
    }
    const char* base = (last_s != -1) ? path + last_s + 1 : path;
    str_copy(audio_current_song, base, sizeof(audio_current_song));

    fs::VFSNode* node = fs::VFS::open(path);
    if (!node) return;
    
    char file_buf[4096];
    fs::File f;
    f.node = node;
    f.offset = 0;
    ssize_t bytes = fs::VFS::read(&f, file_buf, sizeof(file_buf) - 1);
    if (bytes <= 0) return;
    file_buf[bytes] = '\0';

    int idx = 0;
    int len = bytes;
    while (idx < len && audio_note_count < 256) {
        if (file_buf[idx] == '#') {
            while (idx < len && file_buf[idx] != '\n') idx++;
            continue;
        }
        if (file_buf[idx] == ' ' || file_buf[idx] == '\n' || file_buf[idx] == '\r' || file_buf[idx] == '\t') {
            idx++;
            continue;
        }

        uint32_t freq = 0;
        while (idx < len && file_buf[idx] >= '0' && file_buf[idx] <= '9') {
            freq = freq * 10 + (file_buf[idx] - '0');
            idx++;
        }
        
        while (idx < len && (file_buf[idx] == ' ' || file_buf[idx] == '\t')) idx++;

        uint32_t dur = 0;
        while (idx < len && file_buf[idx] >= '0' && file_buf[idx] <= '9') {
            dur = dur * 10 + (file_buf[idx] - '0');
            idx++;
        }

        if (dur > 0) {
            audio_notes[audio_note_count].freq = freq;
            audio_notes[audio_note_count].duration = dur;
            audio_note_count++;
        }
    }
}

void WindowManager::fill_wav_buffer_slice(int buffer_idx) {
    if (!audio_is_wav || !audio_wav_data) return;
    
    static int16_t temp_buf[16384];
    
    // Clear temp buffer
    for (int i = 0; i < 16384; ++i) temp_buf[i] = 0;
    
    int src_bytes_per_sample = audio_wav_bits_per_sample / 8;
    int src_frame_size = audio_wav_channels * src_bytes_per_sample;
    uint32_t total_src_frames = audio_wav_size / src_frame_size;
    
    uint32_t step = (audio_wav_sample_rate << 16) / 48000;
    if (step == 0) step = 1;
    
    uint64_t total_amp = 0;
    bool finished = false;
    
    for (int i = 0; i < 8192; ++i) {
        uint32_t src_frame_idx = audio_wav_phase >> 16;
        uint16_t frac = (uint16_t)(audio_wav_phase & 0xFFFF);
        
        if (src_frame_idx >= total_src_frames) {
            finished = true;
            break;
        }
        
        uint32_t next_frame_idx = src_frame_idx + 1;
        if (next_frame_idx >= total_src_frames) next_frame_idx = total_src_frames - 1;
        
        int16_t raw_l = 0, raw_r = 0;
        int16_t next_l = 0, next_r = 0;
        
        uint8_t* base_ptr = audio_wav_data + audio_wav_offset;
        
        if (audio_wav_bits_per_sample == 16) {
            raw_l = *(int16_t*)(base_ptr + src_frame_idx * src_frame_size);
            if (audio_wav_channels == 2) {
                raw_r = *(int16_t*)(base_ptr + src_frame_idx * src_frame_size + 2);
            } else {
                raw_r = raw_l;
            }
            
            next_l = *(int16_t*)(base_ptr + next_frame_idx * src_frame_size);
            if (audio_wav_channels == 2) {
                next_r = *(int16_t*)(base_ptr + next_frame_idx * src_frame_size + 2);
            } else {
                next_r = next_l;
            }
        } else { // 8-bit unsigned
            uint8_t u_l = base_ptr[src_frame_idx * src_frame_size];
            raw_l = (int16_t)(((int32_t)u_l - 128) << 8);
            if (audio_wav_channels == 2) {
                uint8_t u_r = base_ptr[src_frame_idx * src_frame_size + 1];
                raw_r = (int16_t)(((int32_t)u_r - 128) << 8);
            } else {
                raw_r = raw_l;
            }
            
            uint8_t nu_l = base_ptr[next_frame_idx * src_frame_size];
            next_l = (int16_t)(((int32_t)nu_l - 128) << 8);
            if (audio_wav_channels == 2) {
                uint8_t nu_r = base_ptr[next_frame_idx * src_frame_size + 1];
                next_r = (int16_t)(((int32_t)nu_r - 128) << 8);
            } else {
                next_r = next_l;
            }
        }
        
        // Linear interpolation
        int32_t sample_l = ((int32_t)raw_l * (65536 - frac) + (int32_t)next_l * frac) >> 16;
        int32_t sample_r = ((int32_t)raw_r * (65536 - frac) + (int32_t)next_r * frac) >> 16;
        
        temp_buf[2 * i] = (int16_t)sample_l;
        temp_buf[2 * i + 1] = (int16_t)sample_r;
        
        int32_t abs_l = sample_l >= 0 ? sample_l : -sample_l;
        int32_t abs_r = sample_r >= 0 ? sample_r : -sample_r;
        total_amp += (abs_l + abs_r);
        
        audio_wav_phase += step;
    }
    
    drivers::AC97::write_buffer(buffer_idx, temp_buf, 16384);
    
    if (finished) {
        audio_stop();
    } else {
        audio_amplitude = (uint32_t)(total_amp / 16384);
    }
}

void WindowManager::load_wav_file(const char* path) {
    audio_stop();
    audio_is_wav = true;
    audio_wav_offset = 0;
    audio_wav_size = 0;
    audio_amplitude = 0;
    audio_wav_phase = 0;
    next_buffer_to_fill = 0;
    
    // Extract base name for song details
    int last_s = -1;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') last_s = i;
    }
    const char* base = (last_s != -1) ? path + last_s + 1 : path;
    str_copy(audio_current_song, base, sizeof(audio_current_song));
    
    fs::VFSNode* node = fs::VFS::open(path);
    if (!node) return;
    
    size_t file_size = node->size;
    if (file_size < 44) return;
    
    if (audio_wav_data) {
        kernel::kfree(audio_wav_data);
        audio_wav_data = nullptr;
    }
    
    audio_wav_data = (uint8_t*)kernel::kmalloc(file_size);
    if (!audio_wav_data) return;
    
    fs::File f;
    f.node = node;
    f.offset = 0;
    ssize_t read_bytes = fs::VFS::read(&f, audio_wav_data, file_size);
    if (read_bytes < 44) {
        kernel::kfree(audio_wav_data);
        audio_wav_data = nullptr;
        return;
    }
    
    // Check "RIFF" and "WAVE"
    if (audio_wav_data[0] != 'R' || audio_wav_data[1] != 'I' || audio_wav_data[2] != 'F' || audio_wav_data[3] != 'F' ||
        audio_wav_data[8] != 'W' || audio_wav_data[9] != 'A' || audio_wav_data[10] != 'V' || audio_wav_data[11] != 'E') {
        kernel::kfree(audio_wav_data);
        audio_wav_data = nullptr;
        return;
    }
    
    // Find "fmt " chunk
    uint32_t fmt_offset = 12;
    bool found_fmt = false;
    while (fmt_offset + 8 < file_size) {
        if (audio_wav_data[fmt_offset] == 'f' && audio_wav_data[fmt_offset+1] == 'm' &&
            audio_wav_data[fmt_offset+2] == 't' && audio_wav_data[fmt_offset+3] == ' ') {
            found_fmt = true;
            break;
        }
        uint32_t chunk_len = *(uint32_t*)&audio_wav_data[fmt_offset + 4];
        fmt_offset += 8 + chunk_len;
    }
    
    if (!found_fmt) {
        kernel::kfree(audio_wav_data);
        audio_wav_data = nullptr;
        return;
    }
    
    uint16_t audio_format = *(uint16_t*)&audio_wav_data[fmt_offset + 8];
    if (audio_format != 1) { // Not PCM
        kernel::kfree(audio_wav_data);
        audio_wav_data = nullptr;
        return;
    }
    
    audio_wav_channels = *(uint16_t*)&audio_wav_data[fmt_offset + 10];
    audio_wav_sample_rate = *(uint32_t*)&audio_wav_data[fmt_offset + 12];
    audio_wav_bits_per_sample = *(uint16_t*)&audio_wav_data[fmt_offset + 22];
    
    // Find "data" chunk
    uint32_t data_offset = 12;
    bool found_data = false;
    while (data_offset + 8 < file_size) {
        if (audio_wav_data[data_offset] == 'd' && audio_wav_data[data_offset+1] == 'a' &&
            audio_wav_data[data_offset+2] == 't' && audio_wav_data[data_offset+3] == 'a') {
            found_data = true;
            break;
        }
        uint32_t chunk_len = *(uint32_t*)&audio_wav_data[data_offset + 4];
        data_offset += 8 + chunk_len;
    }
    
    if (!found_data) {
        kernel::kfree(audio_wav_data);
        audio_wav_data = nullptr;
        return;
    }
    
    audio_wav_size = *(uint32_t*)&audio_wav_data[data_offset + 4];
    audio_wav_offset = data_offset + 8;
    
    if (audio_wav_offset + audio_wav_size > file_size) {
        audio_wav_size = file_size - audio_wav_offset;
    }
    
    // Initial fill of circular buffers
    fill_wav_buffer_slice(0);
    fill_wav_buffer_slice(1);
    next_buffer_to_fill = 0;
}

void WindowManager::draw_audio_player_content(Window* win) {
    int x = win->rect.x;
    int y = win->rect.y;
    int w = win->rect.w - 2;
    int h = win->rect.h - TITLE_BAR_HEIGHT - 12;

    // Translucent dark card style background
    uint32_t card_bg = dark_mode ? 0x000F172A : 0x00F8FAFC;
    uint32_t text_color = dark_mode ? 0x00FFFFFF : 0x000F172A;
    
    drivers::Framebuffer::draw_rounded_rect_alpha(x + 10, y + TITLE_BAR_HEIGHT + 10, w - 20, h - 20, 10, card_bg, 255);

    // Draw details
    draw_string("Crecent Audio Player", x + 30, y + TITLE_BAR_HEIGHT + 25, 0x000F52BA, 16.0f);
    
    char track_msg[128] = "Track: ";
    if (audio_current_song[0] != '\0') {
        str_copy(track_msg + 7, audio_current_song, 100);
    } else {
        str_copy(track_msg + 7, "None Loaded", 100);
    }
    draw_string(track_msg, x + 30, y + TITLE_BAR_HEIGHT + 48, text_color, 14.0f);

    char info_msg[128] = "";
    if (audio_is_wav && audio_wav_data) {
        char srate_str[16];
        char bits_str[16];
        int_to_str(audio_wav_sample_rate, srate_str, sizeof(srate_str));
        int_to_str(audio_wav_bits_per_sample, bits_str, sizeof(bits_str));
        
        str_copy(info_msg, "Format: WAV PCM, ", sizeof(info_msg));
        str_copy(info_msg + 17, srate_str, 10);
        int len_cur = 17 + str_len(srate_str);
        str_copy(info_msg + len_cur, " Hz, ", 10);
        len_cur += 5;
        if (audio_wav_channels == 2) {
            str_copy(info_msg + len_cur, "Stereo, ", 10);
            len_cur += 8;
        } else {
            str_copy(info_msg + len_cur, "Mono, ", 10);
            len_cur += 6;
        }
        str_copy(info_msg + len_cur, bits_str, 10);
        len_cur += str_len(bits_str);
        str_copy(info_msg + len_cur, "-bit", 10);
    } else if (audio_note_count > 0) {
        str_copy(info_msg, "Format: SNG (PC Speaker Pitch Sheet)", sizeof(info_msg));
    }
    
    if (info_msg[0] != '\0') {
        draw_string(info_msg, x + 30, y + TITLE_BAR_HEIGHT + 68, C_TEXT_MUTED, 11.0f);
    }

    // Draw progress bar
    int bar_y = y + TITLE_BAR_HEIGHT + 90;
    int bar_w = w - 60;
    drivers::Framebuffer::draw_rounded_rect_alpha(x + 30, bar_y, bar_w, 8, 4, 0x00CBD5E1, 255);
    if (audio_is_wav) {
        if (audio_wav_size > 0) {
            int src_bytes_per_sample = audio_wav_bits_per_sample / 8;
            int src_frame_size = audio_wav_channels * src_bytes_per_sample;
            uint32_t current_src_frame = audio_wav_phase >> 16;
            uint32_t total_src_frames = audio_wav_size / src_frame_size;
            if (total_src_frames > 0) {
                int progress_w = (current_src_frame * bar_w) / total_src_frames;
                if (progress_w > bar_w) progress_w = bar_w;
                drivers::Framebuffer::draw_rounded_rect_alpha(x + 30, bar_y, progress_w, 8, 4, 0x000F52BA, 255);
            }
        }
    } else if (audio_note_count > 0) {
        int progress_w = (audio_note_idx * bar_w) / audio_note_count;
        if (progress_w > bar_w) progress_w = bar_w;
        drivers::Framebuffer::draw_rounded_rect_alpha(x + 30, bar_y, progress_w, 8, 4, 0x000F52BA, 255);
    }

    // Playback control buttons
    int play_x = w / 2 - 90;
    int pause_x = w / 2 - 25;
    int stop_x = w / 2 + 40;
    int btn_y = y + TITLE_BAR_HEIGHT + 115;
    int btn_w = 50;
    int btn_h = 30;

    // Play
    drivers::Framebuffer::draw_rounded_rect_alpha(x + play_x, btn_y, btn_w, btn_h, 6, 0x000F52BA, 255);
    draw_string("Play", x + play_x + 12, btn_y + 8, C_WHITE, 13.0f);

    // Pause
    drivers::Framebuffer::draw_rounded_rect_alpha(x + pause_x, btn_y, btn_w, btn_h, 6, 0x0064748B, 255);
    draw_string("Pause", x + pause_x + 8, btn_y + 8, C_WHITE, 13.0f);

    // Stop
    drivers::Framebuffer::draw_rounded_rect_alpha(x + stop_x, btn_y, btn_w, btn_h, 6, 0x00EF4444, 255);
    draw_string("Stop", x + stop_x + 12, btn_y + 8, C_WHITE, 13.0f);

    // Equalizer bars
    int eq_y = y + TITLE_BAR_HEIGHT + 165;
    int bar_width = 10;
    int bar_spacing = 15;
    int bars_count = 8;
    int total_bars_w = bars_count * bar_width + (bars_count - 1) * bar_spacing;
    int start_eq_x = x + (w - total_bars_w) / 2;

    for (int i = 0; i < bars_count; ++i) {
        int bar_h = 6;
        if (audio_playing) {
            if (audio_is_wav) {
                int anim = (audio_amplitude * (i + 3)) / 2500;
                if (anim > 50) anim = 50;
                bar_h = 6 + anim;
            } else {
                int anim = (frame_counter * (i + 1) * 3) % 45;
                bar_h = 6 + anim;
            }
        }
        int bx = start_eq_x + i * (bar_width + bar_spacing);
        int by = eq_y + 50 - bar_h;
        drivers::Framebuffer::draw_rounded_rect_alpha(bx, by, bar_width, bar_h, 3, 0x0010B981, 255);
    }
}

void WindowManager::draw_picture_viewer_content(Window* win) {
    int x = win->rect.x;
    int y = win->rect.y;
    int w = win->rect.w - 2;
    int h = win->rect.h - TITLE_BAR_HEIGHT - 12;

    const char* path = win->title + 17; // Extract path after "Picture Viewer - "
    fs::VFSNode* node = fs::VFS::open(path);
    if (!node) {
        draw_string("Failed to open image.", x + 20, y + TITLE_BAR_HEIGHT + 20, C_TEXT, 15.0f);
        return;
    }
    
    // Safety check: 2MB cap
    if (node->size > 2 * 1024 * 1024) {
        draw_string("Image exceeds 2MB safety limit.", x + 20, y + TITLE_BAR_HEIGHT + 20, C_TEXT, 15.0f);
        return;
    }

    char header[54];
    fs::File f;
    f.node = node;
    f.offset = 0;
    fs::VFS::read(&f, header, 54);

    if (header[0] != 'B' || header[1] != 'M') {
        draw_string("Invalid BMP format.", x + 20, y + TITLE_BAR_HEIGHT + 20, C_TEXT, 15.0f);
        return;
    }

    int bmp_w = *(int*)&header[18];
    int bmp_h = *(int*)&header[22];
    int bpp = *(short*)&header[28];
    int compression = *(int*)&header[30];
    int data_offset = *(int*)&header[10];

    if (compression != 0 || (bpp != 24 && bpp != 32)) {
        draw_string("Only uncompressed 24-bit/32-bit BMPs supported.", x + 20, y + TITLE_BAR_HEIGHT + 20, C_TEXT, 15.0f);
        return;
    }

    int row_stride = ((bmp_w * bpp + 31) / 32) * 4;
    int pixel_data_size = row_stride * bmp_h;

    uint8_t* pixel_data = (uint8_t*)kernel::kmalloc(pixel_data_size);
    if (!pixel_data) {
        draw_string("Kernel heap memory allocation failed.", x + 20, y + TITLE_BAR_HEIGHT + 20, C_TEXT, 15.0f);
        return;
    }

    f.offset = data_offset;
    fs::VFS::read(&f, pixel_data, pixel_data_size);

    int start_x = x + 1;
    int start_y = y + TITLE_BAR_HEIGHT;

    uint32_t y_ratio = (bmp_h << 16) / h;
    uint32_t x_ratio = (bmp_w << 16) / w;

    Rect clip = drivers::Framebuffer::get_clip_rect();

    for (int sy = 0; sy < h; ++sy) {
        int dy = start_y + sy;
        if (dy < clip.y || dy >= clip.y + clip.h) continue;

        uint32_t bmp_y = (sy * y_ratio) >> 16;
        uint32_t row_y = bmp_h - 1 - bmp_y;
        uint8_t* bmp_row = pixel_data + row_y * row_stride;

        uint32_t bmp_x_accum = 0;
        for (int sx = 0; sx < w; ++sx) {
            int dx = start_x + sx;
            if (dx >= clip.x && dx < clip.x + clip.w) {
                uint32_t bmp_x = bmp_x_accum >> 16;
                uint32_t color = 0;
                if (bpp == 24) {
                    uint8_t* p = bmp_row + bmp_x * 3;
                    color = (p[2] << 16) | (p[1] << 8) | p[0];
                } else if (bpp == 32) {
                    uint8_t* p = bmp_row + bmp_x * 4;
                    color = (p[2] << 16) | (p[1] << 8) | p[0];
                }
                drivers::Framebuffer::draw_pixel(dx, dy, color);
            }
            bmp_x_accum += x_ratio;
        }
    }

    kernel::kfree(pixel_data);
}

} // namespace wm
