/* ls — list files in the initrd using SYS_GETDENT. */
#include "lib.h"

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    char name[64];
    for (unsigned i = 0; ; i++) {
        int n = sys_getdent(i, name, sizeof(name));
        if (!n) break;
        puts(name);
    }
    sys_exit();
}
