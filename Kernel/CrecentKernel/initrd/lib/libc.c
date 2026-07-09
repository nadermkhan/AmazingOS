#include "../include/libc.h"

// Dummy function to satisfy MinGW compiler initialization call
void __main() {}

static inline long long syscall3(long long num, long long a1, long long a2, long long a3) {
    long long ret;
    register long long rdi_val __asm__("rdi") = a1;
    register long long rsi_val __asm__("rsi") = a2;
    register long long rdx_val __asm__("rdx") = a3;
    register long long rax_val __asm__("rax") = num;
    __asm__ __volatile__ (
        "syscall"
        : "=r"(ret)
        : "r"(rax_val), "r"(rdi_val), "r"(rsi_val), "r"(rdx_val)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long long syscall4(long long num, long long a1, long long a2, long long a3, long long a4) {
    long long ret;
    register long long rdi_val __asm__("rdi") = a1;
    register long long rsi_val __asm__("rsi") = a2;
    register long long rdx_val __asm__("rdx") = a3;
    register long long r10_val __asm__("r10") = a4;
    register long long rax_val __asm__("rax") = num;
    __asm__ __volatile__ (
        "syscall"
        : "=r"(ret)
        : "r"(rax_val), "r"(rdi_val), "r"(rsi_val), "r"(rdx_val), "r"(r10_val)
        : "rcx", "r11", "memory"
    );
    return ret;
}

int open(const char* path, int flags) {
    return (int)syscall3(8, (long long)path, (long long)flags, 0);
}

int read(int fd, void* buf, size_t count) {
    return (int)syscall3(9, (long long)fd, (long long)buf, (long long)count);
}

int write(int fd, const void* buf, size_t count) {
    return (int)syscall3(1, (long long)fd, (long long)buf, (long long)count);
}

int close(int fd) {
    return (int)syscall3(10, (long long)fd, 0, 0);
}

int fork() {
    return (int)syscall3(11, 0, 0, 0);
}

int execve(const char* path, char* const argv[], char* const envp[]) {
    return (int)syscall3(12, (long long)path, (long long)argv, (long long)envp);
}

void exit(int status) {
    syscall3(3, (long long)status, 0, 0);
    while (1);
}

int waitpid(int pid, int* wstatus, int options) {
    return (int)syscall3(13, (long long)pid, (long long)wstatus, (long long)options);
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

void* memcpy(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int puts(const char* s) {
    write(1, s, strlen(s));
    return 0;
}

int socket(int domain, int type, int protocol) {
    return (int)syscall3(16, domain, type, protocol);
}

int connect(int sockfd, const struct sockaddr* addr, int addrlen) {
    return (int)syscall3(17, sockfd, (long long)addr, addrlen);
}

int send(int sockfd, const void* buf, size_t len, int flags) {
    return (int)syscall4(18, sockfd, (long long)buf, len, flags);
}

int recv(int sockfd, void* buf, size_t len, int flags) {
    return (int)syscall4(19, sockfd, (long long)buf, len, flags);
}
