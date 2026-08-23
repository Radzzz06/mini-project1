#include "c_exec.h"
#include "a_shell.h"
#include "b_builtins.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define COPY_CHUNK 4096
#define FILE_MODE 0644          

// C1
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

        cursor = (colon == NULL) ? NULL : colon + 1;
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

//redirection
static int copy_all(int from_fd, int to_fd)
{
    char chunk[COPY_CHUNK];
    ssize_t got;

    while ((got = read(from_fd, chunk, sizeof(chunk))) > 0) {
        ssize_t written = 0;

        while (written < got) {
            ssize_t n = write(to_fd, chunk + written, (size_t)(got - written));
            if (n <= 0)
                return 0;
            written += n;
        }
    }
    return (got < 0) ? 0 : 1;
}


static int make_scratch_file(void)
{
    char template[] = "/tmp/cshell_tmpXXXXXX";
    int fd = mkstemp(template);

    if (fd < 0)
        return -1;
    unlink(template);
    return fd;
}

static void close_fd_list(int *fds, int count)
{
    for (int i = 0; i < count; i++) {
        if (fds[i] >= 0)
            close(fds[i]);
    }
}

static void redir_init(Redirection *redir)
{
    redir->in_fd = -1;
    redir->out_fd = -1;
    redir->target_count = 0;
    redir->fan_out = 0;
}

// C2
static int open_input(Command *cmd, Redirection *redir)
{
    int fds[MAX_REDIRECTS];
    int count = 0;
    int scratch;

    for (int i = 0; i < cmd->redirect_count; i++) {
        if (cmd->redirects[i].type != REDIR_IN)
            continue;

        fds[count] = open(cmd->redirects[i].file, O_RDONLY);
        if (fds[count] < 0) {
            fprintf(stderr, "%s: no such file or directory\n", SHELL_NAME);
            close_fd_list(fds, count);
            return 0;
        }
        count++;
    }

    if (count == 0)
        return 1;                     

    if (count == 1) {
        redir->in_fd = fds[0];        
        return 1;
    }

    scratch = make_scratch_file();
    if (scratch < 0) {
        fprintf(stderr, "%s: no such file or directory\n", SHELL_NAME);
        close_fd_list(fds, count);
        return 0;
    }

    for (int i = 0; i < count; i++) {
        copy_all(fds[i], scratch);
        close(fds[i]);
    }

    lseek(scratch, 0, SEEK_SET);      
    redir->in_fd = scratch;
    return 1;
}

// C3
static int open_output(Command *cmd, Redirection *redir)
{
    for (int i = 0; i < cmd->redirect_count; i++) {
        int flags;
        int fd;

        if (cmd->redirects[i].type == REDIR_OUT)
            flags = O_WRONLY | O_CREAT | O_TRUNC;     
        else if (cmd->redirects[i].type == REDIR_APPEND)
            flags = O_WRONLY | O_CREAT | O_APPEND;    
        else
            continue;

        fd = open(cmd->redirects[i].file, flags, FILE_MODE);
        if (fd < 0) {
            fprintf(stderr, "%s: unable to create file for writing\n",SHELL_NAME);
            close_fd_list(redir->targets, redir->target_count);
            redir->target_count = 0;
            return 0;
        }

        redir->targets[redir->target_count] = fd;
        redir->target_count++;
    }

    if (redir->target_count == 0)
        return 1;                     

    if (redir->target_count == 1) {
        redir->out_fd = redir->targets[0];
        return 1;
    }

    redir->out_fd = make_scratch_file();
    if (redir->out_fd < 0) {
        fprintf(stderr, "%s: unable to create file for writing\n", SHELL_NAME);
        close_fd_list(redir->targets, redir->target_count);
        redir->target_count = 0;
        return 0;
    }
    redir->fan_out = 1;
    return 1;
}

int redir_open(Command *cmd, Redirection *redir)
{
    redir_init(redir);

    if (open_input(cmd, redir) == 0)
        return 0;

    if (open_output(cmd, redir) == 0) {
        if (redir->in_fd >= 0)
            close(redir->in_fd);
        redir->in_fd = -1;
        return 0;
    }
    return 1;
}

void redir_apply(Redirection *redir)
{
    if (redir->in_fd >= 0) {
        dup2(redir->in_fd, STDIN_FILENO);   
        close(redir->in_fd);                
    }
    if (redir->out_fd >= 0) {
        dup2(redir->out_fd, STDOUT_FILENO);
        close(redir->out_fd);
    }
}

void redir_finish(Redirection *redir)
{
    if (redir->fan_out == 1 && redir->out_fd >= 0) {
        for (int i = 0; i < redir->target_count; i++) {
            lseek(redir->out_fd, 0, SEEK_SET);   
            copy_all(redir->out_fd, redir->targets[i]);
        }
    }

    if (redir->out_fd >= 0 && redir->fan_out == 1)
        close(redir->out_fd);

    close_fd_list(redir->targets, redir->target_count);

    if (redir->in_fd >= 0)
        close(redir->in_fd);

    redir_init(redir);
}

// C1 and C4
static void child_exec(Command *cmd)
{
    char path[EXEC_PATH_MAX];

    if (builtin_is_builtin(cmd->argv[0]) == 1) {
        builtin_run(cmd->argc, cmd->argv);
        fflush(stdout);
        _exit(0);
    }

    if (exec_resolve(cmd->argv[0], path, EXEC_PATH_MAX) == 0) {
        const char *shown = cmd->argv[0];

        if (shown[0] == '%')          
            shown = shown + 1;
        fprintf(stderr, "%s: command not found (%s)\n", SHELL_NAME, shown);
        _exit(127);
    }

    if (cmd->argv[0][0] == '%')
        memmove(cmd->argv[0], cmd->argv[0] + 1, strlen(cmd->argv[0]));

    execv(path, cmd->argv);

    fprintf(stderr, "%s: command not found (%s)\n", SHELL_NAME, cmd->argv[0]);
    _exit(127);          
}

int exec_run_pipeline(Job *job)
{
    Redirection redirs[MAX_COMMANDS];
    int opened[MAX_COMMANDS];
    pid_t pids[MAX_COMMANDS];
    int stage_count = job->command_count;
    int prev_read = -1;          
    int status = 0;
    int last = -1;

    for (int i = 0; i < stage_count; i++) {
        Command *cmd = job->commands[i];
        int pipe_fds[2];
        int has_pipe = 0;
        pid_t pid;

        opened[i] = 0;
        pids[i] = -1;

        if (i < stage_count - 1) {
            if (pipe(pipe_fds) < 0) {
                perror("cshell: pipe");
                break;
            }
            has_pipe = 1;
        }

        if (cmd->argc == 0 || redir_open(cmd, &redirs[i]) == 0) {
            if (prev_read >= 0)
                close(prev_read);
            prev_read = -1;
            if (has_pipe == 1) {
                close(pipe_fds[1]);
                prev_read = pipe_fds[0];
            }
            continue;
        }
        opened[i] = 1;

        fflush(stdout);
        pid = fork();
        if (pid < 0) {
            perror("cshell: fork");
            break;
        }

        if (pid == 0) {
            
            if (prev_read >= 0) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }

            if (has_pipe == 1) {
                close(pipe_fds[0]);            
                dup2(pipe_fds[1], STDOUT_FILENO);
                close(pipe_fds[1]);           
            }

            redir_apply(&redirs[i]);
            close_fd_list(redirs[i].targets, redirs[i].target_count);

            child_exec(cmd);                  
        }


        pids[i] = pid;
        last = i;

        if (prev_read >= 0)
            close(prev_read);
        prev_read = -1;

        if (has_pipe == 1) {
            close(pipe_fds[1]);        
            prev_read = pipe_fds[0];   
        }
    }

    if (prev_read >= 0)
        close(prev_read);

    for (int i = 0; i < stage_count; i++) {
        if (pids[i] > 0) {
            int child_status = 0;

            waitpid(pids[i], &child_status, 0);
            if (i == last)
                status = child_status;
        }
    }

    for (int i = 0; i < stage_count; i++) {
        if (opened[i] == 1)
            redir_finish(&redirs[i]);
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}