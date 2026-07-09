#include "../include/libc.h"

#define NULL ((void*)0)

int main(int argc, char* argv[]) {
    if (argc < 2) {
        puts("Usage: cat <file_path>\n");
        return 1;
    }

    int fd = open(argv[1], 0); // 0 = O_RDONLY
    if (fd < 0) {
        puts("cat: error opening file: ");
        puts(argv[1]);
        puts("\n");
        return 1;
    }

    char buf[256];
    int bytes_read;
    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, bytes_read);
    }

    close(fd);
    return 0;
}
