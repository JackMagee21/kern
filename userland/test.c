/*
 * Minimal ring-3 test program — no libc.
 *
 * Syscall ABI (int 0x80):
 *   eax = number, ebx/ecx/edx = args, return value in eax.
 */

#define SYS_EXIT   0
#define SYS_WRITE  1   /* write(fd=ebx, buf=ecx, len=edx) */
#define SYS_GETPID 5

static inline void sys_write(int fd, const char *buf, unsigned len) {
    __asm__ volatile (
        "int $0x80"
        :: "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(len)
        : "memory"
    );
}

static inline unsigned sys_getpid(void) {
    unsigned pid;
    __asm__ volatile (
        "int $0x80"
        : "=a"(pid)
        : "0"(SYS_GETPID)
    );
    return pid;
}

static inline void sys_exit(void) {
    __asm__ volatile ("int $0x80" :: "a"(SYS_EXIT));
}

static int utoa(unsigned n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[12]; int i = 0, len = 0;
    while (n) { tmp[i++] = '0' + (n % 10); n /= 10; }
    for (int j = i - 1; j >= 0; j--) buf[len++] = tmp[j];
    buf[len] = '\0';
    return len;
}

void _start(void) {
    const char msg1[] = "Hello from user space!\n";
    sys_write(1, msg1, sizeof(msg1) - 1);

    const char msg2[] = "My PID is: ";
    sys_write(1, msg2, sizeof(msg2) - 1);

    char pidbuf[12];
    int plen = utoa(sys_getpid(), pidbuf);
    sys_write(1, pidbuf, (unsigned)plen);
    sys_write(1, "\n", 1);

    const char msg3[] = "Exiting from user space.\n";
    sys_write(1, msg3, sizeof(msg3) - 1);

    sys_exit();

    for (;;) __asm__ volatile ("hlt");
}
