#include "d_jobs.h"
#include "a_shell.h"
#include "b_builtins.h"
#include "c_exec.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

// job table
static Job_t jobs[JOBS_MAX];
static int next_job_id = 1;  

// Grab a free slot and stamp it with the next job number. Returns NULL if the table is full. We DON'T set pgid/pids yet, the launcher fills those
static Job_t *job_alloc(void)
{
    for (int i = 0; i < JOBS_MAX; i++) {
        if (jobs[i].in_use == 0) 
        {
            Job_t *j = &jobs[i];
            memset(j, 0, sizeof(*j));
            j->in_use = 1;
            j->job_id = next_job_id++;
            j->status = JS_RUNNING;
            return j;
        }
    }
    return NULL;
}

//Find whichever job owns this pid (scan every pipeline's pid list) 
static Job_t *job_find_by_pid(pid_t pid)
{
    for (int i = 0; i < JOBS_MAX; i++) {
        if (jobs[i].in_use == 0)
            continue;
        for (int k = 0; k < jobs[i].npids; k++)
            if (jobs[i].pids[k] == pid)
                return &jobs[i];
    }
    return NULL;
}

// Async-signal-safe printing (used inside the SIGCHLD handler)     
// printf() is NOT safe in a handler, so we go through write()

static void wstr(const char *s)
{
    write(STDOUT_FILENO, s, strlen(s));
}

static void wuint(unsigned long v)
{
    char buf[24];
    int  i = (int)sizeof(buf);

    if (v == 0) {
        write(STDOUT_FILENO, "0", 1);
        return;
    }
    while (v > 0 && i > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    write(STDOUT_FILENO, buf + i, (size_t)((int)sizeof(buf) - i));
}


// Blocking SIGCHLD around critical sections                       
//  While a FOREGROUND job runs we block SIGCHLD so, the handler can't steal the foreground children, and background "exited" reports are delayed until the foreground job finishes (that is D2 rule 11).             

static void sigchld_block(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigprocmask(SIG_BLOCK, &set, NULL);
}

static void sigchld_unblock(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
}

// The SIGCHLD handler: reap finished background children 

static void handle_dead(pid_t pid, int signaled)
{
    Job_t *j = job_find_by_pid(pid);

    if (j == NULL)
        return;                       // not one of ours (shouldn't happen) 

    if (signaled)
        j->abnormal = 1;

    if (j->nalive > 0)
        j->nalive--;

    // The whole pipeline is done. Report it and free the slot.We report the FIRST pid of the pipeline == pgid (D2 rule 13)
    if (j->nalive == 0) 
    {
        wstr(j->name);
        wstr(" with pid ");
        wuint((unsigned long)j->pgid);
        if (j->abnormal) wstr("exited abnormally\n"); 
        else wstr("exited normally\n");
        j->in_use = 0;
    }
}

static void on_sigchld(int sig)
{
    int saved_errno = errno;
    int status;
    pid_t pid;

    // WNOHANG so we never block, loop because one SIGCHLD can stand for several children that died at once
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) 
    {
        if (WIFEXITED(status) || WIFSIGNALED(status))
            handle_dead(pid, WIFSIGNALED(status));
    }

    errno = saved_errno;
}


//  Build the display name/full command line for the status lines

static void str_append(char *dst, int cap, const char *src)
{
    int len = (int)strlen(dst);
    int i = 0;

    while (src[i] != '\0' && len + 1 < cap) 
    {
        dst[len++] = src[i++];
    }
    dst[len] = '\0';
}

static void build_names(Job *job, char *name, char *cmdline)
{
    const char *first = job->commands[0]->argv[0];

    if (first != NULL && first[0] == '%')   // %ls is shown as ls
        first++;

    name[0] = '\0';
    str_append(name, JOB_NAME_MAX, first != NULL ? first : "");

    // cmdline = every stage joined, argv joined by spaces, stages by " | "
    cmdline[0] = '\0';
    for (int s = 0; s < job->command_count; s++) {
        Command *c = job->commands[s];

        if (s > 0)
            str_append(cmdline, JOB_NAME_MAX, " | ");
        for (int a = 0; a < c->argc; a++) {
            if (a > 0)
                str_append(cmdline, JOB_NAME_MAX, " ");
            str_append(cmdline, JOB_NAME_MAX, c->argv[a]);
        }
    }
}

//  Executing one command inside a freshly-forked child             

static void child_run(Command *cmd)
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
            shown++;
        fprintf(stderr, "%s: command not found (%s)\n", SHELL_NAME, shown);
        _exit(127);
    }

    if (cmd->argv[0][0] == '%')
        memmove(cmd->argv[0], cmd->argv[0] + 1, strlen(cmd->argv[0]));

    execv(path, cmd->argv);
    fprintf(stderr, "%s: command not found (%s)\n", SHELL_NAME, cmd->argv[0]);
    _exit(127);
}

// D2: launch a whole pipeline in the BACKGROUND (do not wait)

static void launch_background(Job *job)
{
    Redirection redirs[MAX_COMMANDS];
    int opened[MAX_COMMANDS];
    int stages = job->command_count;
    int prev_read = -1;
    pid_t pgid = 0;         // 0 until the first child is born 
    Job_t *slot;

    // Reserve the table slot up front (with SIGCHLD blocked) so a child that dies immediately still finds its job in the reaper
    sigchld_block();

    slot = job_alloc();
    if (slot == NULL) 
    {
        sigchld_unblock();
        fprintf(stderr, "%s: too many jobs\n", SHELL_NAME);
        return;
    }
    build_names(job, slot->name, slot->cmdline);

    for (int i = 0; i < stages; i++) 
    {
        Command *cmd = job->commands[i];
        int pipe_fds[2];
        int has_pipe = 0;
        pid_t pid;

        opened[i] = 0;

        if (i < stages - 1) 
        {
            if (pipe(pipe_fds) < 0) 
            {
                perror("cshell: pipe");
                break;
            }
            has_pipe = 1;
        }

        if (cmd->argc == 0 || redir_open(cmd, &redirs[i]) == 0) 
        {
            if (prev_read >= 0)
                close(prev_read);
            if (has_pipe) 
            {
                close(pipe_fds[1]);
                prev_read = pipe_fds[0];
            } else 
            {
                prev_read = -1;
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

        if (pid == 0) 
        {
            // The child
            // Join our own process group (leader is the first stage). Doing it here AND in the parent dodges the fork/exec race

            setpgid(0, pgid);          // pgid==0 that means it become group leader

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
            child_run(cmd);
        }

        //parent 
        if (pgid == 0)
            pgid = pid;                // first child defines the group   
        setpgid(pid, pgid);            // race-safe: also set from parent  

        slot->pids[slot->npids++] = pid;
        slot->nalive++;

        if (prev_read >= 0)
            close(prev_read);
        if (has_pipe) 
        {
            close(pipe_fds[1]);
            prev_read = pipe_fds[0];
        } else 
        {
        prev_read = -1;
        }
    }

    if (prev_read >= 0)
        close(prev_read);

    // Parent no longer needs the redirection fds it opened
    for (int i = 0; i < stages; i++)
        if (opened[i] == 1)
            redir_finish(&redirs[i]);

    slot->pgid = pgid;

    // D2 rule 3: print "[job] pid" BEFORE any command output. pid is the first process == pgid (D2 rule 13)
    printf("[%d] %d\n", slot->job_id, (int)slot->pgid);
    fflush(stdout);

    sigchld_unblock(); // now reports may flow
}

// Foreground execution (builtins + your Part C pipeline)            
// Returns 1 on success, 0 if the command could not be started at all (D1 rule 3: stop the rest of the sequence)              

#define RUN_OK 1
#define RUN_FAILED 0

static int run_builtin_with_redir(Command *cmd)
{
    Redirection redir;
    int saved_in = -1;
    int saved_out = -1;

    if (redir_open(cmd, &redir) == 0)
        return RUN_OK;                 // a bad redirect already printed  

    if (redir.in_fd >= 0) 
    {
        saved_in = dup(STDIN_FILENO);
        dup2(redir.in_fd, STDIN_FILENO);
    }
    if (redir.out_fd >= 0) 
    {
        fflush(stdout);
        saved_out = dup(STDOUT_FILENO);
        dup2(redir.out_fd, STDOUT_FILENO);
    }

    builtin_run(cmd->argc, cmd->argv);
    fflush(stdout);

    if (saved_in >= 0) 
    {
        dup2(saved_in, STDIN_FILENO);
        close(saved_in);
    }
    if (saved_out >= 0) 
    {
        dup2(saved_out, STDOUT_FILENO);
        close(saved_out);
    }
    redir_finish(&redir);
    return RUN_OK;
}

static int run_foreground(Job *job)
{
    // Single command: handle builtins, and check a plain command CAN startso D1 knows whether to stop the sequence
    if (job->command_count == 1) 
    {
        Command *cmd = job->commands[0];
        char path[EXEC_PATH_MAX];

        if (cmd->argc == 0)
            return RUN_OK;

        if (builtin_is_builtin(cmd->argv[0]) == 1)
            return run_builtin_with_redir(cmd);

        if (exec_resolve(cmd->argv[0], path, EXEC_PATH_MAX) == 0) 
        {
            const char *shown = cmd->argv[0];
            if (shown[0] == '%')
                shown++;
            fprintf(stderr, "%s: command not found (%s)\n", SHELL_NAME, shown);
            return RUN_FAILED; // D1 rule 3: stop the sequence
        }
    }

    // Otherwise (single external cmd or a pipeline) run it the Part C way, A not-found stage inside a pipeline does NOT stop the sequence
    exec_run_pipeline(job);
    return RUN_OK;
}


//  Public API                                                        

void jobs_init(void)
{
    struct sigaction sa;

    for (int i = 0; i < JOBS_MAX; i++)
        jobs[i].in_use = 0;

    // Install the SIGCHLD reaper. SA_RESTART keeps our per-byte read()from failing with EINTR; the handler still runs immediately, so a background job's "exited" line appears even while we wait at the prompt (D2 rule 10)
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    // Own the terminal so background jobs (in their own process groups)can't steal keyboard input (D2 rule 12). Part E builds on this. Guarded by isatty so grading with piped stdin still works. */
    if (isatty(STDIN_FILENO))
     {
        signal(SIGTTOU, SIG_IGN); // so tcsetpgrp doesn't stop us 
        setpgid(0, 0);       // shell is its own group leader 
        tcsetpgrp(STDIN_FILENO, getpgrp());
    }
}

void jobs_run_sequence(JobList *list, const char *raw_line)
{
    (void)raw_line;                

    for (int i = 0; i < list->job_count; i++) 
    {
        Job *job = list->jobs[i];

        if (job->command_count == 0)
            continue;
        if (job->command_count == 1 && job->commands[0]->argc == 0)
            continue;

        if (job->background == 1) 
        {
            launch_background(job);     // D2: fire and forget          
        } else 
        {
            sigchld_block();            // defer bg reports during fg (D11)
            int rc = run_foreground(job);
            sigchld_unblock();

            if (rc == RUN_FAILED)
                break;                  // D1 rule 3                    
        }
    }
}