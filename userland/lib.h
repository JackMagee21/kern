/*
 * userland/lib.h — tiny libc for Kern 2.0 user programs.
 *
 * Include this in every userland .c file instead of any system headers.
 * All functions are defined inline / static so there is no separate .c to link.
 *
 * Syscall ABI (int 0x80):
 *   eax = number, ebx/ecx/edx = args 1-3, return value in eax.
 */
#ifndef LIB_H
#define LIB_H

typedef unsigned int   uint32_t;
typedef signed   int   int32_t;
typedef unsigned int   size_t;

/* ── Syscall numbers ────────────────────────────────────────────────────── */
#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_OPEN    2
#define SYS_READ    3
#define SYS_CLOSE   4
#define SYS_GETPID  5
#define SYS_SBRK    6
#define SYS_FORK    7
#define SYS_WAITPID 8
#define SYS_EXEC    9
#define SYS_SLEEP   10
#define SYS_PIPE    11
#define SYS_DUP2    12
#define SYS_GETDENT 13
#define SYS_KILL    14
#define SYS_SIGNAL  15
#define SYS_SETFG   16
#define SYS_OPEN2   17
#define SYS_UNLINK  18
#define SYS_LSEEK   19
#define SYS_STAT    20
#define SYS_MKDIR   21
#define SYS_CHDIR   22
#define SYS_GETCWD  23

/* Signal numbers */
#define SIGINT   2
#define SIGKILL  9
#define SIGPIPE 13
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20

/* Signal dispositions */
#define SIG_DFL 0u
#define SIG_IGN 1u

/* open2 flags */
#define O_RDONLY  0
#define O_WRONLY  1
#define O_CREAT   0x40
#define O_APPEND  0x400

/* lseek whence values */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Returned by sys_waitpid when child was stopped (not exited). */
#define WAIT_STOPPED 0x100

/* Stat structure (must match kernel vfs_stat_t). */
typedef struct { uint32_t size; uint32_t type; } stat_t;

/* ── Raw syscall wrappers ───────────────────────────────────────────────── */

static inline void sys_exit(void) {
    __asm__ volatile ("int $0x80" :: "a"(SYS_EXIT), "b"(0));
}
static inline void sys_exit_code(int code) {
    __asm__ volatile ("int $0x80" :: "a"(SYS_EXIT), "b"(code));
}

static inline int sys_write(int fd, const void *buf, unsigned len) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_WRITE), "b"(fd), "c"(buf), "d"(len) : "memory");
    return ret;
}

static inline int sys_read(int fd, void *buf, unsigned len) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_READ), "b"(fd), "c"(buf), "d"(len) : "memory");
    return ret;
}

static inline int sys_open(const char *path) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_OPEN), "b"(path) : "memory");
    return ret;
}

static inline void sys_close(int fd) {
    __asm__ volatile ("int $0x80" :: "a"(SYS_CLOSE), "b"(fd));
}

static inline unsigned sys_getpid(void) {
    unsigned ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_GETPID));
    return ret;
}

static inline void *sys_sbrk(int increment) {
    void *ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_SBRK), "b"(increment) : "memory");
    return ret;
}

static inline int sys_fork(void) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_FORK) : "memory");
    return ret;
}

static inline int sys_waitpid(unsigned pid) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_WAITPID), "b"(pid) : "memory");
    return ret;
}

static inline int sys_exec(const char *path, const char *cmdline) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_EXEC), "b"(path), "c"(cmdline) : "memory");
    return ret;
}

static inline int sys_pipe(int fds[2]) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_PIPE), "b"(fds) : "memory");
    return ret;
}

static inline int sys_dup2(int old_fd, int new_fd) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_DUP2), "b"(old_fd), "c"(new_fd) : "memory");
    return ret;
}

static inline int sys_getdent(unsigned idx, char *buf, unsigned bufsz) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_GETDENT), "b"(idx), "c"(buf), "d"(bufsz) : "memory");
    return ret;
}

static inline int sys_kill(unsigned pid, int sig) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_KILL), "b"(pid), "c"(sig) : "memory");
    return ret;
}

static inline unsigned sys_signal(int sig, unsigned disp) {
    unsigned ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_SIGNAL), "b"(sig), "c"(disp) : "memory");
    return ret;
}

static inline int sys_setfg(unsigned pid) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_SETFG), "b"(pid) : "memory");
    return ret;
}

static inline int sys_open2(const char *path, int flags) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_OPEN2), "b"(path), "c"(flags) : "memory");
    return ret;
}

static inline void sys_sleep(unsigned ms) {
    __asm__ volatile ("int $0x80" :: "a"(SYS_SLEEP), "b"(ms) : "memory");
}

static inline int sys_unlink(const char *path) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_UNLINK), "b"(path) : "memory");
    return ret;
}

static inline int sys_lseek(int fd, int offset, int whence) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_LSEEK), "b"(fd), "c"(offset), "d"(whence) : "memory");
    return ret;
}

static inline int sys_stat(const char *path, stat_t *buf) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_STAT), "b"(path), "c"(buf) : "memory");
    return ret;
}

static inline int sys_mkdir(const char *path) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_MKDIR), "b"(path) : "memory");
    return ret;
}

static inline int sys_chdir(const char *path) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret) : "0"(SYS_CHDIR), "b"(path) : "memory");
    return ret;
}

static inline void sys_getcwd(char *buf, unsigned size) {
    __asm__ volatile ("int $0x80"
        :: "a"(SYS_GETCWD), "b"(buf), "c"(size) : "memory");
}

/* ── Strings ────────────────────────────────────────────────────────────── */

static inline size_t strlen(const char *s) {
    size_t n = 0; while (*s++) n++; return n;
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

static inline char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    while (i < n && src[i]) { dst[i] = src[i]; i++; }
    while (i < n) dst[i++] = '\0';
    return dst;
}

static inline char *strcpy(char *dst, const char *src) {
    char *d = dst; while ((*d++ = *src++)); return dst;
}

static inline void *memset(void *dst, int c, size_t n) {
    unsigned char *p = (unsigned char *)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

static inline void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

/* ── I/O ────────────────────────────────────────────────────────────────── */

static inline void putchar(char c) { sys_write(1, &c, 1); }

static inline void puts(const char *s) {
    sys_write(1, s, strlen(s));
    putchar('\n');
}

static inline int getline(char *buf, int len) {
    int n = sys_read(0, buf, (unsigned)(len - 1));
    if (n > 0 && buf[n-1] == '\n') n--;
    buf[n] = '\0';
    return n;
}

/* ── printf / snprintf / fprintf ────────────────────────────────────────── */

/* Write unsigned n (in given base) into buf with optional width/padding.
 * Returns number of characters written (not including NUL). */
static inline int _itoa_u(unsigned n, unsigned base, char *buf,
                           int width, int zero_pad, int left_align) {
    static const char hex[] = "0123456789abcdef";
    char tmp[32]; int dlen = 0;
    if (!n) { tmp[dlen++] = '0'; }
    else while (n) { tmp[dlen++] = hex[n % base]; n /= base; }
    int pad  = (width > dlen) ? width - dlen : 0;
    int pos  = 0;
    char pc  = (zero_pad && !left_align) ? '0' : ' ';
    if (!left_align) while (pad--) buf[pos++] = pc;
    for (int i = dlen - 1; i >= 0; i--) buf[pos++] = tmp[i];
    if  (left_align) while (pad--) buf[pos++] = ' ';
    buf[pos] = '\0';
    return pos;
}

/*
 * Core formatted-print routine.  Writes at most max-1 chars into dst and
 * NUL-terminates.  Returns number of chars written (excluding NUL).
 *
 * Supported conversions: %c %s %d %u %x %p %%
 * Flags / width: -, 0, and an integer width before the conversion letter.
 */
static inline uint32_t _vsnprintf(char *dst, uint32_t max,
                                   const char *fmt, __builtin_va_list ap) {
    uint32_t pos = 0;
#define _P(c) do { if (pos + 1 < max) dst[pos++] = (char)(c); } while(0)
    while (*fmt) {
        if (*fmt != '%') { _P(*fmt++); continue; }
        fmt++;  /* skip '%' */

        int left = 0, zero = 0, width = 0;
        while (*fmt == '-') { left = 1; fmt++; }
        while (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '1' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

        char tbuf[48]; int tlen = 0;
        switch (*fmt++) {
        case 'c':
            tbuf[0] = (char)__builtin_va_arg(ap, int); tlen = 1; break;
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            int slen = (int)strlen(s);
            int pad  = (width > slen) ? width - slen : 0;
            if (!left) while (pad--) _P(' ');
            while (*s) _P(*s++);
            if  (left) while (pad--) _P(' ');
            continue;
        }
        case 'd': {
            int n   = __builtin_va_arg(ap, int);
            int neg = (n < 0);
            unsigned u = neg ? (unsigned)(-n) : (unsigned)n;
            char nb[32]; int nl = 0;
            if (!u) nb[nl++] = '0';
            else while (u) { nb[nl++] = '0' + (int)(u % 10); u /= 10; }
            int total = nl + (neg ? 1 : 0);
            int pad   = (width > total) ? width - total : 0;
            if (!left && !zero) while (pad--) _P(' ');
            if (neg) _P('-');
            if (!left && zero) while (pad--) _P('0');
            for (int i = nl - 1; i >= 0; i--) _P(nb[i]);
            if (left) while (pad--) _P(' ');
            continue;
        }
        case 'u':
            tlen = _itoa_u(__builtin_va_arg(ap,unsigned), 10, tbuf, width, zero&&!left, left);
            break;
        case 'x':
            tlen = _itoa_u(__builtin_va_arg(ap,unsigned), 16, tbuf, width, zero&&!left, left);
            break;
        case 'p':
            tlen = _itoa_u((unsigned)__builtin_va_arg(ap,void*), 16, tbuf, 8, 1, 0);
            break;
        case '%': _P('%'); continue;
        default:  _P('%'); _P(*(fmt-1)); continue;
        }
        for (int i = 0; i < tlen; i++) _P(tbuf[i]);
    }
    if (max > 0) dst[pos] = '\0';
#undef _P
    return pos;
}

static inline int snprintf(char *buf, uint32_t sz, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = (int)_vsnprintf(buf, sz, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

static inline void printf(const char *fmt, ...) {
    char buf[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = (int)_vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    sys_write(1, buf, (unsigned)n);
}

static inline void fprintf(int fd, const char *fmt, ...) {
    char buf[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = (int)_vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    sys_write(fd, buf, (unsigned)n);
}

/* ── malloc / free (sbrk-based bump+freelist allocator) ─────────────────── */

#define _MALLOC_MAGIC 0xFEED1234u

typedef struct _blk {
    unsigned        magic;
    unsigned        size;
    unsigned        free;
    struct _blk    *next;
} _blk_t;

static _blk_t *_heap_head = (void *)0;
static void   *_heap_end  = (void *)0;

static inline void *malloc(size_t size) {
    if (!size) return (void *)0;
    size = (size + 7u) & ~7u;

    if (!_heap_head) {
        _heap_head = (void *)0;
        _heap_end  = sys_sbrk(0);
    }

    for (_blk_t *b = _heap_head; b; b = b->next) {
        if (b->free && b->size >= size && b->magic == _MALLOC_MAGIC) {
            b->free = 0;
            return (char *)b + sizeof(_blk_t);
        }
    }

    unsigned total = sizeof(_blk_t) + size;
    void *old = sys_sbrk((int)total);
    if ((unsigned)old == (unsigned)-1) return (void *)0;

    _blk_t *b = (_blk_t *)old;
    b->magic = _MALLOC_MAGIC;
    b->size  = size;
    b->free  = 0;
    b->next  = (void *)0;

    _heap_end = (char *)old + total;

    if (!_heap_head) { _heap_head = b; }
    else {
        _blk_t *t = _heap_head;
        while (t->next) t = t->next;
        t->next = b;
    }
    return (char *)b + sizeof(_blk_t);
}

static inline void free(void *ptr) {
    if (!ptr) return;
    _blk_t *b = (_blk_t *)((char *)ptr - sizeof(_blk_t));
    if (b->magic == _MALLOC_MAGIC) b->free = 1;
}

static inline void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    _blk_t *b = (_blk_t *)((char *)ptr - sizeof(_blk_t));
    if (b->magic != _MALLOC_MAGIC) return (void *)0;
    if (b->size >= size) return ptr;
    void *n = malloc(size);
    if (n) { memcpy(n, ptr, b->size); free(ptr); }
    return n;
}

#endif /* LIB_H */
