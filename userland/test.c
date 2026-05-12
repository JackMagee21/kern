/*
 * Minimal ring-3 test program.
 * Uses int 0x80 directly — no libc.
 */

static const char msg[] = "Hello from user space!\n";

void _start(void) {
    /* SYS_WRITE (1): ebx = string pointer */
    __asm__ volatile (
        "mov $1, %%eax\n"
        "mov %0, %%ebx\n"
        "int $0x80\n"
        :: "r"(msg) : "eax", "ebx"
    );

    /* SYS_EXIT (0) */
    __asm__ volatile ("xor %%eax, %%eax; int $0x80" ::: "eax");

    /* Should never reach here. */
    for (;;) __asm__ volatile ("hlt");
}
