#include "printf.h"
#include "vga.h"
#include <stdarg.h>
#include <stdint.h>

static void print_uint(uint32_t n, uint32_t base, const char *digits) {
    char buf[32];
    int i = 0;
    if (n == 0) { terminal_putchar('0'); return; }
    while (n) { buf[i++] = digits[n % base]; n /= base; }
    while (i--) terminal_putchar(buf[i]);
}

static int uint_len(uint32_t n, uint32_t base) {
    if (n == 0) return 1;
    int len = 0;
    while (n) { n /= base; len++; }
    return len;
}

static void print_int(int32_t n) {
    if (n < 0) { terminal_putchar('-'); n = -n; }
    print_uint((uint32_t)n, 10, "0123456789");
}

static void pad(int count, char c) {
    while (count-- > 0) terminal_putchar(c);
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') { terminal_putchar(*fmt); continue; }
        ++fmt;

        /* Parse flags */
        int left = 0, zero = 0;
        while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '0' || *fmt == '#') {
            if (*fmt == '-') left = 1;
            if (*fmt == '0') zero = 1;
            ++fmt;
        }

        /* Parse width */
        int width = 0;
        while (*fmt >= '1' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); ++fmt; }

        /* Skip precision */
        if (*fmt == '.') { ++fmt; while (*fmt >= '0' && *fmt <= '9') ++fmt; }

        /* Skip length modifiers */
        while (*fmt == 'l' || *fmt == 'h') ++fmt;

        char pad_char = (zero && !left) ? '0' : ' ';

        switch (*fmt) {
            case 'c': {
                char c = (char)va_arg(ap, int);
                if (!left) pad(width - 1, ' ');
                terminal_putchar(c);
                if (left)  pad(width - 1, ' ');
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                int slen = 0;
                for (const char *p = s; *p; p++) slen++;
                if (!left) pad(width - slen, ' ');
                while (*s) terminal_putchar(*s++);
                if (left)  pad(width - slen, ' ');
                break;
            }
            case 'd': {
                int32_t n = va_arg(ap, int32_t);
                int sign = (n < 0) ? 1 : 0;
                uint32_t u = sign ? (uint32_t)(-n) : (uint32_t)n;
                int len = uint_len(u, 10) + sign;
                if (!left) pad(width - len, pad_char);
                print_int(n);
                if (left)  pad(width - len, ' ');
                break;
            }
            case 'u': {
                uint32_t n = va_arg(ap, uint32_t);
                int len = uint_len(n, 10);
                if (!left) pad(width - len, pad_char);
                print_uint(n, 10, "0123456789");
                if (left)  pad(width - len, ' ');
                break;
            }
            case 'x': {
                uint32_t n = va_arg(ap, uint32_t);
                int len = uint_len(n, 16);
                if (!left) pad(width - len, pad_char);
                print_uint(n, 16, "0123456789abcdef");
                if (left)  pad(width - len, ' ');
                break;
            }
            case 'X': {
                uint32_t n = va_arg(ap, uint32_t);
                int len = uint_len(n, 16);
                if (!left) pad(width - len, pad_char);
                print_uint(n, 16, "0123456789ABCDEF");
                if (left)  pad(width - len, ' ');
                break;
            }
            case '%': terminal_putchar('%'); break;
            default:  terminal_putchar('%'); terminal_putchar(*fmt); break;
        }
    }

    va_end(ap);
}
