#include "f_spy.h"
#include "a_shell.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

// Map a stat mode to the short TYPE label spy prints 
static const char *type_of(const char *path)
{
    struct stat s;
    if (stat(path, &s) != 0) return "unknown";
    if (S_ISREG(s.st_mode)) return "REG";
    if (S_ISDIR(s.st_mode)) return "DIR";
    if (S_ISCHR(s.st_mode)) return "CHR";
    if (S_ISBLK(s.st_mode)) return "BLK";
    if (S_ISFIFO(s.st_mode)) return "FIFO";
    if (S_ISSOCK(s.st_mode)) return "unix";
    if (S_ISLNK(s.st_mode)) return "LINK";
    return "unknown";
}

static int read_link(const char *p, char *buf, size_t n)
{
    ssize_t r = readlink(p, buf, n - 1);
    if (r < 0) return 0;
    buf[r] = '\0';
    return 1;
}

static void row(pid_t pid, const char *fd, const char *type, const char *path)
{
    printf("%-6d %-6s %-6s %s\n", (int)pid, fd, type, path);
}

// cwd and txt are single symlinks under /proc/<pid>/
static void emit_link(pid_t pid, const char *label, const char *rel)
{
    char link[128], tgt[4096];
    snprintf(link, sizeof link, "/proc/%d/%s", (int)pid, rel);
    if (read_link(link, tgt, sizeof tgt))
        row(pid, label, type_of(tgt), tgt);
}

// Each unique memory-mapped file path (a real path, printed once)
static void emit_maps(pid_t pid)
{
    char path[128];
    snprintf(path, sizeof path, "/proc/%d/maps", (int)pid);
    FILE *f = fopen(path, "r");
    if (f == NULL) return;

    char  seen[512][4096];
    int   nseen = 0;
    char  line[8192];

    while (fgets(line, sizeof line, f) != NULL) 
    {
        char *p = strchr(line, '/');           // pathname starts at first '/' 
        if (p == NULL) continue;               // anon / [heap] / [stack]   
        size_t len = strlen(p);
        if (len > 0 && p[len - 1] == '\n') p[--len] = '\0';

        int dup = 0;
        for (int i = 0; i < nseen; i++)
        {
            if (strcmp(seen[i], p) == 0) 
            { 
                dup = 1; 
                break; 
            }
        }
        if (dup) continue;

        if (nseen < 512) 
        { 
            strncpy(seen[nseen], p, 4095);
            seen[nseen][4095] = '\0'; 
            nseen++; 
        }
        row(pid, "mem", type_of(p), p);
    }
    fclose(f);
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

// Numeric file descriptors, ascending 
static void emit_fds(pid_t pid)
{
    char dirp[128];
    snprintf(dirp, sizeof dirp, "/proc/%d/fd", (int)pid);
    DIR *d = opendir(dirp);
    if (d == NULL) return;

    int fds[1024];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) 
    {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        if (n < 1024) fds[n++] = atoi(e->d_name);
    }
    closedir(d);
    qsort(fds, n, sizeof(int), cmp_int);

    for (int i = 0; i < n; i++) 
    {
        char link[160], tgt[4096], label[16];
        snprintf(link, sizeof link, "/proc/%d/fd/%d", (int)pid, fds[i]);
        snprintf(label, sizeof label, "%d", fds[i]);
        if (read_link(link, tgt, sizeof tgt))
            row(pid, label, type_of(link), tgt);   // stat the link: follows to real object
    }
}

int spy_run(int argc, char **argv)
{
    pid_t pid;

    if (argc == 1) pid = getpid();
    else if (argc == 2) pid = (pid_t)atoi(argv[1]);
    else 
    { 
        fprintf(stderr, "spy: invalid syntax\n"); 
        return 1; 
    }

    char base[64];
    struct stat st;
    snprintf(base, sizeof base, "/proc/%d", (int)pid);
    if (stat(base, &st) != 0) 
    { 
        fprintf(stderr, "spy: no such process\n"); 
        return 1; 
    }

    char fddir[80];
    snprintf(fddir, sizeof fddir, "/proc/%d/fd", (int)pid);
    DIR *probe = opendir(fddir);
    if (probe == NULL && errno == EACCES) 
    { 
        fprintf(stderr, "spy: permission denied\n"); 
        return 1; 
    }
    if (probe != NULL) closedir(probe);

    printf("%-6s %-6s %-6s %s\n", "PID", "FD", "TYPE", "PATH");
    emit_link(pid, "cwd", "cwd");
    emit_link(pid, "txt", "exe");
    emit_maps(pid);
    emit_fds(pid);
    fflush(stdout);
    return 0;
}