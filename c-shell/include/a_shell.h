#ifndef SHELL_H
#define SHELL_H
#define MAX_INPUT_LEN 1024
#define SHELL_NAME "cshell"
#include <signal.h>

extern volatile sig_atomic_t shell_sigint_flag;

int shell_init(void);
char *shell_home(void);

void shell_syntax_error(void);

#endif