#include "f_snoop.h"
#include "a_shell.h"
#include "c_exec.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NSYS 512

// The shell has a SIGCHLD handler that would reap our tracee's stops. Block SIGCHLD for the duration of tracing so ptrace gets every wait status
static void snoop_sigchld(int block)
{
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGCHLD);
    if (block) 
    {
        sigprocmask(SIG_BLOCK, &s, NULL);
    } 
    else 
    {
    sigprocmask(SIG_UNBLOCK, &s, NULL);
    }
}

// x86-64 syscall number -> name. Gaps are NULL and print as syscall_N 
static const char *SYS[NSYS] = 
{
    [0]="read",[1]="write",[2]="open",[3]="close",[4]="stat",[5]="fstat",
    [6]="lstat",[7]="poll",[8]="lseek",[9]="mmap",[10]="mprotect",[11]="munmap",
    [12]="brk",[13]="rt_sigaction",[14]="rt_sigprocmask",[15]="rt_sigreturn",
    [16]="ioctl",[17]="pread64",[18]="pwrite64",[19]="readv",[20]="writev",
    [21]="access",[22]="pipe",[23]="select",[24]="sched_yield",[25]="mremap",
    [26]="msync",[27]="mincore",[28]="madvise",[29]="shmget",[30]="shmat",
    [31]="shmctl",[32]="dup",[33]="dup2",[34]="pause",[35]="nanosleep",
    [36]="getitimer",[37]="alarm",[38]="setitimer",[39]="getpid",[40]="sendfile",
    [41]="socket",[42]="connect",[43]="accept",[44]="sendto",[45]="recvfrom",
    [46]="sendmsg",[47]="recvmsg",[48]="shutdown",[49]="bind",[50]="listen",
    [51]="getsockname",[52]="getpeername",[53]="socketpair",[54]="setsockopt",
    [55]="getsockopt",[56]="clone",[57]="fork",[58]="vfork",[59]="execve",
    [60]="exit",[61]="wait4",[62]="kill",[63]="uname",[72]="fcntl",[73]="flock",
    [74]="fsync",[75]="fdatasync",[76]="truncate",[77]="ftruncate",[78]="getdents",
    [79]="getcwd",[80]="chdir",[81]="fchdir",[82]="rename",[83]="mkdir",
    [84]="rmdir",[85]="creat",[86]="link",[87]="unlink",[88]="symlink",
    [89]="readlink",[90]="chmod",[91]="fchmod",[92]="chown",[93]="fchown",
    [95]="umask",[96]="gettimeofday",[97]="getrlimit",[98]="getrusage",
    [99]="sysinfo",[100]="times",[101]="ptrace",[102]="getuid",[104]="getgid",
    [105]="setuid",[107]="geteuid",[108]="getegid",[109]="setpgid",[110]="getppid",
    [111]="getpgrp",[112]="setsid",[121]="getpgid",[124]="getsid",[137]="statfs",
    [138]="fstatfs",[157]="prctl",[158]="arch_prctl",[186]="gettid",[201]="time",
    [202]="futex",[204]="sched_getaffinity",[213]="epoll_create",[217]="getdents64",
    [218]="set_tid_address",[228]="clock_gettime",[230]="clock_nanosleep",
    [231]="exit_group",[232]="epoll_wait",[233]="epoll_ctl",[234]="tgkill",
    [257]="openat",[258]="mkdirat",[262]="newfstatat",[263]="unlinkat",
    [269]="faccessat",[273]="set_robust_list",[274]="get_robust_list",
    [280]="utimensat",[281]="epoll_pwait",[288]="accept4",[290]="eventfd2",
    [291]="epoll_create1",[292]="dup3",[293]="pipe2",[302]="prlimit64",
    [316]="renameat2",[318]="getrandom",[319]="memfd_create",[322]="execveat",
    [332]="statx",[334]="rseq",[435]="clone3",[439]="faccessat2",
};

static const char *sysname(long nr, char *buf, size_t n)
{
    if (nr >= 0 && nr < NSYS && SYS[nr] != NULL) return SYS[nr];
    snprintf(buf, n, "syscall_%ld", nr);
    return buf;
}

typedef struct 
{ 
    long count; 
    double total; 
    int first; 
} Agg;

static int cmp_agg(const void *a, const void *b);

// The order array lets qsort compare by count desc, then first-seen asc
static Agg *g_agg;

static void run_loop(pid_t pid)
{
    Agg agg[NSYS];
    long order_nr[NSYS];
    int norder = 0;
    memset(agg, 0, sizeof agg);

    int in_call = 0;
    long cur = -1;
    struct timespec t0 = {0, 0};
    int status;

    while (1) 
    {
        if (ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0) break;
        if (waitpid(pid, &status, 0) < 0) break;
        if (WIFEXITED(status) || WIFSIGNALED(status)) break;

        if (WIFSTOPPED(status) && WSTOPSIG(status) == (SIGTRAP | 0x80)) 
        {
            if (in_call == 0)       // syscall entry 
            {                       
                struct user_regs_struct regs;
                if (ptrace(PTRACE_GETREGS, pid, 0, &regs) == 0)
                    cur = (long)regs.orig_rax;
                clock_gettime(CLOCK_MONOTONIC, &t0);
                in_call = 1;
            } 
            else
            {                                  // syscall exit  
                struct timespec t1;
                clock_gettime(CLOCK_MONOTONIC, &t1);
                double d = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
                if (cur >= 0 && cur < NSYS) 
                {
                    if (agg[cur].count == 0) 
                    { 
                        agg[cur].first = norder; 
                        order_nr[norder++] = cur; 
                    }
                    agg[cur].count++;
                    agg[cur].total += d;
                }
                in_call = 0;
            }
        }
    }

    // Sort the syscalls we actually saw
    long used[NSYS];
    int  nused = 0;
    for (int i = 0; i < norder; i++) used[nused++] = order_nr[i];
    g_agg = agg;
    qsort(used, nused, sizeof(long), cmp_agg);

    printf("%-18s%-8s%s\n", "syscall", "calls", "time");
    for (int i = 0; i < nused; i++) 
    {
        char buf[32];
        long nr = used[i];
        printf("%-18s%-8ld%.3fs\n", sysname(nr, buf, sizeof buf), agg[nr].count, agg[nr].total);
    }
    fflush(stdout);
}

static int cmp_agg(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    if (g_agg[x].count != g_agg[y].count)
        return (g_agg[x].count < g_agg[y].count) - (g_agg[x].count > g_agg[y].count);
    return (g_agg[x].first > g_agg[y].first) - (g_agg[x].first < g_agg[y].first);
}

int snoop_run(int argc, char **argv)
{
    // attach mode: snoop -p pid 
    if (argc >= 3 && strcmp(argv[1], "-p") == 0) 
    {
        pid_t pid = (pid_t)atoi(argv[2]);
        char  proc[64];
        struct stat st;
        snprintf(proc, sizeof proc, "/proc/%d", (int)pid);
        if (stat(proc, &st) != 0 || ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) 
        {
            fprintf(stderr, "snoop: no such process\n");
            return 1;
        }
        int status;
        snoop_sigchld(1);
        waitpid(pid, &status, 0);                       // the ATTACH stop 
        ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);
        run_loop(pid);
        snoop_sigchld(0);
        return 0;
    }

    // command mode: snoop cmd [args...] 
    if (argc >= 2) 
    {
        char path[EXEC_PATH_MAX];
        if (exec_resolve(argv[1], path, EXEC_PATH_MAX) == 0) 
        {
            fprintf(stderr, "snoop: command not found\n");
            return 1;
        }
        pid_t pid = fork();
        if (pid < 0) 
        { 
            perror("snoop: fork"); 
            return 1; 
        }
        if (pid == 0) 
        {
            snoop_sigchld(0);                           // clean mask for the tracee
            ptrace(PTRACE_TRACEME, 0, 0, 0);
            execv(path, &argv[1]);
            _exit(127);
        }
        int status;
        snoop_sigchld(1);
        waitpid(pid, &status, 0);                       // stop after execve
        ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);
        run_loop(pid);
        snoop_sigchld(0);
        return 0;
    }

    fprintf(stderr, "snoop: invalid syntax\n");
    return 1;
}