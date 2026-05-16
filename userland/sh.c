/*
 * sh — userland shell for Kern 2.0.
 *
 * Features:
 *   - readline with history (up/down arrows), backspace, tab completion
 *   - Pipelines: cmd1 | cmd2 | ...
 *   - I/O redirection: < infile   > outfile   >> appendfile
 *   - Sequential &&/|| operators between pipelines
 *   - Job control: Ctrl+Z suspends, fg/bg/jobs builtins, SIGCONT
 *   - Builtins: exit, help, cd, pwd, jobs, fg, bg
 */
#include "lib.h"

#define MAXLINE        256
#define MAXARGS        16
#define MAX_PIPE_SEGS   4
#define MAX_AO_SEGS     8
#define HIST_MAX       16
#define MAX_JOBS        8

/* Built fresh before each readline call so line_replace can redraw it. */
static char g_prompt[132];
static int  g_prompt_len;

/* ── Command history ────────────────────────────────────────────────────── */

static char hist[HIST_MAX][MAXLINE];
static int  hist_count = 0;

static void hist_push(const char *line) {
    if (!line[0]) return;
    int slot = hist_count % HIST_MAX;
    int i = 0;
    while (line[i] && i < MAXLINE - 1) { hist[slot][i] = line[i]; i++; }
    hist[slot][i] = '\0';
    hist_count++;
}

/* ── Job table ──────────────────────────────────────────────────────────── */

typedef struct {
    int  pid;
    int  stopped;
    char cmd[MAXLINE];
} job_t;

static job_t jobs[MAX_JOBS];
static int   njobs = 0;

static int job_add(int pid, const char *cmd) {
    if (njobs >= MAX_JOBS) return -1;
    jobs[njobs].pid     = pid;
    jobs[njobs].stopped = 1;
    int i = 0;
    while (cmd[i] && i < MAXLINE - 1) { jobs[njobs].cmd[i] = cmd[i]; i++; }
    jobs[njobs].cmd[i] = '\0';
    return ++njobs;
}

static void job_remove(int pid) {
    for (int i = 0; i < njobs; i++) {
        if (jobs[i].pid == pid) {
            for (int j = i; j < njobs - 1; j++) jobs[j] = jobs[j+1];
            njobs--;
            return;
        }
    }
}

/* ── Tab completion ─────────────────────────────────────────────────────── */

static void line_replace(const char *buf, int n);  /* forward decl */

static void tab_complete(char *buf, int *n, int maxlen) {
    /* Find start of the current word. */
    int ws = *n;
    while (ws > 0 && buf[ws-1] != ' ') ws--;
    int partial_len = *n - ws;
    const char *partial = buf + ws;

    char match[MAXLINE]; match[0] = '\0';
    int  match_count = 0;
    char name[64];

    for (unsigned i = 0; ; i++) {
        int r = sys_getdent(i, name, sizeof(name));
        if (!r) break;
        if (strncmp(name, partial, (size_t)partial_len) != 0) continue;
        if (match_count == 0) {
            strcpy(match, name);
        } else {
            /* Narrow to longest common prefix. */
            int k;
            for (k = 0; match[k] && name[k] && match[k] == name[k]; k++);
            match[k] = '\0';
        }
        match_count++;
    }

    if (match_count == 0) return;

    int new_len = (int)strlen(match);
    if (new_len <= partial_len) {
        if (match_count > 1) sys_write(1, "\x07", 1); /* bell */
        return;
    }

    /* Insert the extra characters. */
    int extra = new_len - partial_len;
    for (int i = 0; i < extra && *n < maxlen - 1; i++) {
        buf[(*n)++] = match[partial_len + i];
    }
    buf[*n] = '\0';
    line_replace(buf, *n);
}

/* ── readline ───────────────────────────────────────────────────────────── */

static void line_replace(const char *buf, int n) {
    sys_write(1, "\r\033[K", 4);
    sys_write(1, g_prompt, (unsigned)g_prompt_len);
    if (n > 0) sys_write(1, buf, (unsigned)n);
}

static int readline(char *buf, int maxlen) {
    int n  = 0;
    int hi = -1;
    char saved[MAXLINE]; saved[0] = '\0';

    for (;;) {
        char c;
        sys_read(0, &c, 1);

        if (c == '\n' || c == '\r') {
            sys_write(1, "\n", 1);
            buf[n] = '\0';
            return n;
        }

        if (c == '\b' || c == 127) {
            if (n > 0) { n--; sys_write(1, "\b \b", 3); }
            continue;
        }

        if (c == '\t') { tab_complete(buf, &n, maxlen); continue; }

        if (c == '\x1b') {
            char seq[2];
            sys_read(0, &seq[0], 1);
            if (seq[0] != '[') continue;
            sys_read(0, &seq[1], 1);
            int avail = hist_count < HIST_MAX ? hist_count : HIST_MAX;
            if (seq[1] == 'A') { /* Up */
                if (hi + 1 >= avail) continue;
                if (hi < 0) {
                    int j = 0;
                    while (j < n) { saved[j] = buf[j]; j++; }
                    saved[j] = '\0';
                }
                hi++;
                int slot = ((hist_count - 1 - hi) % HIST_MAX + HIST_MAX) % HIST_MAX;
                n = 0;
                while (hist[slot][n] && n < maxlen - 1) { buf[n] = hist[slot][n]; n++; }
                line_replace(buf, n);
            } else if (seq[1] == 'B') { /* Down */
                if (hi < 0) continue;
                hi--;
                if (hi < 0) {
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
            continue;
        }

        if (c == '\x03') { /* Ctrl+C echo */
            sys_write(1, "^C\n", 3);
            buf[0] = '\0';
            return 0;
        }

        if (n < maxlen - 1) { buf[n++] = c; sys_write(1, &c, 1); }
    }
}

/* ── Command parsing ────────────────────────────────────────────────────── */

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

static void build_cmdline(char **argv, int argc, char *buf, int bufsz) {
    int pos = 0;
    for (int i = 0; i < argc && pos < bufsz - 2; i++) {
        if (i) buf[pos++] = ' ';
        const char *s = argv[i];
        while (*s && pos < bufsz - 1) buf[pos++] = *s++;
    }
    buf[pos] = '\0';
}

static int split_pipe(char *line, char **segs, int max) {
    int n = 0;
    segs[n++] = line;
    for (char *p = line; *p && n < max; p++) {
        if (*p == '|' && *(p+1) != '|') { *p = '\0'; segs[n++] = p + 1; }
    }
    return n;
}

/* And-or connector between pipelines. */
#define CONN_END 0
#define CONN_AND 1
#define CONN_OR  2

typedef struct { char *str; int conn; } ao_t;

static int split_and_or(char *line, ao_t *out, int max) {
    int n = 0;
    out[n].str  = line;
    out[n].conn = CONN_END;
    for (char *p = line; *p && n < max - 1; p++) {
        if (p[0] == '&' && p[1] == '&') {
            *p = '\0'; out[n].conn = CONN_AND;
            n++; out[n].str = p + 2; out[n].conn = CONN_END; p++;
        } else if (p[0] == '|' && p[1] == '|') {
            *p = '\0'; out[n].conn = CONN_OR;
            n++; out[n].str = p + 2; out[n].conn = CONN_END; p++;
        }
    }
    return n + 1;
}

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

static void exec_cmd(cmd_t *cmd, int in_fd, int out_fd) {
    if (in_fd != 0)  { sys_dup2(in_fd, 0);  sys_close(in_fd);  }
    if (out_fd != 1) { sys_dup2(out_fd, 1); sys_close(out_fd); }

    if (cmd->infile) {
        int fd = sys_open2(cmd->infile, O_RDONLY);
        if (fd < 0) { fprintf(1, "sh: cannot open: %s\n", cmd->infile); sys_exit(); }
        sys_dup2(fd, 0); sys_close(fd);
    }
    if (cmd->outfile) {
        int flags = O_WRONLY | O_CREAT;
        if (cmd->append) flags |= O_APPEND;
        int fd = sys_open2(cmd->outfile, flags);
        if (fd < 0) { fprintf(1, "sh: cannot create: %s\n", cmd->outfile); sys_exit(); }
        sys_dup2(fd, 1); sys_close(fd);
    }

    char cmdline[MAXLINE];
    build_cmdline(cmd->argv, cmd->argc, cmdline, sizeof(cmdline));
    sys_signal(SIGINT,  SIG_DFL);
    sys_signal(SIGTSTP, SIG_DFL);
    if (sys_exec(cmd->argv[0], cmdline) < 0) {
        fprintf(1, "sh: not found: %s\n", cmd->argv[0]);
    }
    sys_exit();
}

/*
 * Run a pipeline, wait for it.
 * Returns exit status (0 = success) or WAIT_STOPPED if a child was stopped.
 */
static int run_pipeline(char **segs, int nseg, const char *raw_line) {
    int pipes[MAX_PIPE_SEGS - 1][2];
    int pids[MAX_PIPE_SEGS];
    int npipes = 0;

    for (int i = 0; i < nseg - 1; i++) {
        if (sys_pipe(pipes[i]) < 0) {
            puts("sh: pipe() failed");
            for (int j = 0; j < i; j++) {
                sys_close(pipes[j][0]); sys_close(pipes[j][1]);
            }
            return 1;
        }
        npipes++;
    }

    for (int i = 0; i < nseg; i++) {
        cmd_t cmd;
        if (parse_cmd(segs[i], &cmd) == 0) { pids[i] = -1; continue; }

        int in_fd  = (i > 0)       ? pipes[i-1][0] : 0;
        int out_fd = (i < nseg-1)  ? pipes[i][1]   : 1;

        pids[i] = sys_fork();
        if (pids[i] < 0) { puts("sh: fork() failed"); pids[i] = -1; break; }

        if (pids[i] == 0) {
            for (int j = 0; j < nseg - 1; j++) {
                if (pipes[j][0] != in_fd)  sys_close(pipes[j][0]);
                if (pipes[j][1] != out_fd) sys_close(pipes[j][1]);
            }
            exec_cmd(&cmd, in_fd, out_fd);
        }
    }

    for (int i = 0; i < npipes; i++) {
        sys_close(pipes[i][0]); sys_close(pipes[i][1]);
    }

    int last_status = 0;
    for (int i = 0; i < nseg; i++) {
        if (pids[i] <= 0) continue;
        if (i == nseg - 1) sys_setfg(pids[i]);
        int status = sys_waitpid((unsigned)pids[i]);
        if (status == WAIT_STOPPED) {
            int jn = job_add(pids[i], raw_line ? raw_line : "");
            fprintf(1, "\n[%d]+ Stopped   %s\n",
                    jn, raw_line ? raw_line : "");
            last_status = WAIT_STOPPED;
        } else {
            last_status = status;
        }
    }
    sys_setfg(0);
    return last_status;
}

/* ── Prompt with cwd ────────────────────────────────────────────────────── */

static void print_prompt(void) {
    char cwd[128];
    sys_getcwd(cwd, sizeof(cwd));
    g_prompt_len = snprintf(g_prompt, sizeof(g_prompt), "%s$ ", cwd);
    sys_write(1, g_prompt, (unsigned)g_prompt_len);
}

/* ── Entry point ────────────────────────────────────────────────────────── */

void _start(int argc, char **argv) {
    (void)argc; (void)argv;

    sys_signal(SIGINT,  SIG_IGN);
    sys_signal(SIGTSTP, SIG_IGN);

    puts("Kern 2.0 shell  (type 'help' for commands)");

    char line[MAXLINE];

    for (;;) {
        print_prompt();
        int n = readline(line, sizeof(line));
        if (n == 0) continue;

        hist_push(line);

        /* ── Built-ins ── */
        if (strcmp(line, "exit") == 0) { puts("Goodbye."); sys_exit(); }

        if (strcmp(line, "help") == 0) {
            puts("Built-ins : exit  help  cd  pwd  jobs  fg  bg");
            puts("Programs  : ls  echo  cat  wc  grep  rm  mkdir  test");
            puts("Pipes     : cmd1 | cmd2");
            puts("Redirect  : cmd < in   cmd > out   cmd >> out");
            puts("Logic     : cmd1 && cmd2   cmd1 || cmd2");
            puts("Job ctrl  : Ctrl+Z suspend   fg [%N]   bg [%N]");
            continue;
        }

        if (strcmp(line, "pwd") == 0) {
            char cwd[128];
            sys_getcwd(cwd, sizeof(cwd));
            puts(cwd);
            continue;
        }

        if (strncmp(line, "cd", 2) == 0 && (line[2] == ' ' || line[2] == '\0')) {
            const char *path = (line[2] == ' ') ? line + 3 : "/";
            if (sys_chdir(path) < 0)
                fprintf(1, "cd: %s: not found\n", path);
            continue;
        }

        if (strcmp(line, "jobs") == 0) {
            if (!njobs) { puts("No jobs."); }
            else for (int i = 0; i < njobs; i++)
                fprintf(1, "[%d]+ Stopped   %s\n", i+1, jobs[i].cmd);
            continue;
        }

        /* fg [%N | N] */
        if (strncmp(line, "fg", 2) == 0 && (line[2] == ' ' || line[2] == '\0')) {
            int jn = 1;
            if (line[2] == ' ') {
                const char *p = line + 3;
                if (*p == '%') p++;
                jn = 0; while (*p >= '0' && *p <= '9') jn = jn*10 + (*p++ - '0');
            }
            if (jn < 1 || jn > njobs) { puts("fg: no such job"); continue; }
            int pid = jobs[jn-1].pid;
            fprintf(1, "%s\n", jobs[jn-1].cmd);
            job_remove(pid);
            sys_setfg((unsigned)pid);
            sys_kill((unsigned)pid, SIGCONT);
            int status = sys_waitpid((unsigned)pid);
            sys_setfg(0);
            if (status == WAIT_STOPPED) {
                int nn = job_add(pid, "");
                fprintf(1, "\n[%d]+ Stopped\n", nn);
            }
            continue;
        }

        /* bg [%N | N] */
        if (strncmp(line, "bg", 2) == 0 && (line[2] == ' ' || line[2] == '\0')) {
            int jn = 1;
            if (line[2] == ' ') {
                const char *p = line + 3;
                if (*p == '%') p++;
                jn = 0; while (*p >= '0' && *p <= '9') jn = jn*10 + (*p++ - '0');
            }
            if (jn < 1 || jn > njobs) { puts("bg: no such job"); continue; }
            int pid = jobs[jn-1].pid;
            fprintf(1, "[%d]+ %s &\n", jn, jobs[jn-1].cmd);
            jobs[jn-1].stopped = 0;
            sys_kill((unsigned)pid, SIGCONT);
            continue;
        }

        /* ── And-or pipeline execution ── */
        ao_t ao[MAX_AO_SEGS];
        int  nao = split_and_or(line, ao, MAX_AO_SEGS);
        int  last_status = 0;

        for (int i = 0; i < nao; i++) {
            if (i > 0) {
                int prev_conn = ao[i-1].conn;
                if (prev_conn == CONN_AND && last_status != 0) continue;
                if (prev_conn == CONN_OR  && last_status == 0) continue;
            }
            char *pipe_segs[MAX_PIPE_SEGS];
            int nseg = split_pipe(ao[i].str, pipe_segs, MAX_PIPE_SEGS);
            last_status = run_pipeline(pipe_segs, nseg, ao[i].str);
        }
    }
}
