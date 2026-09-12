#include "d_jobs.h"
#include "a_shell.h"
#include "b_builtins.h"
#include "c_exec.h"
#include "f_spy.h"
#include "f_snoop.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

//Globals: the job table + a bit of shell state                    
static Job_t jobs[JOBS_MAX];
static int next_job_id = 1;    // monotonic: never reused       
static pid_t shell_pgid = 0;  // the shell's own process group   
static int have_tty = 0;     // is stdin a real terminal?       

static volatile sig_atomic_t g_alarm = 0;   // set by the SIGALRM handler 

// Result of waiting on a foreground group
typedef enum 
{ 
    FG_DONE,
    FG_STOPPED, 
    FG_TIMEDOUT 
} FgResult;

// Table helpers 
static Job_t *job_alloc(void)
{
    for (int i = 0; i < JOBS_MAX; i++) 
    {
        if (jobs[i].in_use == 0) {
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

static Job_t *job_find_by_pid(pid_t pid)
{
    for (int i = 0; i < JOBS_MAX; i++) 
    {
        if (jobs[i].in_use == 0)
            continue;
        for (int k = 0; k < jobs[i].npids; k++)
            if (jobs[i].pids[k] == pid)
                return &jobs[i];
    }
    return NULL;
}

static Job_t *job_find_by_id(int id)
{
    for (int i = 0; i < JOBS_MAX; i++)
        if (jobs[i].in_use == 1 && jobs[i].job_id == id)
            return &jobs[i];
    return NULL;
}

// Async-signal-safe printing (used inside the SIGCHLD handler)  

static void wstr(const char *s)
{
    write(STDOUT_FILENO, s, strlen(s)); 
}

static void wuint(unsigned long v)
{
    char buf[24];
    int  i = (int)sizeof(buf);
    if (v == 0) { write(STDOUT_FILENO, "0", 1); return; }
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    write(STDOUT_FILENO, buf + i, (size_t)((int)sizeof(buf) - i));
}

// Blocking SIGCHLD around critical sections     
static void sigchld_block(void)
{
    sigset_t s; sigemptyset(&s); sigaddset(&s, SIGCHLD);
    sigprocmask(SIG_BLOCK, &s, NULL);
}
static void sigchld_unblock(void)
{
    sigset_t s; sigemptyset(&s); sigaddset(&s, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &s, NULL);
}

// Signal handlers    

static void handle_dead(pid_t pid, int signaled)
{
    Job_t *j = job_find_by_pid(pid);
    if (j == NULL) return;
    if (signaled) j->abnormal = 1;
    if (j->nalive > 0) j->nalive--;

    if (j->nalive == 0) // whole pipeline finished 
    {                 
        wstr(j->name);
        wstr(" with pid ");
        wuint((unsigned long)j->pgid);    // first pid == pgid (D13) 
        wstr(j->abnormal ? " exited abnormally\n" : " exited normally\n");
        j->in_use = 0;
    }
}

static void on_sigchld(int sig)
{
    int saved = errno;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) 
    {
        if (WIFEXITED(status) || WIFSIGNALED(status)) 
        {
            handle_dead(pid, WIFSIGNALED(status));
        } else if (WIFSTOPPED(status))  // bg job hit SIGTTIN 
        {           
            Job_t *j = job_find_by_pid(pid);
            if (j) j->status = JS_STOPPED;
        } else if (WIFCONTINUED(status)) 
        {
            Job_t *j = job_find_by_pid(pid);
            if (j) j->status = JS_RUNNING;
        }
    }
    errno = saved;
}

// Ctrl-C at the prompt: newline + let the reader abandon the line
static void on_sigint(int sig) 
{
    (void)sig; 
    shell_sigint_flag = 1; 
    write(STDOUT_FILENO, "\n", 1);
}

// Ctrl-Z at the prompt: the shell must NOT stop, so we catch and ignore

static void on_sigtstp(int sig) 
{
    (void)sig; 
}

// resume --timeout fires this
static void on_sigalrm(int sig) 
{ 
    (void)sig;
    g_alarm = 1; 
}

//  Building display strings                                      
static void str_append(char *dst, int cap, const char *src)
{
    int len = (int)strlen(dst), i = 0;
    while (src[i] != '\0' && len + 1 < cap) dst[len++] = src[i++];
    dst[len] = '\0';
}

static const char *bare_name(const char *s)   // "%ls" -> "ls" 
{
    if (s != NULL && s[0] == '%') return s + 1;
    if(s != NULL) return s;
    else return "";
}

static void build_names(Job *job, char *name, char *cmdline)
{
    name[0] = '\0';
    str_append(name, JOB_NAME_MAX, bare_name(job->commands[0]->argv[0]));

    cmdline[0] = '\0';
    for (int s = 0; s < job->command_count; s++) {
        Command *c = job->commands[s];
        if (s > 0) str_append(cmdline, JOB_NAME_MAX, " | ");
        for (int a = 0; a < c->argc; a++) {
            if (a > 0) str_append(cmdline, JOB_NAME_MAX, " ");
            str_append(cmdline, JOB_NAME_MAX, c->argv[a]);
        }
    }
}

// Child side: run one command after fork                       

static void child_run(Command *cmd)
{
    char path[EXEC_PATH_MAX];

    // Restore default signal behaviour so Ctrl-C / Ctrl-Z reach the child (the shell has these blocked/ignored/handled)
    signal(SIGINT,  SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    sigchld_unblock();

    if (builtin_is_builtin(cmd->argv[0]) == 1) 
    {
        builtin_run(cmd->argc, cmd->argv);
        fflush(stdout);
        _exit(0);
    }
    if (exec_resolve(cmd->argv[0], path, EXEC_PATH_MAX) == 0) 
    {
        fprintf(stderr, "%s: command not found (%s)\n", SHELL_NAME, bare_name(cmd->argv[0]));
        _exit(127);
    }
    if (cmd->argv[0][0] == '%')
        memmove(cmd->argv[0], cmd->argv[0] + 1, strlen(cmd->argv[0]));
    execv(path, cmd->argv);
    fprintf(stderr, "%s: command not found (%s)\n", SHELL_NAME, cmd->argv[0]);
    _exit(127);
}

// Waiting on a foreground group (with optional timeout)           

static FgResult wait_group(pid_t pgid, int nalive, int timeout_secs)
{
    int alive = nalive;
    int st;
    pid_t w;

    if (timeout_secs > 0) 
    { 
        g_alarm = 0;
        alarm((unsigned)timeout_secs); 
    }

    while (alive > 0) 
    {
        w = waitpid(-pgid, &st, WUNTRACED);
        if (w < 0) {
            if (errno == EINTR) 
            {
                if (timeout_secs > 0 && g_alarm) // resume timed out 
                {   
                    kill(-pgid, SIGTERM);
                    while (waitpid(-pgid, &st, 0) > 0) { }   // reap them
                    alarm(0);
                    return FG_TIMEDOUT;
                }
                continue;
            }
            break;                                    // ECHILD etc.    
        }
        if (WIFSTOPPED(st))
            {
                if (timeout_secs > 0) alarm(0);
                return FG_STOPPED; 
            }
        if (WIFEXITED(st) || WIFSIGNALED(st)) alive--;
    }
    if (timeout_secs > 0) alarm(0);
    return FG_DONE;
}

//Launch a whole pipeline, foreground or background         
static void take_terminal(pid_t pgid) 
{ 
    if (have_tty) tcsetpgrp(STDIN_FILENO, pgid); 
}

static void launch_pipeline(Job *job, int background)
{
    Redirection redirs[JOB_MAX_PIDS];
    int opened[JOB_MAX_PIDS];
    int stages = job->command_count;
    int prev_read = -1;
    pid_t pgid = 0;
    pid_t plist[JOB_MAX_PIDS];
    char pnm[JOB_MAX_PIDS][JOB_PNAME_MAX];
    int npids = 0;
    char name[JOB_NAME_MAX], cmdline[JOB_NAME_MAX];

    if (stages > JOB_MAX_PIDS) stages = JOB_MAX_PIDS;
    build_names(job, name, cmdline);

    sigchld_block();

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
            if (prev_read >= 0) close(prev_read);
            if (has_pipe) 
            {
                close(pipe_fds[1]);
                prev_read = pipe_fds[0];
            } 
            else 
            {
                prev_read = -1;
            }
            continue;
        }
        opened[i] = 1;

        fflush(stdout);
        pid = fork();
        if (pid < 0) 
        { 
            perror("cshell: fork");
            break; 
        }

        if (pid == 0) {
            setpgid(0, pgid);                     // join the group    
            if (prev_read >= 0) 
            { 
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }
            if (has_pipe == 1) 
            {
                close(pipe_fds[0]);
                dup2(pipe_fds[1], STDOUT_FILENO);
                close(pipe_fds[1]);
            }
            redir_apply(&redirs[i]);
            for (int t = 0; t < redirs[i].target_count; t++) close(redirs[i].targets[t]);
            child_run(cmd);
        }

        if (pgid == 0) pgid = pid;                // first child leads 
        setpgid(pid, pgid);                       // race-safe in parent

        plist[npids] = pid;
        strncpy(pnm[npids], bare_name(cmd->argv[0]), JOB_PNAME_MAX - 1);
        pnm[npids][JOB_PNAME_MAX - 1] = '\0';
        npids++;

        if (prev_read >= 0) close(prev_read);
        prev_read = has_pipe ? (close(pipe_fds[1]), pipe_fds[0]) : -1;
    }
    if (prev_read >= 0) close(prev_read);

    // Background
    if (background == 1) 
    {
        Job_t *slot = job_alloc();
        if (slot != NULL) 
        {
            slot->pgid = pgid;
            slot->npids = npids;
            slot->nalive = npids;
            slot->status = JS_RUNNING;
            memcpy(slot->pids, plist, sizeof(pid_t) * npids);
            for (int i = 0; i < npids; i++) strcpy(slot->pnames[i], pnm[i]);
            str_append(slot->name, JOB_NAME_MAX, name);
            str_append(slot->cmdline, JOB_NAME_MAX, cmdline);
            printf("[%d] %d\n", slot->job_id, (int)pgid);   // [job] pid 
            fflush(stdout);
        }
        for (int i = 0; i < stages; i++) if (opened[i]) redir_finish(&redirs[i]);
        sigchld_unblock();
        return;
    }

    // Foreground
    take_terminal(pgid);
    FgResult r = wait_group(pgid, npids, 0);
    take_terminal(shell_pgid);                    // reclaim terminal  

    for (int i = 0; i < stages; i++) if (opened[i]) redir_finish(&redirs[i]);

    if (r == FG_STOPPED) // Ctrl-Z: now tracked 
    {                         
        Job_t *slot = job_alloc();
        if (slot != NULL) {
            slot->pgid = pgid;
            slot->npids = npids;
            slot->nalive = npids;
            slot->status = JS_STOPPED;
            memcpy(slot->pids, plist, sizeof(pid_t) * npids);
            for (int i = 0; i < npids; i++) strcpy(slot->pnames[i], pnm[i]);
            str_append(slot->name, JOB_NAME_MAX, name);
            str_append(slot->cmdline, JOB_NAME_MAX, cmdline);
            printf("[%d] + Stopped    %s\n", slot->job_id, cmdline);
            fflush(stdout);
        }
    }
    sigchld_unblock();
}

//  E1: activities                                              

static void jobs_activities(void)
{
    // print groups oldest-first == ascending job_id
    int printed;
    int last_id = 0;
    do {
        int next_id = 0;
        Job_t *pick = NULL;
        for (int i = 0; i < JOBS_MAX; i++) 
        {
            if (jobs[i].in_use == 0) continue;
            if (jobs[i].job_id > last_id && (next_id == 0 || jobs[i].job_id < next_id)) 
            {
                next_id = jobs[i].job_id;
                pick = &jobs[i];
            }
        }
        printed = (pick != NULL);
        if (printed) {
            last_id = pick->job_id;
            printf("[%d] pgid %d\n", pick->job_id, (int)pick->pgid);
            for (int k = 0; k < pick->npids; k++) 
            {
                if (kill(pick->pids[k], 0) != 0) continue;   // exited: skip
                printf("  %d %s %s\n", (int)pick->pids[k], pick->pnames[k],pick->status == JS_STOPPED ? "Stopped" : "Running");
            }
        }
    } while (printed);
    fflush(stdout);
}

//  E3: resume %n (fg [--timeout s] | bg)          

static int all_digits(const char *s)
{
    if (s == NULL || *s == '\0') return 0;
    for (int i = 0; s[i]; i++) if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

static void jobs_resume(int argc, char **argv)
{
    if (argc < 3 || argv[1][0] != '%' || all_digits(argv[1] + 1) == 0) 
    {
        fprintf(stderr, "resume: invalid syntax\n"); return;
    }
    int is_fg;
    if (strcmp(argv[2], "fg") == 0) is_fg = 1;
    else if (strcmp(argv[2], "bg") == 0) is_fg = 0;
    else 
    { 
        fprintf(stderr, "resume: invalid syntax\n");
        return; 
    }

    int timeout = 0;
    if (is_fg && argc > 3) 
    {
        if (argc != 5 || strcmp(argv[3], "--timeout") != 0 || all_digits(argv[4]) == 0) 
        {
            fprintf(stderr, "resume: invalid syntax\n"); 
            return;
        }
        timeout = atoi(argv[4]);
    } else if (!is_fg && argc != 3) 
    {
        fprintf(stderr, "resume: invalid syntax\n"); 
        return;
    }

    Job_t *j = job_find_by_id(atoi(argv[1] + 1));
    if (j == NULL) 
    { 
        fprintf(stderr, "resume: no such job\n"); 
        return; 
    }

    kill(-j->pgid, SIGCONT);
    j->status = JS_RUNNING;

    if (is_fg == 0) 
    {                                 // bg
        printf("[%d] + Running    %s\n", j->job_id, j->cmdline);
        fflush(stdout);
        return;
    }

    printf("%s\n", j->cmdline);                        // fg: echo cmd (rule 11)
    fflush(stdout);

    pid_t pgid = j->pgid;
    int nalive = j->nalive;
    int id = j->job_id;
    char cmdline[JOB_NAME_MAX];
    strcpy(cmdline, j->cmdline);

    sigchld_block();
    take_terminal(pgid);
    FgResult r = wait_group(pgid, nalive, timeout);
    take_terminal(shell_pgid);

    j = job_find_by_id(id);                            // re-find (may be gone)
    if (r == FG_DONE) 
    {
        if (j) j->in_use = 0;
    } else if (r == FG_STOPPED) 
    {
        if (j) j->status = JS_STOPPED;
        printf("[%d] + Stopped    %s\n", id, cmdline);
    } 
    else 
    { // FG_TIMEDOUT
        if (j) j->in_use = 0;
        printf("resume: job timed out\n");
    }
    fflush(stdout);
    sigchld_unblock();
}

// E4: ping <target> <signal>       

static void jobs_ping(int argc, char **argv)
{
    if (argc != 3) 
    { 
        fprintf(stderr, "ping: invalid syntax\n");
        return; 
    }

    /* signal is validated BEFORE the target (rule 3). */
    if (all_digits(argv[2]) == 0) 
    { 
        fprintf(stderr, "ping: invalid syntax\n"); 
        return; 
    }
    int sig = atoi(argv[2]);
    int actual = sig % 64;

    const char *tgt = argv[1];
    if (tgt[0] == '%') 
    {
        if (all_digits(tgt + 1) == 0) 
        { 
            fprintf(stderr, "ping: no such process found\n"); 
            return; 
        }
        Job_t *j = job_find_by_id(atoi(tgt + 1));
        if (j == NULL || kill(-j->pgid, actual) != 0) 
        {
            fprintf(stderr, "ping: no such process found\n"); 
            return;
        }
        printf("Sent signal %d to %s\n", sig, tgt);
    }
    else 
    {
        if (all_digits(tgt) == 0) 
        { 
            fprintf(stderr, "ping: no such process found\n"); 
            return; 
        }
        pid_t pid = (pid_t)atoi(tgt);
        if (job_find_by_pid(pid) == NULL || kill(pid, actual) != 0) 
        {
            fprintf(stderr, "ping: no such process found\n"); 
            return;
        }
        printf("Sent signal %d to %d\n", sig, (int)pid);
    }
    fflush(stdout);
}

// Foreground dispatch (builtins + job-control builtins + externals)

#define RUN_OK 1
#define RUN_FAILED 0

static int run_builtin_with_redir(Command *cmd)
{
    Redirection redir;
    int saved_in = -1, saved_out = -1;

    if (redir_open(cmd, &redir) == 0) return RUN_OK;
    if (redir.in_fd >= 0)  
    { 
        saved_in  = dup(STDIN_FILENO);  
        dup2(redir.in_fd,  STDIN_FILENO); 
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
        dup2(saved_in,  STDIN_FILENO);  
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
    if (job->command_count == 1) 
    {
        Command *cmd = job->commands[0];
        char path[EXEC_PATH_MAX];

        if (cmd->argc == 0) return RUN_OK;

        // job-control builtins live here (they need the job table)
        if (strcmp(cmd->argv[0], "activities") == 0) 
        { 
            jobs_activities();               
            return RUN_OK; 
        }
        if (strcmp(cmd->argv[0], "resume") == 0) 
        { 
            jobs_resume(cmd->argc, cmd->argv); 
            return RUN_OK; 
        }
        if (strcmp(cmd->argv[0], "ping") == 0) 
        { 
            jobs_ping(cmd->argc, cmd->argv);   
            return RUN_OK; 
        }
        if (strcmp(cmd->argv[0], "spy") == 0) 
        { 
            spy_run(cmd->argc, cmd->argv);  
            return RUN_OK; 
        }
        if (strcmp(cmd->argv[0], "snoop") == 0) 
        { 
            snoop_run(cmd->argc, cmd->argv); 
            return RUN_OK; 
        }

        if (builtin_is_builtin(cmd->argv[0]) == 1)
            return run_builtin_with_redir(cmd);

        if (exec_resolve(cmd->argv[0], path, EXEC_PATH_MAX) == 0) 
        {
            fprintf(stderr, "%s: command not found (%s)\n", SHELL_NAME, bare_name(cmd->argv[0]));
            return RUN_FAILED;                    // D1 rule 3 
        }
    }
    launch_pipeline(job, 0);
    return RUN_OK;
}

//  Public API                                            

static void install(int signo, void (*handler)(int), int restart)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = restart ? SA_RESTART : 0;
    sigaction(signo, &sa, NULL);
}

void jobs_init(void)
{
    for (int i = 0; i < JOBS_MAX; i++) jobs[i].in_use = 0;

    install(SIGCHLD, on_sigchld, 1);   // SA_RESTART: don't break read()  
    install(SIGINT,  on_sigint, 0);   // no restart: read() gets EINTR   
    install(SIGTSTP, on_sigtstp, 1);   // catch+ignore so shell won't stop
    install(SIGALRM, on_sigalrm, 0);   // no restart: waitpid gets EINTR 
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    have_tty = isatty(STDIN_FILENO);
    setpgid(0, 0);
    shell_pgid = getpgrp();
    if (have_tty) tcsetpgrp(STDIN_FILENO, shell_pgid);
}

void jobs_run_sequence(JobList *list, const char *raw_line)
{
    (void)raw_line;
    for (int i = 0; i < list->job_count; i++) 
    {
        Job *job = list->jobs[i];
        if (job->command_count == 0) continue;
        if (job->command_count == 1 && job->commands[0]->argc == 0) continue;

        if (job->background == 1) 
        {
            launch_pipeline(job, 1);
        } else 
        {
            if (run_foreground(job) == RUN_FAILED)
                break;                            // D1 rule 3
        }
    }
}

int jobs_has_stopped(void)
{
    for (int i = 0; i < JOBS_MAX; i++)
        if (jobs[i].in_use == 1 && jobs[i].status == JS_STOPPED)
            return 1;
    return 0;
}

void jobs_hangup_all(void)
{
    for (int i = 0; i < JOBS_MAX; i++) 
    {
        if (jobs[i].in_use == 0) continue;
        kill(-jobs[i].pgid, SIGHUP);
        kill(-jobs[i].pgid, SIGCONT);   // so a stopped group actually gets it
    }
}