/* mkdir — create a directory via SYS_MKDIR. */
#include "lib.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: mkdir <dir>");
        sys_exit();
    }
    for (int i = 1; i < argc; i++) {
        if (sys_mkdir(argv[i]) < 0) {
            puts("mkdir: cannot create '");
            puts(argv[i]);
            puts("'\n");
        }
    }
    sys_exit();
}
