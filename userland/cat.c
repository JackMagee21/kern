/*
 * cat — concatenate files to stdout.
 * Usage: cat <file> [file ...]
 * With no arguments, copies stdin to stdout.
 */
#include "lib.h"

#define BUFSZ 512

static void cat_fd(int fd) {
    char buf[BUFSZ];
    int  n;
    while ((n = sys_read(fd, buf, BUFSZ)) > 0)
        sys_write(1, buf, (unsigned)n);
}

void _start(int argc, char **argv) {
    if (argc < 2) {
        cat_fd(0); /* stdin */
    } else {
        for (int i = 1; i < argc; i++) {
            int fd = sys_open(argv[i]);
            if (fd < 0) {
                sys_write(1, "cat: cannot open: ", 18);
                puts(argv[i]);
                continue;
            }
            cat_fd(fd);
            sys_close(fd);
        }
    }
    sys_exit();
}
