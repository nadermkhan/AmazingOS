#include "syscall.hpp"
#include "vmm.hpp"
#include "pmm.hpp"
#include "scheduler.hpp"
#include "heap.hpp"
#include "../drivers/serial.hpp"
#include "../drivers/vga.hpp"
#include "wm.hpp"
#include "../drivers/ttf.hpp"
#include "elf.hpp"
#include "../drivers/e1000.hpp"

extern "C" {
    uint64_t user_rsp_temp = 0;
    void syscall_handler_entry();
    void* memcpy(void* dest, const void* src, size_t n);
    void fork_child_return();
}

#include "../drivers/ps2.hpp"
#include "../fs/vfs.hpp"

namespace kernel {

constexpr int EBADF = 9;
constexpr int ENOENT = 2;
constexpr int EMFILE = 24;
constexpr int EFAULT = 14;

struct SharedMemSegment {
    int key;
    uint64_t phys_frames[256];
    size_t page_count;
    int ref_count;
};

static SharedMemSegment shm_segments[16];

// --- POSIX Networking Support ---

static inline uint16_t htons(uint16_t val) {
    return (uint16_t)((val << 8) | (val >> 8));
}
static inline uint16_t ntohs(uint16_t val) {
    return htons(val);
}
static inline uint32_t htonl(uint32_t val) {
    return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | ((val & 0xFF0000) >> 8) | (val >> 24);
}
static inline uint32_t ntohl(uint32_t val) {
    return htonl(val);
}

struct EthernetHeader {
    uint8_t dest_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
} __attribute__((packed));

struct IPv4Header {
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed));

struct TCPHeader {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

struct TCPPseudoHeader {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t reserved;
    uint8_t protocol;
    uint16_t tcp_length;
} __attribute__((packed));

struct ARPHeader {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t op;
    uint8_t src_mac[6];
    uint32_t src_ip;
    uint8_t dest_mac[6];
    uint32_t dest_ip;
} __attribute__((packed));

static uint16_t ip_checksum(void* addr, int count) {
    uint32_t sum = 0;
    uint8_t* ptr = (uint8_t*)addr;
    for (int i = 0; i < count - 1; i += 2) {
        sum += ((uint32_t)ptr[i] << 8) + ptr[i+1];
    }
    if (count & 1) {
        sum += (uint32_t)ptr[count - 1] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    uint16_t checksum = (uint16_t)~sum;
    return htons(checksum);
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dest_ip, void* tcp_seg, int tcp_len) {
    uint32_t sum = 0;
    
    uint16_t* src_ptr = (uint16_t*)&src_ip;
    sum += ntohs(src_ptr[0]);
    sum += ntohs(src_ptr[1]);
    
    uint16_t* dest_ptr = (uint16_t*)&dest_ip;
    sum += ntohs(dest_ptr[0]);
    sum += ntohs(dest_ptr[1]);
    
    sum += 6; // protocol
    sum += tcp_len; // length
    
    uint8_t* ptr = (uint8_t*)tcp_seg;
    for (int i = 0; i < tcp_len - 1; i += 2) {
        sum += ((uint32_t)ptr[i] << 8) + ptr[i+1];
    }
    if (tcp_len & 1) {
        sum += (uint32_t)ptr[tcp_len - 1] << 8;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    uint16_t checksum = (uint16_t)~sum;
    return htons(checksum);
}

enum SocketState {
    SOCKET_CLOSED,
    SOCKET_SYN_SENT,
    SOCKET_ESTABLISHED,
    SOCKET_FIN_WAIT
};

struct Socket {
    bool active;
    SocketState state;
    uint32_t dest_ip;
    uint16_t dest_port;
    uint16_t src_port;
    uint32_t my_seq;
    uint32_t my_ack;
    char rx_buffer[8192];
    size_t rx_len;
};

static Socket sockets[16];

static void pci_poll_network() {
    alignas(16) char packet[2048];
    uint16_t len;
    while ((len = drivers::e1000_recv_packet(packet, sizeof(packet))) > 0) {
        if (len < sizeof(EthernetHeader)) continue;
        
        EthernetHeader* eth = (EthernetHeader*)packet;
        uint16_t ethertype = ntohs(eth->ethertype);
        
        if (ethertype == 0x0806) {
            if (len < sizeof(EthernetHeader) + sizeof(ARPHeader)) continue;
            ARPHeader* arp = (ARPHeader*)(packet + sizeof(EthernetHeader));
            if (ntohs(arp->op) == 1 && arp->dest_ip == 0x0F02000A) {
                alignas(16) char reply_pkt[128];
                EthernetHeader* out_eth = (EthernetHeader*)reply_pkt;
                for (int i = 0; i < 6; i++) {
                    out_eth->dest_mac[i] = eth->src_mac[i];
                    out_eth->src_mac[i] = eth->dest_mac[i];
                }
                out_eth->ethertype = htons(0x0806);
                
                ARPHeader* out_arp = (ARPHeader*)(reply_pkt + sizeof(EthernetHeader));
                out_arp->htype = htons(1);
                out_arp->ptype = htons(0x0800);
                out_arp->hlen = 6;
                out_arp->plen = 4;
                out_arp->op = htons(2);
                
                for (int i = 0; i < 6; i++) {
                    out_arp->src_mac[i] = eth->dest_mac[i];
                    out_arp->dest_mac[i] = arp->src_mac[i];
                }
                out_arp->src_ip = 0x0F02000A;
                out_arp->dest_ip = arp->src_ip;
                
                drivers::e1000_send_packet(reply_pkt, sizeof(EthernetHeader) + sizeof(ARPHeader));
            }
            continue;
        }
        
        if (ethertype != 0x0800) continue;
        if (len < sizeof(EthernetHeader) + sizeof(IPv4Header)) continue;
        
        IPv4Header* ip = (IPv4Header*)(packet + sizeof(EthernetHeader));
        if (ip->protocol != 6) continue;
        
        int ip_header_len = (ip->ver_ihl & 0x0F) * 4;
        TCPHeader* tcp = (TCPHeader*)(packet + sizeof(EthernetHeader) + ip_header_len);
        
        int sock_idx = -1;
        for (int i = 0; i < 16; i++) {
            if (sockets[i].active && 
                sockets[i].dest_ip == ip->src_ip && 
                sockets[i].dest_port == ntohs(tcp->src_port) && 
                sockets[i].src_port == ntohs(tcp->dest_port)) {
                sock_idx = i;
                break;
            }
        }
        
        if (sock_idx == -1) continue;
        Socket& sock = sockets[sock_idx];
        
        uint32_t incoming_seq = ntohl(tcp->seq);
        uint32_t incoming_ack = ntohl(tcp->ack);
        uint16_t tcp_header_len = (tcp->data_offset >> 4) * 4;
        uint16_t ip_total_len = ntohs(ip->total_length);
        uint16_t payload_len = ip_total_len - ip_header_len - tcp_header_len;
        
        if (sock.state == SOCKET_SYN_SENT && (tcp->flags & 0x12) == 0x12) {
            sock.my_ack = incoming_seq + 1;
            sock.my_seq = incoming_ack;
            sock.state = SOCKET_ESTABLISHED;
            
            char ack_pkt[128];
            EthernetHeader* out_eth = (EthernetHeader*)ack_pkt;
            for (int i = 0; i < 6; i++) {
                out_eth->dest_mac[i] = eth->src_mac[i];
                out_eth->src_mac[i] = eth->dest_mac[i];
            }
            out_eth->ethertype = htons(0x0800);
            
            IPv4Header* out_ip = (IPv4Header*)(ack_pkt + sizeof(EthernetHeader));
            out_ip->ver_ihl = 0x45;
            out_ip->tos = 0;
            out_ip->total_length = htons(40);
            out_ip->id = htons(1);
            out_ip->flags_fragment = 0;
            out_ip->ttl = 64;
            out_ip->protocol = 6;
            out_ip->checksum = 0;
            out_ip->src_ip = ip->dest_ip;
            out_ip->dest_ip = ip->src_ip;
            out_ip->checksum = ip_checksum(out_ip, 20);
            
            TCPHeader* out_tcp = (TCPHeader*)(ack_pkt + sizeof(EthernetHeader) + 20);
            out_tcp->src_port = tcp->dest_port;
            out_tcp->dest_port = tcp->src_port;
            out_tcp->seq = htonl(sock.my_seq);
            out_tcp->ack = htonl(sock.my_ack);
            out_tcp->data_offset = (5 << 4);
            out_tcp->flags = 0x10;
            out_tcp->window = htons(4096);
            out_tcp->checksum = 0;
            out_tcp->urgent = 0;
            out_tcp->checksum = tcp_checksum(out_ip->src_ip, out_ip->dest_ip, out_tcp, 20);
            
            drivers::e1000_send_packet(ack_pkt, sizeof(EthernetHeader) + 40);
        }
        else if (sock.state == SOCKET_ESTABLISHED) {
            if (payload_len > 0) {
                if (incoming_seq == sock.my_ack) {
                    if (sock.rx_len + payload_len < sizeof(sock.rx_buffer)) {
                        char* payload = (char*)tcp + tcp_header_len;
                        for (uint16_t i = 0; i < payload_len; i++) {
                            sock.rx_buffer[sock.rx_len + i] = payload[i];
                        }
                        sock.rx_len += payload_len;
                        sock.my_ack += payload_len;
                    }
                    
                    char ack_pkt[128];
                    EthernetHeader* out_eth = (EthernetHeader*)ack_pkt;
                    for (int i = 0; i < 6; i++) {
                        out_eth->dest_mac[i] = eth->src_mac[i];
                        out_eth->src_mac[i] = eth->dest_mac[i];
                    }
                    out_eth->ethertype = htons(0x0800);
                    
                    IPv4Header* out_ip = (IPv4Header*)(ack_pkt + sizeof(EthernetHeader));
                    out_ip->ver_ihl = 0x45;
                    out_ip->tos = 0;
                    out_ip->total_length = htons(40);
                    out_ip->id = htons(1);
                    out_ip->flags_fragment = 0;
                    out_ip->ttl = 64;
                    out_ip->protocol = 6;
                    out_ip->checksum = 0;
                    out_ip->src_ip = ip->dest_ip;
                    out_ip->dest_ip = ip->src_ip;
                    out_ip->checksum = ip_checksum(out_ip, 20);
                    
                    TCPHeader* out_tcp = (TCPHeader*)(ack_pkt + sizeof(EthernetHeader) + 20);
                    out_tcp->src_port = tcp->dest_port;
                    out_tcp->dest_port = tcp->src_port;
                    out_tcp->seq = htonl(sock.my_seq);
                    out_tcp->ack = htonl(sock.my_ack);
                    out_tcp->data_offset = (5 << 4);
                    out_tcp->flags = 0x10;
                    out_tcp->window = htons(4096);
                    out_tcp->checksum = 0;
                    out_tcp->urgent = 0;
                    out_tcp->checksum = tcp_checksum(out_ip->src_ip, out_ip->dest_ip, out_tcp, 20);
                    
                    drivers::e1000_send_packet(ack_pkt, sizeof(EthernetHeader) + 40);
                }
            }
            
            if (tcp->flags & 0x01) {
                sock.my_ack = incoming_seq + 1;
                sock.state = SOCKET_CLOSED;
                
                char ack_pkt[128];
                EthernetHeader* out_eth = (EthernetHeader*)ack_pkt;
                for (int i = 0; i < 6; i++) {
                    out_eth->dest_mac[i] = eth->src_mac[i];
                    out_eth->src_mac[i] = eth->dest_mac[i];
                }
                out_eth->ethertype = htons(0x0800);
                
                IPv4Header* out_ip = (IPv4Header*)(ack_pkt + sizeof(EthernetHeader));
                out_ip->ver_ihl = 0x45;
                out_ip->tos = 0;
                out_ip->total_length = htons(40);
                out_ip->id = htons(1);
                out_ip->flags_fragment = 0;
                out_ip->ttl = 64;
                out_ip->protocol = 6;
                out_ip->checksum = 0;
                out_ip->src_ip = ip->dest_ip;
                out_ip->dest_ip = ip->src_ip;
                out_ip->checksum = ip_checksum(out_ip, 20);
                
                TCPHeader* out_tcp = (TCPHeader*)(ack_pkt + sizeof(EthernetHeader) + 20);
                out_tcp->src_port = tcp->dest_port;
                out_tcp->dest_port = tcp->src_port;
                out_tcp->seq = htonl(sock.my_seq);
                out_tcp->ack = htonl(sock.my_ack);
                out_tcp->data_offset = (5 << 4);
                out_tcp->flags = 0x11;
                out_tcp->window = htons(4096);
                out_tcp->checksum = 0;
                out_tcp->urgent = 0;
                out_tcp->checksum = tcp_checksum(out_ip->src_ip, out_ip->dest_ip, out_tcp, 20);
                
                drivers::e1000_send_packet(ack_pkt, sizeof(EthernetHeader) + 40);
            }
        }
    }
}

// ---------------------------------

// MSR register offsets
constexpr uint32_t MSR_EFER   = 0xC0000080;
constexpr uint32_t MSR_STAR   = 0xC0000081;
constexpr uint32_t MSR_LSTAR  = 0xC0000082;
constexpr uint32_t MSR_SFMASK = 0xC0000084;

static void write_msr(uint32_t msr, uint64_t val) {
    uint32_t low = (uint32_t)val;
    uint32_t high = (uint32_t)(val >> 32);
    __asm__ __volatile__ ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ __volatile__ ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void syscall_init() {
    // 1. Enable SCE (System Call Extension) bit 0 and NXE (No-Execute Enable) bit 11 in Extended Feature Enable Register (EFER)
    uint64_t efer = read_msr(MSR_EFER);
    write_msr(MSR_EFER, efer | 1 | (1ULL << 11));

    // 2. Configure STAR MSR:
    // Bits 32-47: Kernel Segment selector base (0x08 -> Kernel Code selector, 0x10 Kernel Data selector)
    // Bits 48-63: User Segment selector base (0x10 selector index -> User CS = 0x23, User SS = 0x1B)
    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    write_msr(MSR_STAR, star);

    // 3. Configure LSTAR MSR to point to assembly system call entrypoint
    write_msr(MSR_LSTAR, (uint64_t)&syscall_handler_entry);

    // 4. Configure SFMASK MSR (bit 9 is IF, Interrupt flag).
    // Masking it clears IF on entry, disabling hardware interrupts during syscall startup.
    write_msr(MSR_SFMASK, 0x200);

    drivers::Serial::println("[INIT] System Call (SCE) MSRs configured successfully.");
}

// Walk page table to verify pointer safety
bool syscall_validate_buffer(const void* ptr, size_t size) {
    uint64_t addr = (uint64_t)ptr;
    uint64_t end = addr + size;

    uint64_t start_page = addr & ~0xFFFULL;
    uint64_t end_page = (end + 4095) & ~0xFFFULL;

    for (uint64_t page = start_page; page < end_page; page += 4096) {
        if (!vmm_is_user_mapped(page)) {
            return false; // Page is either not present or is a kernel-space page!
        }
    }
    return true;
}

extern "C" __attribute__((sysv_abi)) void syscall_dispatcher(SyscallFrame* frame) {
    // Read syscall number from RAX
    uint64_t syscall_num = frame->rax;



    switch (syscall_num) {
        case 1: { // sys_write (POSIX compliant)
            int fd = (int)frame->rdi;
            const char* buffer = (const char*)frame->rsi;
            size_t count = frame->rdx;

            if (!syscall_validate_buffer(buffer, count)) {
                drivers::Serial::println("[SYSCALL] Security violation: sys_write passed invalid user buffer pointer!");
                frame->rax = -EFAULT;
                break;
            }

            if (fd < 0 || fd >= 16) {
                frame->rax = -EBADF;
                break;
            }

            fs::File* file = current_thread->fd_table[fd];
            if (!file || !file->node) {
                frame->rax = -EBADF;
                break;
            }

            if (file->node == (fs::VFSNode*)2 || file->node == (fs::VFSNode*)3) { // stdout or stderr
                // Print each character as a null-terminated string to match driver APIs
                char temp[2] = {0, 0};
                for (size_t i = 0; i < count; ++i) {
                    temp[0] = buffer[i];
                    drivers::Vga::print(temp);
                    drivers::Serial::print(temp);
                }
                frame->rax = count;
            } else {
                ssize_t written = fs::VFS::write(file, buffer, count);
                if (written < 0) {
                    frame->rax = -EBADF;
                } else {
                    frame->rax = (uint64_t)written;
                }
            }
            break;
        }

        case 2: { // sys_yield
            // Cooperatively yield processor control to next ready thread
            schedule();
            frame->rax = 0;
            break;
        }

        case 3: { // sys_exit
            int status = (int)frame->rdi;
            // Terminate the current user thread and pass status
            thread_exit(status);
            // This case never returns
            break;
        }

        case 4: { // sys_create_window
            int x = (int)frame->rdi;
            int y = (int)frame->rsi;
            int w = (int)frame->rdx;
            int h = (int)frame->r10;
            const char* title = (const char*)frame->r8;
            uint32_t color = (uint32_t)frame->r9;

            // Safe strnlen to prevent read overflow Page Faults
            size_t len = 0;
            while (len < 64 && title[len]) {
                len++;
            }

            if (!syscall_validate_buffer(title, len)) {
                drivers::Serial::println("[SYSCALL] Security violation: sys_create_window passed invalid title pointer!");
                frame->rax = (uint64_t)-1;
                break;
            }

            wm::Window* win = wm::WindowManager::create_window(x, y, w, h, title, color);
            if (!win) {
                frame->rax = (uint64_t)-1;
                break;
            }

            int client_w = w - 2;
            int client_h = h - 42;
            if (client_w > 0 && client_h > 0) {
                win->buffer = new uint32_t[client_w * client_h];
                for (int i = 0; i < client_w * client_h; i++) {
                    win->buffer[i] = 0x00FFFFFF;
                }
            }

            frame->rax = win->id;
            break;
        }

        case 5: { // sys_update_window
            int win_id = (int)frame->rdi;
            const uint32_t* user_buf = (const uint32_t*)frame->rsi;

            wm::Window* win = wm::WindowManager::get_window_by_id(win_id);
            if (!win || !win->buffer) {
                frame->rax = (uint64_t)-1;
                break;
            }

            int client_w = win->rect.w - 2;
            int client_h = win->rect.h - 42;
            size_t size_bytes = client_w * client_h * sizeof(uint32_t);

            if (!syscall_validate_buffer(user_buf, size_bytes)) {
                drivers::Serial::println("[SYSCALL] Security violation: sys_update_window passed invalid user buffer pointer!");
                frame->rax = (uint64_t)-1;
                break;
            }

            memcpy(win->buffer, user_buf, size_bytes);
            wm::WindowManager::force_redraw_all();

            frame->rax = 0;
            break;
        }

        case 6: { // sys_get_window_event
            int win_id = (int)frame->rdi;
            Event* user_ev = (Event*)frame->rsi;

            if (!syscall_validate_buffer(user_ev, sizeof(Event))) {
                drivers::Serial::println("[SYSCALL] Security violation: sys_get_window_event passed invalid event pointer!");
                frame->rax = (uint64_t)-1;
                break;
            }

            wm::Window* win = wm::WindowManager::get_window_by_id(win_id);
            if (!win) {
                frame->rax = (uint64_t)-1;
                break;
            }

            if (win->pending_event.type != 0) {
                user_ev->type = win->pending_event.type;
                user_ev->mx = win->pending_event.mx;
                user_ev->my = win->pending_event.my;
                user_ev->key = win->pending_event.key;

                win->pending_event.type = 0;
                frame->rax = 1;
            } else {
                frame->rax = 0;
            }
            break;
        }

        case 7: { // sys_draw_string
            int win_id = (int)frame->rdi;
            int x = (int)frame->rsi;
            int y = (int)frame->rdx;
            uint32_t color = (uint32_t)frame->r10;
            const char* user_str = (const char*)frame->r8;
            int size = (int)frame->r9;

            if (size < 6 || size > 72) {
                frame->rax = (uint64_t)-1;
                break;
            }

            size_t len = 0;
            while (len < 128 && user_str[len]) {
                len++;
            }

            if (!syscall_validate_buffer(user_str, len)) {
                drivers::Serial::println("[SYSCALL] Security violation: sys_draw_string passed invalid string pointer!");
                frame->rax = (uint64_t)-1;
                break;
            }

            wm::Window* win = wm::WindowManager::get_window_by_id(win_id);
            if (!win || !win->buffer) {
                frame->rax = (uint64_t)-1;
                break;
            }

            int client_w = win->rect.w - 2;
            int client_h = win->rect.h - 42;

            drivers::TtfRenderer::draw_string_target(win->buffer, client_w, client_h, user_str, x, y, color, (float)size);
            wm::WindowManager::force_redraw_all();

            frame->rax = 0;
            break;
        }

        case 8: { // sys_open
            const char* path = (const char*)frame->rdi;
            int flags = (int)frame->rsi;
            (void)flags;

            // Safe strlen calculation to validate path buffer
            size_t len = 0;
            while (len < 256 && path[len]) {
                len++;
            }

            if (!syscall_validate_buffer(path, len)) {
                frame->rax = -EFAULT;
                break;
            }

            fs::VFSNode* node = fs::VFS::open(path);
            if (!node) {
                frame->rax = -ENOENT;
                break;
            }

            // Lock interrupts to prevent scheduler preemption on fd_table modification
            uint64_t rflags;
            __asm__ __volatile__ ("pushfq; popq %0; cli" : "=r"(rflags));

            int free_fd = -1;
            for (int i = 0; i < Thread::MAX_FILE_DESCRIPTORS; i++) {
                if (current_thread->fd_table[i] == nullptr) {
                    free_fd = i;
                    break;
                }
            }

            if (free_fd == -1) {
                __asm__ __volatile__ ("pushq %0; popfq" : : "r"(rflags));
                frame->rax = -EMFILE;
                break;
            }

            // Locate a free descriptor slot in fd_pool
            int free_pool_idx = -1;
            for (int i = 0; i < Thread::MAX_FILE_DESCRIPTORS; i++) {
                if (current_thread->fd_pool[i].ref_count == 0) {
                    free_pool_idx = i;
                    break;
                }
            }

            if (free_pool_idx == -1) {
                __asm__ __volatile__ ("pushq %0; popfq" : : "r"(rflags));
                frame->rax = -EMFILE;
                break;
            }

            fs::File* file = &current_thread->fd_pool[free_pool_idx];
            file->node = node;
            file->offset = 0;
            file->flags = flags;
            file->ref_count = 1;

            current_thread->fd_table[free_fd] = file;

            // Restore RFLAGS
            __asm__ __volatile__ ("pushq %0; popfq" : : "r"(rflags));

            frame->rax = free_fd;
            break;
        }

        case 9: { // sys_read
            int fd = (int)frame->rdi;
            void* buffer = (void*)frame->rsi;
            size_t count = frame->rdx;

            if (!syscall_validate_buffer(buffer, count)) {
                frame->rax = -EFAULT;
                break;
            }

            if (fd < 0 || fd >= 16) {
                frame->rax = -EBADF;
                break;
            }

            fs::File* file = current_thread->fd_table[fd];
            if (!file || !file->node) {
                frame->rax = -EBADF;
                break;
            }

            if (file->node == (fs::VFSNode*)1) { // stdin
                char* dest = (char*)buffer;
                size_t read_bytes = 0;
                while (read_bytes < count) {
                    char c = 0;
                    bool got_char = false;
                    while (!got_char) {
                        if (drivers::Ps2::poll_keyboard(c)) {
                            got_char = true;
                        } else if (drivers::Serial::is_received()) {
                            c = drivers::Serial::read_char();
                            if (c == '\r') {
                                c = '\n';
                            }
                            got_char = true;
                        } else {
                            schedule();
                        }
                    }
                    if (c == '\b' || c == 127) {
                        if (read_bytes > 0) {
                            read_bytes--;
                            char erase_seq[4] = {'\b', ' ', '\b', '\0'};
                            drivers::Vga::print(erase_seq);
                            drivers::Serial::print(erase_seq);
                        }
                    } else if (c == '\n' || c == '\r') {
                        dest[read_bytes++] = '\n';
                        char nl[2] = {'\n', '\0'};
                        drivers::Vga::print(nl);
                        drivers::Serial::print(nl);
                        break;
                    } else {
                        dest[read_bytes++] = c;
                        char ech[2] = {c, '\0'};
                        drivers::Vga::print(ech);
                        drivers::Serial::print(ech);
                    }
                }
                frame->rax = read_bytes;
            } else if (file->node == (fs::VFSNode*)2 || file->node == (fs::VFSNode*)3) { // stdout or stderr
                frame->rax = -EBADF;
            } else { // filesystem node
                ssize_t read_bytes = fs::VFS::read(file, buffer, count);
                if (read_bytes < 0) {
                    frame->rax = -EBADF;
                } else {
                    frame->rax = (uint64_t)read_bytes;
                }
            }
            break;
        }

        case 10: { // sys_close
            int fd = (int)frame->rdi;

            if (fd < 0 || fd >= 16) {
                frame->rax = -EBADF;
                break;
            }

            // Lock interrupts to modify fd_table
            uint64_t rflags;
            __asm__ __volatile__ ("pushfq; popq %0; cli" : "=r"(rflags));

            fs::File* file = current_thread->fd_table[fd];
            if (!file || file->ref_count <= 0) {
                __asm__ __volatile__ ("pushq %0; popfq" : : "r"(rflags));
                frame->rax = -EBADF;
                break;
            }

            file->ref_count--;
            if (file->ref_count == 0) {
                file->node = nullptr;
                file->offset = 0;
                file->flags = 0;
            }

            current_thread->fd_table[fd] = nullptr;

            // Restore RFLAGS
            __asm__ __volatile__ ("pushq %0; popfq" : : "r"(rflags));

            frame->rax = 0;
            break;
        }

        case 11: { // sys_fork
            drivers::Serial::println("[SYSCALL] sys_fork cloning parent PML4 and stack.");
            drivers::Serial::print("[SYSCALL] sys_fork parent PML4: ");
            char parent_hex[19];
            parent_hex[0] = '0'; parent_hex[1] = 'x';
            const char* hex_chars_local = "0123456789ABCDEF";
            for (int x = 0; x < 16; ++x) {
                parent_hex[2 + x] = hex_chars_local[(current_thread->pml4_phys >> ((15 - x) * 4)) & 0x0F];
            }
            parent_hex[18] = '\0';
            drivers::Serial::println(parent_hex);
            // 1. Create child thread control block
            Thread* child = new Thread();
            if (!child) {
                frame->rax = (uint64_t)-1;
                break;
            }

            // 2. Clone page tables (address space)
            uint64_t child_pml4 = vmm_create_user_address_space();
            if (!child_pml4) {
                delete child;
                frame->rax = (uint64_t)-1;
                break;
            }

            drivers::Serial::print("[SYSCALL] sys_fork child PML4: ");
            char hex_buf[19];
            hex_buf[0] = '0'; hex_buf[1] = 'x';
            const char* hex_chars = "0123456789ABCDEF";
            for (int x = 0; x < 16; ++x) {
                hex_buf[2 + x] = hex_chars[(child_pml4 >> ((15 - x) * 4)) & 0x0F];
            }
            hex_buf[18] = '\0';
            drivers::Serial::println(hex_buf);

            // Clone all parent user-space mappings
            vmm_clone_user_space(current_thread->pml4_phys, child_pml4);

            // 3. Allocate and clone kernel stack
            void* child_stack = kmalloc(4096);
            if (!child_stack) {
                vmm_destroy_user_address_space(child_pml4);
                delete child;
                frame->rax = (uint64_t)-1;
                break;
            }

            // Calculate stack usage offset
            uint64_t stack_used = current_thread->rsp0 - (uint64_t)frame;
            uint64_t child_rsp0 = (uint64_t)child_stack + 4096;
            uint64_t child_frame_addr = child_rsp0 - stack_used;

            drivers::Serial::print("[DEBUG] parent_rsp0: ");
            char buf1[19]; buf1[0] = '0'; buf1[1] = 'x';
            for (int x = 0; x < 16; ++x) buf1[2 + x] = hex_chars[(current_thread->rsp0 >> ((15 - x) * 4)) & 0x0F];
            buf1[18] = '\0';
            drivers::Serial::print(buf1);
            
            drivers::Serial::print(" parent_frame: ");
            char buf2[19]; buf2[0] = '0'; buf2[1] = 'x';
            for (int x = 0; x < 16; ++x) buf2[2 + x] = hex_chars[((uint64_t)frame >> ((15 - x) * 4)) & 0x0F];
            buf2[18] = '\0';
            drivers::Serial::print(buf2);
            
            drivers::Serial::print(" stack_used: ");
            char buf3[16];
            int idx = 0;
            uint64_t temp = stack_used;
            if (temp == 0) buf3[idx++] = '0';
            else {
                char rev[16];
                int r_idx = 0;
                while (temp > 0) { rev[r_idx++] = '0' + (temp % 10); temp /= 10; }
                while (r_idx > 0) buf3[idx++] = rev[--r_idx];
            }
            buf3[idx] = '\0';
            drivers::Serial::print(buf3);
            
            drivers::Serial::print(" child_rsp0: ");
            char buf4[19]; buf4[0] = '0'; buf4[1] = 'x';
            for (int x = 0; x < 16; ++x) buf4[2 + x] = hex_chars[(child_rsp0 >> ((15 - x) * 4)) & 0x0F];
            buf4[18] = '\0';
            drivers::Serial::print(buf4);
            
            drivers::Serial::print(" child_frame_addr: ");
            char buf5[19]; buf5[0] = '0'; buf5[1] = 'x';
            for (int x = 0; x < 16; ++x) buf5[2 + x] = hex_chars[(child_frame_addr >> ((15 - x) * 4)) & 0x0F];
            buf5[18] = '\0';
            drivers::Serial::println(buf5);

            // Copy kernel stack frame
            memcpy((void*)child_frame_addr, (void*)frame, stack_used);

            // Set child RAX return value to 0 inside child's copied frame
            SyscallFrame* child_frame = (SyscallFrame*)child_frame_addr;
            child_frame->rax = 0;

            // Push context_switch return address and 6 callee-saved registers
            uint64_t* child_stack_ptr = (uint64_t*)child_frame_addr;
            *(--child_stack_ptr) = (uint64_t)&fork_child_return; // return RIP
            *(--child_stack_ptr) = 0; // RBP
            *(--child_stack_ptr) = 0; // RBX
            *(--child_stack_ptr) = 0; // R15
            *(--child_stack_ptr) = 0; // R14
            *(--child_stack_ptr) = 0; // R13
            *(--child_stack_ptr) = 0; // R12

            // 4. Initialize child Thread structure
            child->rsp = (uint64_t)child_stack_ptr;

            drivers::Serial::print("[DEBUG] child->rsp: ");
            char buf6[19]; buf6[0] = '0'; buf6[1] = 'x';
            const char* hex_chars_local_2 = "0123456789ABCDEF";
            for (int x = 0; x < 16; ++x) buf6[2 + x] = hex_chars_local_2[(child->rsp >> ((15 - x) * 4)) & 0x0F];
            buf6[18] = '\0';
            drivers::Serial::println(buf6);

            child->id = scheduler_generate_id();
            child->state = THREAD_RUNNABLE;
            child->stack_limit = child_stack;
            child->rsp0 = child_rsp0;
            child->pml4_phys = child_pml4;
            child->next = nullptr;
            child->waiting_for_pid = 0;
            child->exit_status = 0;
            child->parent_id = current_thread->id;

            // Copy file descriptors
            for (int i = 0; i < Thread::MAX_FILE_DESCRIPTORS; i++) {
                if (current_thread->fd_table[i]) {
                    child->fd_pool[i] = current_thread->fd_pool[i];
                    child->fd_table[i] = &child->fd_pool[i];
                    child->fd_pool[i].ref_count = 1;
                } else {
                    child->fd_table[i] = nullptr;
                }
            }

            // Register and Enqueue child thread
            scheduler_register_thread(child);
            scheduler_enqueue(child);
            drivers::Serial::println("[SYSCALL] sys_fork child thread successfully registered and enqueued.");

            // Return child ID to the parent process
            frame->rax = child->id;
            break;
        }

        case 12: { // sys_execve
            const char* path = (const char*)frame->rdi;
            char* const* argv = (char* const*)frame->rsi;
            char* const* envp = (char* const*)frame->rdx;
            (void)envp;

            drivers::Serial::print("[SYSCALL] sys_execve called for path: ");
            drivers::Serial::println(path ? path : "NULL");

            // Validate path pointer
            size_t len = 0;
            while (len < 256 && path[len]) {
                len++;
            }
            if (!syscall_validate_buffer(path, len)) {
                frame->rax = -EFAULT;
                break;
            }

            // Parse argv arguments (read strings from parent's context before switching CR3)
            int argc = 0;
            char args_buf[16][128];
            if (argv) {
                if (syscall_validate_buffer(argv, sizeof(char*) * 16)) {
                    for (int i = 0; i < 16; i++) {
                        char* arg_ptr = argv[i];
                        if (!arg_ptr) break;
                        size_t arg_len = 0;
                        while (arg_len < 127 && arg_ptr[arg_len]) {
                            arg_len++;
                        }
                        if (syscall_validate_buffer(arg_ptr, arg_len)) {
                            memcpy(args_buf[argc], arg_ptr, arg_len);
                            args_buf[argc][arg_len] = '\0';
                            argc++;
                        } else {
                            break;
                        }
                    }
                }
            }

            uint64_t entry_point = 0;
            uint64_t stack_top = 0;
            uint64_t pml4_phys = 0;

            if (!elf_load(path, entry_point, stack_top, pml4_phys)) {
                frame->rax = -ENOENT;
                break;
            }

            // Lock interrupts to prevent scheduler preemption on active PML4 updates
            uint64_t rflags;
            __asm__ __volatile__ ("pushfq; popq %0; cli" : "=r"(rflags));

            // Clean up old child PML4 if it wasn't the main/boot address space
            if (current_thread->pml4_phys != active_pml4) { // wait, if we replace the active address space, let's defer destruction or just switch
                // ...
            }

            // Update thread context and CR3
            uint64_t old_pml4 = current_thread->pml4_phys;
            current_thread->pml4_phys = pml4_phys;
            active_pml4 = pml4_phys;
            __asm__ __volatile__ ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");

            // Copy arguments onto the new stack
            uint64_t arg_ptors[16];
            char* stack_char_ptr = (char*)stack_top;
            for (int i = argc - 1; i >= 0; i--) {
                size_t arg_len = 0;
                while (args_buf[i][arg_len]) arg_len++;
                stack_char_ptr -= (arg_len + 1);
                memcpy(stack_char_ptr, args_buf[i], arg_len + 1);
                arg_ptors[i] = (uint64_t)stack_char_ptr;
            }

            uint64_t aligned_stack = (uint64_t)stack_char_ptr & ~7ULL;
            uint64_t* stack_ptrs = (uint64_t*)aligned_stack;

            stack_ptrs--;
            *stack_ptrs = 0; // NULL sentinel
            for (int i = argc - 1; i >= 0; i--) {
                stack_ptrs--;
                *stack_ptrs = arg_ptors[i];
            }

            uint64_t argv_address = (uint64_t)stack_ptrs;

            stack_ptrs--;
            *stack_ptrs = argc;

            uint64_t final_rsp = (uint64_t)stack_ptrs;

            // Restore interrupts
            __asm__ __volatile__ ("pushq %0; popfq" : : "r"(rflags));

            // Clean up old address space memory if it was a custom user process space
            // Since we switched CR3, we can safely destroy the old child PML4!
            if (current_thread->id != 0 && old_pml4 != 0) {
                // To avoid deleting active tables: we do it safely
                // vmm_destroy_user_address_space(old_pml4);
            }

            frame->rip = entry_point;
            frame->rsp = final_rsp;
            frame->rdi = argc;
            frame->rsi = argv_address;
            frame->rax = 0;
            break;
        }

        case 13: { // sys_waitpid
            int target_pid = (int)frame->rdi;
            int* wstatus = (int*)frame->rsi;
            int options = (int)frame->rdx;
            (void)options;

            if (wstatus && !syscall_validate_buffer(wstatus, sizeof(int))) {
                frame->rax = -EFAULT;
                break;
            }

            frame->rax = (uint64_t)scheduler_waitpid(target_pid, wstatus);
            break;
        }

        case 14: { // sys_shm_get
            int key = (int)frame->rdi;
            size_t size = frame->rsi;

            size_t page_count = (size + 4095) / 4096;
            if (page_count == 0 || page_count > 256) {
                frame->rax = (uint64_t)-1;
                break;
            }

            int shmid = -1;
            uint64_t rflags;
            __asm__ __volatile__ ("pushfq; popq %0; cli" : "=r"(rflags));

            for (int i = 0; i < 16; i++) {
                if (shm_segments[i].ref_count > 0 && shm_segments[i].key == key) {
                    shmid = i;
                    break;
                }
            }

            if (shmid == -1) {
                for (int i = 0; i < 16; i++) {
                    if (shm_segments[i].ref_count == 0) {
                        shmid = i;
                        break;
                    }
                }

                if (shmid != -1) {
                    shm_segments[shmid].key = key;
                    shm_segments[shmid].page_count = page_count;
                    shm_segments[shmid].ref_count = 1;
                    bool ok = true;
                    for (size_t i = 0; i < page_count; i++) {
                        uint64_t frame_phys = pmm_alloc_frame();
                        if (!frame_phys) {
                            ok = false;
                            for (size_t j = 0; j < i; j++) {
                                pmm_free_frame(shm_segments[shmid].phys_frames[j]);
                            }
                            break;
                        }
                        shm_segments[shmid].phys_frames[i] = frame_phys;
                    }
                    if (!ok) {
                        shm_segments[shmid].ref_count = 0;
                        shmid = -1;
                    }
                }
            }

            __asm__ __volatile__ ("pushq %0; popfq" : : "r"(rflags));

            frame->rax = (uint64_t)shmid;
            break;
        }

        case 15: { // sys_shm_at
            int shmid = (int)frame->rdi;
            uint64_t virt_addr = frame->rsi;

            if (shmid < 0 || shmid >= 16 || shm_segments[shmid].ref_count == 0) {
                frame->rax = (uint64_t)-1;
                break;
            }

            if ((virt_addr & 0xFFFUL) != 0 || virt_addr < 0x400000000ULL) {
                frame->rax = (uint64_t)-1;
                break;
            }

            bool ok = true;
            uint64_t rflags;
            __asm__ __volatile__ ("pushfq; popq %0; cli" : "=r"(rflags));

            size_t pages = shm_segments[shmid].page_count;
            uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER | VMM_FLAG_NO_EXECUTE;
            for (size_t i = 0; i < pages; i++) {
                uint64_t phys = shm_segments[shmid].phys_frames[i];
                if (!vmm_map_page_in_pml4(current_thread->pml4_phys, virt_addr + i * 4096, phys, flags)) {
                    ok = false;
                    break;
                }
            }

            __asm__ __volatile__ ("pushq %0; popfq" : : "r"(rflags));

            frame->rax = ok ? 0 : (uint64_t)-1;
            break;
        }

        case 16: { // sys_socket
            int domain = (int)frame->rdi;
            int type = (int)frame->rsi;
            int protocol = (int)frame->rdx;
            (void)domain; (void)type; (void)protocol;

            int sock_idx = -1;
            for (int i = 0; i < 16; i++) {
                if (!sockets[i].active) {
                    sock_idx = i;
                    break;
                }
            }

            if (sock_idx != -1) {
                sockets[sock_idx].active = true;
                sockets[sock_idx].state = SOCKET_CLOSED;
                sockets[sock_idx].rx_len = 0;
                frame->rax = (uint64_t)(100 + sock_idx);
            } else {
                frame->rax = (uint64_t)-1;
            }
            break;
        }

        case 17: { // sys_connect
            int sockfd = (int)frame->rdi;
            uint64_t addr_ptr = frame->rsi;
            uint32_t addrlen = (uint32_t)frame->rdx;
            (void)addrlen;

            int sock_idx = sockfd - 100;
            if (sock_idx < 0 || sock_idx >= 16 || !sockets[sock_idx].active) {
                frame->rax = (uint64_t)-9; // EBADF
                break;
            }

            struct sockaddr_in {
                uint16_t sin_family;
                uint16_t sin_port;
                uint32_t sin_addr;
                char sin_zero[8];
            }* addr = (struct sockaddr_in*)addr_ptr;

            if (!syscall_validate_buffer(addr, sizeof(struct sockaddr_in))) {
                frame->rax = (uint64_t)-14; // EFAULT
                break;
            }

            Socket& sock = sockets[sock_idx];
            sock.dest_ip = addr->sin_addr;
            sock.dest_port = ntohs(addr->sin_port);
            sock.src_port = 50000 + sock_idx;
            sock.my_seq = 1000;
            sock.my_ack = 0;
            sock.state = SOCKET_SYN_SENT;
            sock.rx_len = 0;

            // Construct and send TCP SYN packet
            alignas(16) char syn_pkt[128];
            EthernetHeader* eth = (EthernetHeader*)syn_pkt;
            eth->dest_mac[0] = 0x52; eth->dest_mac[1] = 0x54; eth->dest_mac[2] = 0x00;
            eth->dest_mac[3] = 0x12; eth->dest_mac[4] = 0x35; eth->dest_mac[5] = 0x02;
            eth->src_mac[0] = 0x52; eth->src_mac[1] = 0x54; eth->src_mac[2] = 0x00;
            eth->src_mac[3] = 0x12; eth->src_mac[4] = 0x34; eth->src_mac[5] = 0x56;
            eth->ethertype = htons(0x0800);

            IPv4Header* ip = (IPv4Header*)(syn_pkt + sizeof(EthernetHeader));
            ip->ver_ihl = 0x45;
            ip->tos = 0;
            ip->total_length = htons(40);
            ip->id = htons(1);
            ip->flags_fragment = 0;
            ip->ttl = 64;
            ip->protocol = 6;
            ip->checksum = 0;
            ip->src_ip = 0x0F02000A; // 10.0.2.15 (big-endian 0x0F02000A)
            ip->dest_ip = sock.dest_ip;
            ip->checksum = ip_checksum(ip, 20);

            TCPHeader* tcp = (TCPHeader*)(syn_pkt + sizeof(EthernetHeader) + 20);
            tcp->src_port = htons(sock.src_port);
            tcp->dest_port = htons(sock.dest_port);
            tcp->seq = htonl(sock.my_seq);
            tcp->ack = 0;
            tcp->data_offset = (5 << 4);
            tcp->flags = 0x02; // SYN
            tcp->window = htons(4096);
            tcp->checksum = 0;
            tcp->urgent = 0;
            tcp->checksum = tcp_checksum(ip->src_ip, ip->dest_ip, tcp, 20);

            drivers::e1000_send_packet(syn_pkt, sizeof(EthernetHeader) + 40);

            // Poll for SYN-ACK
            int timeout = 50000000;
            while (sock.state == SOCKET_SYN_SENT && timeout > 0) {
                pci_poll_network();
                timeout--;
            }

            if (sock.state == SOCKET_ESTABLISHED) {
                frame->rax = 0;
            } else {
                frame->rax = (uint64_t)-110; // ETIMEDOUT
            }
            break;
        }

        case 18: { // sys_send
            int sockfd = (int)frame->rdi;
            const void* buf = (const void*)frame->rsi;
            size_t len = (size_t)frame->rdx;
            int flags = (int)frame->r10;
            (void)flags;

            int sock_idx = sockfd - 100;
            if (sock_idx < 0 || sock_idx >= 16 || !sockets[sock_idx].active || sockets[sock_idx].state != SOCKET_ESTABLISHED) {
                frame->rax = (uint64_t)-9; // EBADF
                break;
            }

            if (!syscall_validate_buffer(buf, len)) {
                frame->rax = (uint64_t)-14; // EFAULT
                break;
            }

            Socket& sock = sockets[sock_idx];
            alignas(16) char pkt[2048];
            EthernetHeader* eth = (EthernetHeader*)pkt;
            eth->dest_mac[0] = 0x52; eth->dest_mac[1] = 0x54; eth->dest_mac[2] = 0x00;
            eth->dest_mac[3] = 0x12; eth->dest_mac[4] = 0x35; eth->dest_mac[5] = 0x02;
            eth->src_mac[0] = 0x52; eth->src_mac[1] = 0x54; eth->src_mac[2] = 0x00;
            eth->src_mac[3] = 0x12; eth->src_mac[4] = 0x34; eth->src_mac[5] = 0x56;
            eth->ethertype = htons(0x0800);

            IPv4Header* ip = (IPv4Header*)(pkt + sizeof(EthernetHeader));
            ip->ver_ihl = 0x45;
            ip->tos = 0;
            ip->total_length = htons(40 + len);
            ip->id = htons(2);
            ip->flags_fragment = 0;
            ip->ttl = 64;
            ip->protocol = 6;
            ip->checksum = 0;
            ip->src_ip = 0x0F02000A; // 10.0.2.15 (big-endian 0x0F02000A)
            ip->dest_ip = sock.dest_ip;
            ip->checksum = ip_checksum(ip, 20);

            TCPHeader* tcp = (TCPHeader*)(pkt + sizeof(EthernetHeader) + 20);
            tcp->src_port = htons(sock.src_port);
            tcp->dest_port = htons(sock.dest_port);
            tcp->seq = htonl(sock.my_seq);
            tcp->ack = htonl(sock.my_ack);
            tcp->data_offset = (5 << 4);
            tcp->flags = 0x18; // PSH|ACK
            tcp->window = htons(4096);
            tcp->checksum = 0;
            tcp->urgent = 0;

            char* payload = (char*)tcp + 20;
            for (size_t i = 0; i < len; i++) {
                payload[i] = ((const char*)buf)[i];
            }

            tcp->checksum = tcp_checksum(ip->src_ip, ip->dest_ip, tcp, 20 + len);

            drivers::e1000_send_packet(pkt, sizeof(EthernetHeader) + 40 + len);

            sock.my_seq += len;
            frame->rax = len;
            break;
        }

        case 19: { // sys_recv
            int sockfd = (int)frame->rdi;
            void* buf = (void*)frame->rsi;
            size_t len = (size_t)frame->rdx;
            int flags = (int)frame->r10;
            (void)flags;

            int sock_idx = sockfd - 100;
            if (sock_idx < 0 || sock_idx >= 16 || !sockets[sock_idx].active) {
                frame->rax = (uint64_t)-9; // EBADF
                break;
            }

            if (!syscall_validate_buffer(buf, len)) {
                frame->rax = (uint64_t)-14; // EFAULT
                break;
            }

            Socket& sock = sockets[sock_idx];
            int timeout = 5000000;
            while (sock.rx_len == 0 && sock.state == SOCKET_ESTABLISHED && timeout > 0) {
                pci_poll_network();
                timeout--;
            }

            if (sock.rx_len > 0) {
                size_t to_copy = sock.rx_len;
                if (to_copy > len) to_copy = len;

                for (size_t i = 0; i < to_copy; i++) {
                    ((char*)buf)[i] = sock.rx_buffer[i];
                }

                for (size_t i = to_copy; i < sock.rx_len; i++) {
                    sock.rx_buffer[i - to_copy] = sock.rx_buffer[i];
                }
                sock.rx_len -= to_copy;

                frame->rax = to_copy;
            } else {
                frame->rax = 0;
            }
            break;
        }

        default: {
            drivers::Serial::print("[SYSCALL] Error: Invalid syscall index: ");
            // Print index in decimal
            char buf[32];
            uint64_t temp = syscall_num;
            int idx = 0;
            if (temp == 0) buf[idx++] = '0';
            else {
                char rev[32];
                int r_idx = 0;
                while (temp > 0) {
                    rev[r_idx++] = '0' + (temp % 10);
                    temp /= 10;
                }
                while (r_idx > 0) buf[idx++] = rev[--r_idx];
            }
            buf[idx] = '\0';
            drivers::Serial::println(buf);

            frame->rax = (uint64_t)-1;
            break;
        }
    }
}

} // namespace kernel
