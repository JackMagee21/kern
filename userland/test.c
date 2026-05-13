#include "lib.h"

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("=== fork/waitpid test ===\n");
    printf("Parent PID: %u\n", sys_getpid());

    int child = sys_fork();
    if (child < 0) {
        puts("fork() failed");
        sys_exit();
    }

    if (child == 0) {
        /* Child */
        printf("Child PID:  %u  (fork returned %d)\n", sys_getpid(), child);
        sys_sleep(50);
        puts("Child: done sleeping, exiting.");
        sys_exit();
    }

    /* Parent */
    printf("Parent: child PID is %d, waiting...\n", child);
    sys_waitpid((unsigned)child);
    puts("Parent: child has exited.");

    /* Test malloc in parent (sbrk / heap) */
    char *buf = (char *)malloc(128);
    if (buf) {
        strcpy(buf, "heap ok");
        printf("Parent: %s\n", buf);
        free(buf);
    }

    puts("Parent: exiting.");
    sys_exit();
    for (;;) __asm__ volatile ("hlt");
}
