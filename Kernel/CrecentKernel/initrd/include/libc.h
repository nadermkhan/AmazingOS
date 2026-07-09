#pragma once

#define EFAULT  14
#define ENOENT  2
#define EBADF   9
#define EMFILE  24
#define ECHILD  10

typedef unsigned long long size_t;

// Syscall wrappers
int open(const char* path, int flags);
int read(int fd, void* buf, size_t count);
int write(int fd, const void* buf, size_t count);
int close(int fd);
int fork();
int execve(const char* path, char* const argv[], char* const envp[]);
void exit(int status);
int waitpid(int pid, int* wstatus, int options);

// Utility functions
size_t strlen(const char* s);
void* memcpy(void* dest, const void* src, size_t n);
int strcmp(const char* s1, const char* s2);
int puts(const char* s);

// Sockets structures
struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    unsigned int sin_addr;
    char sin_zero[8];
};

// Sockets syscall wrappers
int socket(int domain, int type, int protocol);
int connect(int sockfd, const struct sockaddr* addr, int addrlen);
int send(int sockfd, const void* buf, size_t len, int flags);
int recv(int sockfd, void* buf, size_t len, int flags);
