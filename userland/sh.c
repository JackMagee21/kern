/*
 * sh — userland shell for Kern 2.0.
 *
 * Supports simple commands and pipelines (cmd1 | cmd2 | ... | cmdN).
 * Built-in commands: exit, help.
 */
#include "lib.h"

#define MAXLINE        256
#define MAXARGS        16
#define MAX_PIPE_SEGS   4
#define PROMPT         "$ "

/* Split line on spaces; returns argc. Modifies line. */
static int split(char *line, char *argv[], int max) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max - 1) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = (void *)0;
    return argc;
}

/* Build a space-joined cmdline string from argv tokens. */
static void build_cmdline(char **argv, int argc, char *buf, int bufsz) {
    int pos = 0;
    for (int i = 0; i < argc && pos < bufsz - 2; i++) {
        if (i) buf[pos++] = ' ';
        const char *s = argv[i];
        while (*s && pos < bufsz - 1) buf[pos++] = *s++;
    }
    buf[pos] = '\0';
}

/* Split line on '|' into segments; returns segment count. Modifies line. */
static int split_pipe(char *line, char **segs, int max) {
    int n = 0;
    segs[n++] = line;
    for (char *p = line; *p && n < max; p++) {
        if (*p == '|') {
            *p = '\0';
            segs[n++] = p + 1;
        }
    }
    return n;
}

/*
 * Execute one pipeline segment in the calling process (always a child).
 * Redirects in_fd → stdin and out_fd → stdout when they differ, then execs.
 * Never returns.
 */
static void exec_segment(char *seg, int in_fd, int out_fd) {
    char *args[MAXARGS];
    int ac = split(seg, args, MAXARGS);
    if (ac == 0) sys_exit();

    if (in_fd != 0)  { sys_dup2(in_fd, 0);  sys_close(in_fd);  }
    if (out_fd != 1) { sys_dup2(out_fd, 1); sys_close(out_fd); }

    char cmdline[MAXLINE];
    build_cmdline(args, ac, cmdline, sizeof(cmdline));

    if (sys_exec(args[0], cmdline) < 0) {
        sys_write(1, "sh: not found: ", 15);
        puts(args[0]);
    }
    sys_exit();
}

/*
 * Fork and exec all segments in a pipeline.
 * For a single segment this degenerates to a plain fork+exec+wait.
 */
static void run_pipeline(char **segs, int nseg) {
    int pipes[MAX_PIPE_SEGS - 1][2];
    int pids[MAX_PIPE_SEGS];
    int npipes = 0;

    /* Create the inter-segment pipes. */
    for (int i = 0; i < nseg - 1; i++) {
        if (sys_pipe(pipes[i]) < 0) {
            puts("sh: pipe() failed");
            for (int j = 0; j < i; j++) {
                sys_close(pipes[j][0]);
                sys_close(pipes[j][1]);
            }
            return;
        }
        npipes++;
    }

    /* Fork one child per segment. */
    for (int i = 0; i < nseg; i++) {
        int in_fd  = (i > 0)      ? pipes[i - 1][0] : 0;
        int out_fd = (i < nseg-1) ? pipes[i][1]     : 1;

        pids[i] = sys_fork();
        if (pids[i] < 0) {
            puts("sh: fork() failed");
            pids[i] = -1;
            break;
        }

        if (pids[i] == 0) {
            /* Child: close all pipe ends except the ones this segment uses. */
            for (int j = 0; j < nseg - 1; j++) {
                if (pipes[j][0] != in_fd)  sys_close(pipes[j][0]);
                if (pipes[j][1] != out_fd) sys_close(pipes[j][1]);
            }
            exec_segment(segs[i], in_fd, out_fd);
            /* not reached */
        }
    }

    /* Parent: close all pipe ends so children see EOF when writers exit. */
    for (int i = 0; i < npipes; i++) {
        sys_close(pipes[i][0]);
        sys_close(pipes[i][1]);
    }

    /* Wait for all children in order. */
    for (int i = 0; i < nseg; i++) {
        if (pids[i] > 0) sys_waitpid((unsigned)pids[i]);
    }
}

void _start(int argc, char **argv) {
    (void)argc; (void)argv;

    puts("Kern 2.0 shell — type 'help' or a program name.");

    char line[MAXLINE];

    for (;;) {
        sys_write(1, PROMPT, 2);
        int n = getline(line, sizeof(line));
        if (n == 0) continue;

        /* Built-ins (checked before split_pipe modifies the buffer). */
        if (strcmp(line, "exit") == 0) { puts("Goodbye."); sys_exit(); }
        if (strcmp(line, "help") == 0) {
            puts("Built-ins: exit, help");
            puts("Programs:  ls  echo  cat  test  (any name in the initrd)");
            puts("Pipelines: cmd1 | cmd2 | ...");
            continue;
        }

        char *segs[MAX_PIPE_SEGS];
        int nseg = split_pipe(line, segs, MAX_PIPE_SEGS);
        run_pipeline(segs, nseg);
    }
}
