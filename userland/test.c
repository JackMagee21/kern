#include "lib.h"

void _start(void) {
    printf("Hello from user space!\n");
    printf("My PID is: %u\n", sys_getpid());

    /* Test malloc/free */
    char *buf = (char *)malloc(64);
    if (buf) {
        strcpy(buf, "malloc works!");
        printf("heap: %s\n", buf);
        free(buf);
    } else {
        puts("malloc failed");
    }

    /* Test stdin — ask for name and echo it back */
    puts("Enter your name: ");
    char line[64];
    int n = getline(line, sizeof(line));
    if (n > 0)
        printf("Hello, %s!\n", line);
    else
        puts("(no input)");

    puts("Exiting.");
    sys_exit();
    for (;;) __asm__ volatile ("hlt");
}
