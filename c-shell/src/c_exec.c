#include "c_exec.h"
#include "a_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int is_runnable(const char *path)
{
    struct stat info;

    if (stat(path, &info) != 0)
        return 0;
    if (S_ISDIR(info.st_mode))
        return 0;
    if (access(path, X_OK) != 0)
        return 0;
    return 1;
}

static int join_path(char *dest, int size, const char *dir, const char *name)
{
    int dir_len = (int)strlen(dir);
    int need = dir_len + 1 + (int)strlen(name) + 1;

    if (need > size)
        return 0;

    strcpy(dest, dir);
    if (dir_len > 0 && dest[dir_len - 1] != '/')
        strcat(dest, "/");
    strcat(dest, name);
    return 1;
}


static int search_in_path(const char *name, char *out, int out_size)
{
    char *path_env = getenv("PATH");
    char *path_copy;
    char *cursor;
    int found = 0;

    if (path_env == NULL)
        return 0;

    path_copy = strdup(path_env);
    if (path_copy == NULL)
        return 0;

    cursor = path_copy;
    while (cursor != NULL && *cursor != '\0' && found == 0) {
        char *colon = strchr(cursor, ':');
        char candidate[EXEC_PATH_MAX];
        const char *dir = cursor;

        if (colon != NULL)
            *colon = '\0';

        if (dir[0] == '\0')
            dir = ".";

        if (join_path(candidate, EXEC_PATH_MAX, dir, name) == 1
            && is_runnable(candidate) == 1) {
            if ((int)strlen(candidate) < out_size) {
                strcpy(out, candidate);
                found = 1;
            }
        }
        if(colon == NULL) cursor=NULL;
        else cursor = colon + 1;
    }
    free(path_copy);
    return found;
}

int exec_resolve(const char *name, char *out, int out_size)
{
    char candidate[EXEC_PATH_MAX];

    if (strchr(name, '/') != NULL) {
        if ((int)strlen(name) >= out_size)
            return 0;
        if (is_runnable(name) == 0)
            return 0;
        strcpy(out, name);
        return 1;
    }

    if (name[0] == '%')
        return search_in_path(name + 1, out, out_size);

    if (join_path(candidate, EXEC_PATH_MAX, ".", name) == 1 && is_runnable(candidate) == 1) {
        if ((int)strlen(candidate) < out_size) {
            strcpy(out, candidate);
            return 1;
        }
    }

    return search_in_path(name, out, out_size);
}

int exec_run_command(Command *cmd)
{
    char path[EXEC_PATH_MAX];
    pid_t pid;
    int status = 0;

    if (cmd->argc == 0 || cmd->argv[0] == NULL)
        return -1;

    if (cmd->argv[0][0] == '%') {
        char stripped[EXEC_PATH_MAX];

        if ((int)strlen(cmd->argv[0]) >= EXEC_PATH_MAX)
            return -1;
        strcpy(stripped, cmd->argv[0]);          

        if (exec_resolve(stripped, path, EXEC_PATH_MAX) == 0) {
            fprintf(stderr, "%s: command not found (%s)\n", SHELL_NAME,stripped + 1);
            return -1;
        }
        memmove(cmd->argv[0], cmd->argv[0] + 1, strlen(cmd->argv[0]));
    } else if (exec_resolve(cmd->argv[0], path, EXEC_PATH_MAX) == 0) {
        fprintf(stderr, "%s: command not found (%s)\n", SHELL_NAME,cmd->argv[0]);
        return -1;
    }

    fflush(stdout);          
    pid = fork();
    if (pid < 0) {
        perror("cshell: fork");
        return -1;
    }

    if (pid == 0) {
        execv(path, cmd->argv);
        fprintf(stderr, "%s: command not found (%s)\n", SHELL_NAME,cmd->argv[0]);_exit(127);         
    }

    if (waitpid(pid, &status, 0) < 0)
        return -1;

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}