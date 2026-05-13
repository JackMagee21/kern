#ifndef FD_H
#define FD_H

#include <stdint.h>

struct vfs_file;
struct pipe;

typedef enum {
    FD_NONE   = 0,  /* slot is empty (defaults to keyboard/VGA for fd 0/1) */
    FD_FILE   = 1,  /* open VFS file */
    FD_PIPE_R = 2,  /* read end of a pipe */
    FD_PIPE_W = 3,  /* write end of a pipe */
} fd_type_t;

typedef struct {
    fd_type_t        type;
    union {
        struct vfs_file *file;
        struct pipe     *pipe;
    };
} fd_t;

#endif /* FD_H */
