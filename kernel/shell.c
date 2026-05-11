#include "shell.h"
#include "printf.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include <stddef.h>

#define LINE_MAX 256
#define PROMPT   "kern> "

/* ── string helpers (no libc) ─────────────────────────────────────────── */

static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Returns pointer past the leading word in s, or s if s is empty. */
static const char *skip_word(const char *s) {
    while (*s && *s != ' ') s++;
    while (*s == ' ') s++;
    return s;
}

/* ── line reader ───────────────────────────────────────────────────────── */

static void readline(char *buf, size_t max) {
    size_t len = 0;
    for (;;) {
        char c = keyboard_getchar();

        if (c == '\n') {
            terminal_putchar('\n');
            break;
        }

        if (c == '\b') {
            if (len > 0) {
                len--;
                /* Erase the character on screen: back, space, back */
                terminal_putchar('\b');
                terminal_putchar(' ');
                terminal_putchar('\b');
            }
            continue;
        }

        if (len < max - 1) {
            buf[len++] = c;
            terminal_putchar(c);
        }
    }
    buf[len] = '\0';
}

/* ── built-in commands ─────────────────────────────────────────────────── */

static void cmd_help(const char *args) {
    (void)args;
    terminal_print("Commands:\n");
    terminal_print("  help          show this message\n");
    terminal_print("  clear         clear the screen\n");
    terminal_print("  echo <text>   print text\n");
    terminal_print("  ticks         milliseconds since boot\n");
    terminal_print("  halt          stop the CPU\n");
}

static void cmd_clear(const char *args) {
    (void)args;
    terminal_clear();
}

static void cmd_echo(const char *args) {
    terminal_print(args);
    terminal_putchar('\n');
}

static void cmd_ticks(const char *args) {
    (void)args;
    kprintf("%u ms since boot\n", timer_get_ticks());
}

static void cmd_halt(const char *args) {
    (void)args;
    terminal_print("Halting.\n");
    __asm__ volatile ("cli; hlt");
}

/* ── command table ─────────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    void (*fn)(const char *args);
} command_t;

static const command_t commands[] = {
    { "help",  cmd_help  },
    { "clear", cmd_clear },
    { "echo",  cmd_echo  },
    { "ticks", cmd_ticks },
    { "halt",  cmd_halt  },
};
#define CMD_COUNT ((size_t)(sizeof(commands) / sizeof(commands[0])))

/* ── dispatch ──────────────────────────────────────────────────────────── */

static void dispatch(const char *line) {
    if (!*line) return;

    /* Copy the first word into a null-terminated buffer for comparison. */
    char name[32] = {0};
    size_t i = 0;
    while (line[i] && line[i] != ' ' && i < sizeof(name) - 1) {
        name[i] = line[i];
        i++;
    }

    const char *args = skip_word(line);

    for (size_t j = 0; j < CMD_COUNT; j++) {
        if (kstrcmp(name, commands[j].name) == 0) {
            commands[j].fn(args);
            return;
        }
    }

    kprintf("Unknown command: %s\n", name);
    terminal_print("Type 'help' for a list of commands.\n");
}

/* ── main loop ─────────────────────────────────────────────────────────── */

void shell_run(void) {
    char line[LINE_MAX];

    terminal_print("\nKern 2.0 -- type 'help' for commands.\n\n");

    for (;;) {
        terminal_print(PROMPT);
        readline(line, sizeof(line));
        dispatch(line);
    }
}
