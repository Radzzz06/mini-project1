#ifndef EXEC_H
#define EXEC_H

#include "a_command.h"

#define EXEC_PATH_MAX 4096

int exec_run_command(Command *cmd);

int exec_resolve(const char *name, char *out, int out_size);

#endif