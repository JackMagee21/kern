#ifndef SIGNAL_H
#define SIGNAL_H

#include <stdint.h>

#define NSIG     32

#define SIGINT    2
#define SIGKILL   9
#define SIGPIPE  13
#define SIGCHLD  17

#define SIG_DFL  0u
#define SIG_IGN  1u

#endif
