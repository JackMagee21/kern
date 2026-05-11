#include "vga.h"

static size_t    terminal_row;
static size_t    terminal_col;
static uint8_t   terminal_color;
static uint16_t *terminal_buffer;

static inline uint8_t vga_make_color(vga_color fg, vga_color bg) {
    return fg | (bg << 4);
}

static inline uint16_t vga_make_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void terminal_scroll(void) {
    for (size_t r = 1; r < VGA_HEIGHT; r++)
        for (size_t c = 0; c < VGA_WIDTH; c++)
            terminal_buffer[(r - 1) * VGA_WIDTH + c] =
                terminal_buffer[r * VGA_WIDTH + c];

    for (size_t c = 0; c < VGA_WIDTH; c++)
        terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + c] =
            vga_make_entry(' ', terminal_color);

    terminal_row = VGA_HEIGHT - 1;
}

void terminal_init(void) {
    terminal_row    = 0;
    terminal_col    = 0;
    terminal_color  = vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_buffer = VGA_MEMORY;

    for (size_t r = 0; r < VGA_HEIGHT; r++)
        for (size_t c = 0; c < VGA_WIDTH; c++)
            terminal_buffer[r * VGA_WIDTH + c] =
                vga_make_entry(' ', terminal_color);
}

void terminal_clear(void) {
    for (size_t r = 0; r < VGA_HEIGHT; r++)
        for (size_t c = 0; c < VGA_WIDTH; c++)
            terminal_buffer[r * VGA_WIDTH + c] =
                vga_make_entry(' ', terminal_color);
    terminal_row = 0;
    terminal_col = 0;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        terminal_col = 0;
        if (++terminal_row == VGA_HEIGHT)
            terminal_scroll();
        return;
    }

    if (c == '\b') {
        if (terminal_col > 0) {
            terminal_col--;
        } else if (terminal_row > 0) {
            terminal_row--;
            terminal_col = VGA_WIDTH - 1;
        }
        return;
    }

    terminal_buffer[terminal_row * VGA_WIDTH + terminal_col] =
        vga_make_entry(c, terminal_color);

    if (++terminal_col == VGA_WIDTH) {
        terminal_col = 0;
        if (++terminal_row == VGA_HEIGHT)
            terminal_scroll();
    }
}

void terminal_print(const char *str) {
    for (size_t i = 0; str[i] != '\0'; i++)
        terminal_putchar(str[i]);
}

void terminal_set_color(vga_color fg, vga_color bg) {
    terminal_color = vga_make_color(fg, bg);
}