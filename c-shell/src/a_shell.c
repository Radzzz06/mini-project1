#include "a_shell.h"
#include <stdio.h>
#include <unistd.h>

#define MAX_PATH 4096

static char home_path[MAX_PATH];

int shell_init(void)
{
    if (getcwd(home_path, MAX_PATH) == NULL) {
        perror("cshell: getcwd");
        return -1;
    }
    return 0;
}

char *shell_home(void)
{
    return home_path;
}

void shell_syntax_error(void)
{
    fprintf(stderr, "cshell: invalid syntax\n");
}