/* grep — print lines containing a literal string pattern. */
#include "lib.h"

#define BUFSZ 512

/* Returns 1 if needle appears in haystack. */
static int contains(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (!nlen) return 1;
    for (; *haystack; haystack++) {
        size_t i = 0;
        while (i < nlen && haystack[i] == needle[i]) i++;
        if (i == nlen) return 1;
    }
    return 0;
}

/* Process one fd: print lines that contain pattern. */
static void grep_fd(int fd, const char *pattern) {
    char buf[BUFSZ];
    char line[BUFSZ];
    int  lpos = 0;
    int  n;

    while ((n = sys_read(fd, buf, BUFSZ)) > 0) {
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || lpos == BUFSZ - 1) {
                line[lpos] = '\0';
                if (contains(line, pattern)) {
                    sys_write(1, line, (unsigned)lpos);
                    putchar('\n');
                }
                lpos = 0;
            } else {
                line[lpos++] = c;
            }
        }
    }
    /* Flush any partial last line (no trailing newline). */
    if (lpos > 0) {
        line[lpos] = '\0';
        if (contains(line, pattern)) {
            sys_write(1, line, (unsigned)lpos);
            putchar('\n');
        }
    }
}

void _start(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: grep <pattern> [file ...]");
        sys_exit();
    }

    const char *pattern = argv[1];

    if (argc < 3) {
        grep_fd(0, pattern);
    } else {
        for (int i = 2; i < argc; i++) {
            int fd = sys_open(argv[i]);
            if (fd < 0) {
                sys_write(1, "grep: cannot open: ", 19);
                puts(argv[i]);
                continue;
            }
            grep_fd(fd, pattern);
            sys_close(fd);
        }
    }
    sys_exit();
}
