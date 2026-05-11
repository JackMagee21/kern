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

static void print_int(int32_t n) {
    if (n < 0) { terminal_putchar('-'); n = -n; }
    print_uint((uint32_t)n, 10, "0123456789");
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') { terminal_putchar(*fmt); continue; }
        switch (*++fmt) {
            case 'c': terminal_putchar((char)va_arg(ap, int)); break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                while (*s) terminal_putchar(*s++);
                break;
            }
            case 'd': print_int(va_arg(ap, int32_t)); break;
            case 'u': print_uint(va_arg(ap, uint32_t), 10, "0123456789"); break;
            case 'x': print_uint(va_arg(ap, uint32_t), 16, "0123456789abcdef"); break;
            case 'X': print_uint(va_arg(ap, uint32_t), 16, "0123456789ABCDEF"); break;
            case '%': terminal_putchar('%'); break;
            default:  terminal_putchar('%'); terminal_putchar(*fmt); break;
        }
    }

    va_end(ap);
}
