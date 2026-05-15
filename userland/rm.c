/* rm — remove a file via SYS_UNLINK. */
#include "lib.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: rm <file>");
        sys_exit();
    }
    for (int i = 1; i < argc; i++) {
        if (sys_unlink(argv[i]) < 0) {
            puts("rm: cannot remove '");
            puts(argv[i]);
            puts("'\n");
        }
    }
    sys_exit();
}
