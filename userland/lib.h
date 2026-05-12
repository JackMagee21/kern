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
#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_OPEN   2
#define SYS_READ   3
#define SYS_CLOSE  4
#define SYS_GETPID 5
#define SYS_SBRK   6

/* ── Raw syscall wrappers ───────────────────────────────────────────────── */

static inline void sys_exit(void) {
    __asm__ volatile ("int $0x80" :: "a"(SYS_EXIT));
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

/* ── Strings ────────────────────────────────────────────────────────────── */

static inline size_t strlen(const char *s) {
    size_t n = 0; while (*s++) n++; return n;
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
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

/* Read one line into buf (up to len-1 chars), null-terminate. Returns length. */
static inline int getline(char *buf, int len) {
    int n = sys_read(0, buf, (unsigned)(len - 1));
    if (n > 0 && buf[n-1] == '\n') n--;
    buf[n] = '\0';
    return n;
}

/* ── printf ─────────────────────────────────────────────────────────────── */

static inline void _put_uint(unsigned n, unsigned base) {
    static const char hex[] = "0123456789abcdef";
    char buf[32]; int i = 0;
    if (!n) { putchar('0'); return; }
    while (n) { buf[i++] = hex[n % base]; n /= base; }
    while (i--) putchar(buf[i]);
}

/* Minimal printf: supports %c %s %d %u %x %%. No width/precision. */
static inline void printf(const char *fmt, ...) {
    /* Manual va_list using pointer arithmetic (works with -ffreestanding). */
    const char **ap = (const char **)__builtin_apply_args();
    (void)ap; /* suppress warning — we use __builtin_va_list below */

    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') { putchar(*fmt); continue; }
        switch (*++fmt) {
            case 'c': putchar((char)__builtin_va_arg(args, int)); break;
            case 's': { const char *s = __builtin_va_arg(args, const char *);
                        if (!s) s = "(null)";
                        sys_write(1, s, strlen(s)); break; }
            case 'd': { int n = __builtin_va_arg(args, int);
                        if (n < 0) { putchar('-'); _put_uint((unsigned)-n, 10); }
                        else _put_uint((unsigned)n, 10); break; }
            case 'u': _put_uint(__builtin_va_arg(args, unsigned), 10); break;
            case 'x': _put_uint(__builtin_va_arg(args, unsigned), 16); break;
            case '%': putchar('%'); break;
            default:  putchar('%'); putchar(*fmt); break;
        }
    }
    __builtin_va_end(args);
}

/* ── malloc / free (sbrk-based bump+freelist allocator) ─────────────────── */

#define _MALLOC_MAGIC 0xFEED1234u

typedef struct _blk {
    unsigned        magic;
    unsigned        size;   /* usable bytes */
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

    /* Append to list. */
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
