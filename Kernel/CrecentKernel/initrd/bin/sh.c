#include "../include/libc.h"

#define NULL ((void*)0)

static void trim(char* str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || str[len - 1] == ' ')) {
        str[len - 1] = '\0';
        len--;
    }
}

int main() {
    puts("========================================\n");
    puts("       AmazingOS Interactive Shell       \n");
    puts("========================================\n\n");

    char input_buf[128];
    char* argv[16];

    while (1) {
        puts("sh$ ");
        
        int bytes = read(0, input_buf, sizeof(input_buf) - 1);
        if (bytes <= 0) {
            continue;
        }
        input_buf[bytes] = '\0';
        trim(input_buf);

        if (strlen(input_buf) == 0) {
            continue;
        }

        int argc = 0;
        char* token = input_buf;
        while (*token && argc < 15) {
            while (*token == ' ') {
                *token = '\0';
                token++;
            }
            if (*token == '\0') break;

            argv[argc++] = token;

            while (*token && *token != ' ') {
                token++;
            }
        }
        argv[argc] = NULL;

        if (argc == 0) {
            continue;
        }

        if (strcmp(argv[0], "exit") == 0) {
            puts("Exiting shell...\n");
            exit(0);
        } else if (strcmp(argv[0], "help") == 0) {
            puts("Available commands:\n");
            puts("  help       - Display this help message\n");
            puts("  exit       - Terminate the shell\n");
            puts("  ls         - List files in /tar\n");
            puts("  [program]  - Run an executable file (e.g., /tar/bin/test_app)\n");
        } else if (strcmp(argv[0], "ls") == 0) {
            puts("Contents of directory /tar:\n");
            puts("  bin/test_app\n");
            puts("  docs/info.txt\n");
            puts("  hello.txt\n");
            puts("  inter.otf\n");
        } else {
            char cmd_path[64];
            cmd_path[0] = '\0';

            if (argv[0][0] != '/') {
                memcpy(cmd_path, "/tar/bin/", 9);
                memcpy(cmd_path + 9, argv[0], strlen(argv[0]) + 1);
            } else {
                memcpy(cmd_path, argv[0], strlen(argv[0]) + 1);
            }

            int pid = fork();
            if (pid < 0) {
                puts("sh: failed to fork child process!\n");
            } else if (pid == 0) {
                execve(cmd_path, argv, NULL);
                puts("sh: command not found: ");
                puts(argv[0]);
                puts("\n");
                exit(1);
            } else {
                int status = 0;
                waitpid(pid, &status, 0);
            }
        }
    }
    return 0;
}
