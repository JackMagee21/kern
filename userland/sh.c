/*
 * sh — userland shell for Kern 2.0.
 *
 * Features:
 *   - readline() with command history (up/down arrows), backspace, Ctrl+C echo
 *   - Pipelines: cmd1 | cmd2 | ... (up to MAX_PIPE_SEGS stages)
 *   - I/O redirection: < infile   > outfile   >> appendfile
 *   - Signals: ignores SIGINT so Ctrl+C kills child without killing shell
 */
#include "lib.h"

#define MAXLINE        256
#define MAXARGS        16
#define MAX_PIPE_SEGS   4
#define HIST_MAX       16
#define PROMPT         "$ "
#define PROMPT_LEN      2

/* ── Command history ────────────────────────────────────────────────────── */

static char hist[HIST_MAX][MAXLINE];
static int  hist_count = 0; /* total commands ever entered */

static void hist_push(const char *line) {
    if (!line[0]) return;
    int slot = hist_count % HIST_MAX;
    int i = 0;
    while (line[i] && i < MAXLINE - 1) { hist[slot][i] = line[i]; i++; }
    hist[slot][i] = '\0';
    hist_count++;
}

/* ── readline: raw char-by-char with history and basic editing ──────────── */

/*
 * Erase the current visible line (prompt + typed chars) and reprint
 * prompt + new content.  Uses ANSI \r + erase-to-EOL sequence.
 */
static void line_replace(const char *buf, int n) {
    sys_write(1, "\r\033[K", 4);   /* CR + erase to end of line */
    sys_write(1, PROMPT, PROMPT_LEN);
    if (n > 0) sys_write(1, buf, (unsigned)n);
}

static int readline(char *buf, int maxlen) {
    int n   = 0;  /* current line length   */
    int hi  = -1; /* history nav index (-1 = editing live input) */
    char saved[MAXLINE];
    saved[0] = '\0';

    for (;;) {
        char c;
        sys_read(0, &c, 1); /* raw single-char read */

        /* Enter */
        if (c == '\n' || c == '\r') {
            sys_write(1, "\n", 1);
            buf[n] = '\0';
            return n;
        }

        /* Backspace */
        if (c == '\b' || c == 127) {
            if (n > 0) {
                n--;
                sys_write(1, "\b \b", 3);
            }
            continue;
        }

        /* ESC: start of ANSI escape sequence */
        if (c == '\x1b') {
            char seq[2];
            sys_read(0, &seq[0], 1);
            if (seq[0] != '[') continue;
            sys_read(0, &seq[1], 1);

            int avail = hist_count < HIST_MAX ? hist_count : HIST_MAX;

            if (seq[1] == 'A') { /* Up arrow */
                if (hi + 1 >= avail) continue;
                if (hi < 0) {
                    /* Save current input before navigating away. */
                    int j = 0;
                    while (j < n) { saved[j] = buf[j]; j++; }
                    saved[j] = '\0';
                }
                hi++;
                int slot = ((hist_count - 1 - hi) % HIST_MAX + HIST_MAX) % HIST_MAX;
                n = 0;
                while (hist[slot][n] && n < maxlen - 1) { buf[n] = hist[slot][n]; n++; }
                line_replace(buf, n);

            } else if (seq[1] == 'B') { /* Down arrow */
                if (hi < 0) continue;
                hi--;
                if (hi < 0) {
                    /* Restore saved input. */
                    n = 0;
                    while (saved[n] && n < maxlen - 1) { buf[n] = saved[n]; n++; }
                    line_replace(buf, n);
                } else {
                    int slot = ((hist_count - 1 - hi) % HIST_MAX + HIST_MAX) % HIST_MAX;
                    n = 0;
                    while (hist[slot][n] && n < maxlen - 1) { buf[n] = hist[slot][n]; n++; }
                    line_replace(buf, n);
                }
            }
            /* Ignore left/right arrows for now */
            continue;
        }

        /* Ctrl+C echo — the kernel already signalled the fg task; just
         * print ^C and start a new line so the prompt looks clean. */
        if (c == '\x03') {
            sys_write(1, "^C\n", 3);
            buf[0] = '\0';
            return 0;
        }

        /* Printable character */
        if (n < maxlen - 1) {
            buf[n++] = c;
            sys_write(1, &c, 1); /* echo */
        }
    }
}

/* ── Command parsing ────────────────────────────────────────────────────── */

/* Split str on spaces; returns token count. Modifies str. */
static int split(char *str, char *argv[], int max) {
    int n = 0;
    char *p = str;
    while (*p && n < max - 1) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[n++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    argv[n] = (void *)0;
    return n;
}

/* Build a space-joined cmdline string from argv. */
static void build_cmdline(char **argv, int argc, char *buf, int bufsz) {
    int pos = 0;
    for (int i = 0; i < argc && pos < bufsz - 2; i++) {
        if (i) buf[pos++] = ' ';
        const char *s = argv[i];
        while (*s && pos < bufsz - 1) buf[pos++] = *s++;
    }
    buf[pos] = '\0';
}

/* Split line on '|'; returns segment count. Modifies line. */
static int split_pipe(char *line, char **segs, int max) {
    int n = 0;
    segs[n++] = line;
    for (char *p = line; *p && n < max; p++) {
        if (*p == '|') { *p = '\0'; segs[n++] = p + 1; }
    }
    return n;
}

/* Parsed single command with optional I/O redirections. */
typedef struct {
    char *argv[MAXARGS];
    int   argc;
    char *infile;
    char *outfile;
    int   append;
} cmd_t;

static int parse_cmd(char *seg, cmd_t *cmd) {
    char *words[MAXARGS + 4];
    int   nw = split(seg, words, MAXARGS + 4);
    cmd->argc = 0;
    cmd->infile = cmd->outfile = (void *)0;
    cmd->append = 0;
    for (int i = 0; i < nw; i++) {
        if (strcmp(words[i], "<") == 0 && i + 1 < nw) {
            cmd->infile = words[++i];
        } else if (strcmp(words[i], ">") == 0 && i + 1 < nw) {
            cmd->outfile = words[++i]; cmd->append = 0;
        } else if (strcmp(words[i], ">>") == 0 && i + 1 < nw) {
            cmd->outfile = words[++i]; cmd->append = 1;
        } else if (cmd->argc < MAXARGS - 1) {
            cmd->argv[cmd->argc++] = words[i];
        }
    }
    cmd->argv[cmd->argc] = (void *)0;
    return cmd->argc;
}

/* ── Execution ──────────────────────────────────────────────────────────── */

/*
 * Called in a child process.  Sets up I/O (pipe ends + file redirections)
 * then execs.  Never returns.
 */
static void exec_cmd(cmd_t *cmd, int in_fd, int out_fd) {
    /* Apply pipe redirections (may be overridden by explicit redirects). */
    if (in_fd != 0)  { sys_dup2(in_fd, 0);  sys_close(in_fd);  }
    if (out_fd != 1) { sys_dup2(out_fd, 1); sys_close(out_fd); }

    /* Apply explicit file redirections. */
    if (cmd->infile) {
        int fd = sys_open2(cmd->infile, O_RDONLY);
        if (fd < 0) {
            sys_write(1, "sh: cannot open: ", 17);
            puts(cmd->infile);
            sys_exit();
        }
        sys_dup2(fd, 0);
        sys_close(fd);
    }
    if (cmd->outfile) {
        int flags = O_WRONLY | O_CREAT;
        if (cmd->append) flags |= O_APPEND;
        int fd = sys_open2(cmd->outfile, flags);
        if (fd < 0) {
            sys_write(1, "sh: cannot create: ", 19);
            puts(cmd->outfile);
            sys_exit();
        }
        sys_dup2(fd, 1);
        sys_close(fd);
    }

    char cmdline[MAXLINE];
    build_cmdline(cmd->argv, cmd->argc, cmdline, sizeof(cmdline));

    /* Reset SIGINT to default in child. */
    sys_signal(SIGINT, SIG_DFL);

    if (sys_exec(cmd->argv[0], cmdline) < 0) {
        sys_write(1, "sh: not found: ", 15);
        puts(cmd->argv[0]);
    }
    sys_exit();
}

static void run_pipeline(char **segs, int nseg) {
    int pipes[MAX_PIPE_SEGS - 1][2];
    int pids[MAX_PIPE_SEGS];
    int npipes = 0;

    for (int i = 0; i < nseg - 1; i++) {
        if (sys_pipe(pipes[i]) < 0) {
            puts("sh: pipe() failed");
            for (int j = 0; j < i; j++) {
                sys_close(pipes[j][0]); sys_close(pipes[j][1]);
            }
            return;
        }
        npipes++;
    }

    for (int i = 0; i < nseg; i++) {
        cmd_t cmd;
        if (parse_cmd(segs[i], &cmd) == 0) { pids[i] = -1; continue; }

        int in_fd  = (i > 0)      ? pipes[i - 1][0] : 0;
        int out_fd = (i < nseg-1) ? pipes[i][1]     : 1;

        pids[i] = sys_fork();
        if (pids[i] < 0) { puts("sh: fork() failed"); pids[i] = -1; break; }

        if (pids[i] == 0) {
            /* Close all pipe fds except the ones this segment uses. */
            for (int j = 0; j < nseg - 1; j++) {
                if (pipes[j][0] != in_fd)  sys_close(pipes[j][0]);
                if (pipes[j][1] != out_fd) sys_close(pipes[j][1]);
            }
            exec_cmd(&cmd, in_fd, out_fd);
            /* not reached */
        }
    }

    for (int i = 0; i < npipes; i++) {
        sys_close(pipes[i][0]); sys_close(pipes[i][1]);
    }

    /* Set foreground to the last stage and wait for all children. */
    for (int i = 0; i < nseg; i++) {
        if (pids[i] > 0) {
            if (i == nseg - 1) sys_setfg(pids[i]);
            sys_waitpid((unsigned)pids[i]);
        }
    }
    sys_setfg(0);
}

/* ── Entry point ────────────────────────────────────────────────────────── */

void _start(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Shell survives Ctrl+C — only the child gets SIGINT. */
    sys_signal(SIGINT, SIG_IGN);

    puts("Kern 2.0 shell  (type 'help' for commands)");

    char line[MAXLINE];

    for (;;) {
        sys_write(1, PROMPT, PROMPT_LEN);
        int n = readline(line, sizeof(line));
        if (n == 0) continue;

        /* Save to history before built-in checks modify the buffer. */
        hist_push(line);

        /* ── Built-ins ── */
        if (strcmp(line, "exit") == 0) { puts("Goodbye."); sys_exit(); }

        if (strcmp(line, "help") == 0) {
            puts("Built-ins : exit  help");
            puts("Programs  : ls  echo  cat  wc  grep  test  (any initrd file)");
            puts("Pipes     : cmd1 | cmd2 | ...");
            puts("Redirect  : cmd < in   cmd > out   cmd >> out");
            puts("History   : up/down arrows");
            continue;
        }

        char *segs[MAX_PIPE_SEGS];
        int nseg = split_pipe(line, segs, MAX_PIPE_SEGS);
        run_pipeline(segs, nseg);
    }
}
