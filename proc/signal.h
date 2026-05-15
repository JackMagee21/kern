#ifndef SIGNAL_H
#define SIGNAL_H

#include <stdint.h>

#define NSIG     32

#define SIGINT    2
#define SIGKILL   9
#define SIGPIPE  13
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20

#define SIG_DFL  0u
#define SIG_IGN  1u

/* Returned by task_waitpid when the child was stopped (not exited). */
#define WAIT_STOPPED  0x100

#endif
