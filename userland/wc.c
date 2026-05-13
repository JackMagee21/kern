/* wc — count lines, words, and characters from stdin or files. */
#include "lib.h"

#define BUFSZ 512

static void count(int fd, unsigned *lines, unsigned *words, unsigned *chars) {
    char buf[BUFSZ];
    int in_word = 0;
    int n;
    while ((n = sys_read(fd, buf, BUFSZ)) > 0) {
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            (*chars)++;
            if (c == '\n') (*lines)++;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                (*words)++;
            }
        }
    }
}

void _start(int argc, char **argv) {
    unsigned lines = 0, words = 0, chars = 0;

    if (argc < 2) {
        count(0, &lines, &words, &chars);
    } else {
        for (int i = 1; i < argc; i++) {
            int fd = sys_open(argv[i]);
            if (fd < 0) {
                sys_write(1, "wc: cannot open: ", 17);
                puts(argv[i]);
                continue;
            }
            count(fd, &lines, &words, &chars);
            sys_close(fd);
        }
    }

    printf("%u %u %u\n", lines, words, chars);
    sys_exit();
}
