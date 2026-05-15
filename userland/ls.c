/* ls — list files in the current directory. */
#include "lib.h"

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    char name[64];
    stat_t st;
    for (unsigned i = 0; ; i++) {
        int n = sys_getdent(i, name, sizeof(name));
        if (!n) break;
        if (sys_stat(name, &st) == 0) {
            if (st.type == 1)
                printf("%-24s [dir]\n", name);
            else
                printf("%-24s %u\n", name, st.size);
        } else {
            puts(name);
            puts("\n");
        }
    }
    sys_exit();
}
