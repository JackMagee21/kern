#include "shell.h"
#include "printf.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "pmm.h"
#include "heap.h"
#include "task.h"
#include "scheduler.h"
#include <stddef.h>
#include <stdint.h>

#define LINE_MAX 256
#define PROMPT   "kern> "

/* ── String / parse helpers ───────────────────────────────────────────── */

static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static const char *skip_word(const char *s) {
    while (*s && *s != ' ') s++;
    while (*s == ' ')       s++;
    return s;
}

/* Parse a decimal unsigned integer from s; returns 0 on empty/non-digit. */
static uint32_t parse_u32(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}

/* ── Line reader ───────────────────────────────────────────────────────── */

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

/* ── Built-in commands ─────────────────────────────────────────────────── */

static void cmd_help(const char *args) {
    (void)args;
    terminal_print("Commands:\n");
    terminal_print("  help              show this message\n");
    terminal_print("  clear             clear the screen\n");
    terminal_print("  echo <text>       print text\n");
    terminal_print("  ticks             milliseconds since boot\n");
    terminal_print("  meminfo           physical memory and heap statistics\n");
    terminal_print("  ps                list all tasks\n");
    terminal_print("  sleep <ms>        sleep for <ms> milliseconds\n");
    terminal_print("  version           kernel version summary\n");
    terminal_print("  halt              stop the CPU\n");
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

static void cmd_meminfo(const char *args) {
    (void)args;

    size_t total  = pmm_get_total();
    size_t free_f = pmm_get_free();
    size_t used_f = total - free_f;

    terminal_print("Physical memory:\n");
    kprintf("  Total : %u KB  (%u MB)\n",
            (uint32_t)(total  * 4), (uint32_t)(total  * 4 / 1024));
    kprintf("  Used  : %u KB\n",  (uint32_t)(used_f * 4));
    kprintf("  Free  : %u KB  (%u MB)\n",
            (uint32_t)(free_f * 4), (uint32_t)(free_f * 4 / 1024));
    terminal_print("Heap:\n");
    kprintf("  Used  : %u bytes\n", (uint32_t)heap_get_used());
    kprintf("  Free  : %u bytes\n", (uint32_t)heap_get_free());
}

static void cmd_ps(const char *args) {
    (void)args;
    scheduler_print_tasks();
}

static void cmd_sleep(const char *args) {
    uint32_t ms = parse_u32(args);
    if (!ms) {
        terminal_print("Usage: sleep <ms>\n");
        return;
    }
    kprintf("Sleeping %u ms...\n", ms);
    task_sleep(ms);
    terminal_print("Awake.\n");
}

static void cmd_version(const char *args) {
    (void)args;
    terminal_print("Kern 2.0 -- x86 hobby kernel\n");
    terminal_print("  CPU:       GDT (ring-0/3), IDT, PIC, exceptions, TSS\n");
    terminal_print("  Memory:    PMM bitmap, 4 KB paging, heap (kmalloc/kfree)\n");
    terminal_print("  Devices:   VGA 80x25, PS/2 keyboard, PIT 1 kHz\n");
    terminal_print("  Process:   cooperative scheduler, task_yield/sleep\n");
}

static void cmd_halt(const char *args) {
    (void)args;
    terminal_print("Halting.\n");
    __asm__ volatile ("cli; hlt");
}

/* ── Command table ─────────────────────────────────────────────────────── */

typedef struct { const char *name; void (*fn)(const char *); } command_t;

static const command_t commands[] = {
    { "help",    cmd_help    },
    { "clear",   cmd_clear   },
    { "echo",    cmd_echo    },
    { "ticks",   cmd_ticks   },
    { "meminfo", cmd_meminfo },
    { "ps",      cmd_ps      },
    { "sleep",   cmd_sleep   },
    { "version", cmd_version },
    { "halt",    cmd_halt    },
};
#define CMD_COUNT ((size_t)(sizeof(commands) / sizeof(commands[0])))

/* ── Dispatch ──────────────────────────────────────────────────────────── */

static void dispatch(const char *line) {
    if (!*line) return;

    char name[32] = {0};
    size_t i = 0;
    while (line[i] && line[i] != ' ' && i < sizeof(name) - 1)
        { name[i] = line[i]; i++; }

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

/* ── Main loop ─────────────────────────────────────────────────────────── */

void shell_run(void) {
    char line[LINE_MAX];
    terminal_print("\nKern 2.0 -- type 'help' for commands.\n\n");
    for (;;) {
        terminal_print(PROMPT);
        readline(line, sizeof(line));
        dispatch(line);
    }
}
