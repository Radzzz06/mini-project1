#ifndef SHELL_H
#define SHELL_H
#define MAX_INPUT_LEN 1024
#define SHELL_NAME "cshell"


int shell_init(void);
char *shell_home(void);

void shell_syntax_error(void);

#endif