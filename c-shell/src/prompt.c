#include "prompt.h"
#include "shell.h"
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PATH 4096
#define MAX_HOST 256

static char *get_username(void)
{
    struct passwd *entry = getpwuid(getuid());
    if (entry != NULL && entry->pw_name != NULL)
        return entry->pw_name;

    char *from_env = getenv("USER");
    if (from_env != NULL)
        return from_env;
    return "unknown";
}

static void shorten_path(char *path, char *home, char *result)
{
    int home_len = (int)strlen(home);

    if (home_len > 0 && strncmp(path, home, home_len) == 0
        && (path[home_len] == '\0' || path[home_len] == '/')) {
        strcpy(result, "~");
        strcat(result, path + home_len);
    } else {
        strcpy(result, path);
    }
}

char *prompt_build(void)
{
    static char prompt[MAX_PATH + MAX_HOST + 256];
    char path[MAX_PATH];
    char short_path[MAX_PATH + 2];
    char host[MAX_HOST];

    if (getcwd(path, MAX_PATH) == NULL)
        strcpy(path, "?");

    if (gethostname(host, MAX_HOST) != 0)
        strcpy(host, "unknown");
    host[MAX_HOST - 1] = '\0';

    char *dot = strchr(host, '.');
    if (dot != NULL)
        *dot = '\0';

    shorten_path(path, shell_home(), short_path);

    strcpy(prompt, "<");
    strcat(prompt, get_username());
    strcat(prompt, "@");
    strcat(prompt, host);
    strcat(prompt, ":");
    strcat(prompt, short_path);
    strcat(prompt, "> ");
    return prompt;
}