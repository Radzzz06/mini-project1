#ifndef EXEC_H
#define EXEC_H

#include "a_command.h"

#define EXEC_PATH_MAX 4096

//C1
int exec_run_command(Command *cmd);

int exec_resolve(const char *name, char *out, int out_size);

//C2
int exec_open_input(Command *cmd, int *fd_out);

#endif