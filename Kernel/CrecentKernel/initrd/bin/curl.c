#include "../include/libc.h"

#define NULL ((void*)0)

int main() {
    puts("--- AmazingOS Curl Utility ---\n");

    int sock = socket(2, 1, 6); // AF_INET = 2, SOCK_STREAM = 1, IPPROTO_TCP = 6
    if (sock < 0) {
        puts("Error: socket creation failed!\n");
        exit(1);
    }

    struct sockaddr_in addr;
    addr.sin_family = 2;
    addr.sin_port = 0x5000;      // Port 80 (big-endian 0x5000)
    addr.sin_addr = 0x0202000A;  // IP 10.0.2.2 (big-endian 0x0202000A)

    puts("Connecting to host server (10.0.2.2:80)...\n");
    if (connect(sock, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
        puts("Error: connection failed!\n");
        close(sock);
        exit(1);
    }

    puts("Connected successfully! Sending HTTP GET request...\n");
    const char* request = 
        "GET /test.txt HTTP/1.1\r\n"
        "Host: 10.0.2.2\r\n"
        "Connection: close\r\n\r\n";

    int req_len = 0;
    while (request[req_len]) req_len++;

    if (send(sock, request, req_len, 0) < 0) {
        puts("Error: failed to send HTTP request!\n");
        close(sock);
        exit(1);
    }

    puts("HTTP request sent! Awaiting response...\n\n");

    char buffer[256];
    int bytes;
    while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';
        puts(buffer);
    }

    puts("\n\nConnection closed by host.\n");
    close(sock);
    exit(0);
}
