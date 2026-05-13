/* echo — print arguments separated by spaces, followed by a newline. */
#include "lib.h"

void _start(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar(' ');
        sys_write(1, argv[i], strlen(argv[i]));
    }
    putchar('\n');
    sys_exit();
}
