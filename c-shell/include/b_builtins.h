#ifndef BUILTINS_H
#define BUILTINS_H

int builtin_is_builtin(const char *name);

int builtin_run(int argc, char **argv);
void builtin_error(const char *message);

int reveal_command(int argc, char **argv);
int peek_command(int argc, char **argv);
int locate_command(int argc, char **argv);

#endif