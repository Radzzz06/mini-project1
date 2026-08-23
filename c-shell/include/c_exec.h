#ifndef EXEC_H
#define EXEC_H

#include "a_command.h"

#define EXEC_PATH_MAX 4096

typedef struct {
    int in_fd;
    int out_fd;
    int targets[MAX_REDIRECTS];   
    int target_count;
    int fan_out;                  
} Redirection;

//C2 and C3
int redir_open(Command *cmd, Redirection *redir);

void redir_apply(Redirection *redir);

void redir_finish(Redirection *redir);

//C1
int exec_run_command(Command *cmd);

int exec_resolve(const char *name, char *out, int out_size);

#endif