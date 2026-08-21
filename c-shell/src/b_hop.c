#include "b_hop.h"
#include "b_builtins.h"
#include "a_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>


static char previous_dir[HOP_PATH_MAX];
static int has_previous = 0;  


#define FREC_MAX_ENTRIES 512
#define FREC_FILE_NAME ".cshell_hop_history"

typedef struct {
    char path[HOP_PATH_MAX];
    long visits;     
    long last_time;   
} FrecEntry;

static FrecEntry frec_table[FREC_MAX_ENTRIES];
static int frec_count = 0;


static void frec_file_path(char *out, int out_size)
{
    char *home = shell_home();
    int need = (int)strlen(home) + 1 + (int)strlen(FREC_FILE_NAME) + 1;

    if (need > out_size) {
        out[0] = '\0';
        return;
    }
    strcpy(out, home);
    if (out[0] != '\0' && out[strlen(out) - 1] != '/')
        strcat(out, "/");
    strcat(out, FREC_FILE_NAME);
}


static void frec_load(void)
{
    char file[HOP_PATH_MAX];
    char line[HOP_PATH_MAX + 128];
    FILE *fp;

    frec_count = 0;
    frec_file_path(file, HOP_PATH_MAX);
    if (file[0] == '\0')
        return;

    fp = fopen(file, "r");
    if (fp == NULL)
        return;   

    while (fgets(line, (int)sizeof(line), fp) != NULL) {
        char *cursor = line;
        long visits;
        long last_time;
        int len;

        if (frec_count >= FREC_MAX_ENTRIES)
            break;

        visits = strtol(cursor, &cursor, 10);
        last_time = strtol(cursor, &cursor, 10);

        while (*cursor == ' ')
            cursor++;

        len = (int)strlen(cursor);
        while (len > 0 && (cursor[len - 1] == '\n' || cursor[len - 1] == '\r')) {
            cursor[len - 1] = '\0';
            len--;
        }

        if (len == 0 || len >= HOP_PATH_MAX || visits <= 0)
            continue;

        strcpy(frec_table[frec_count].path, cursor);
        frec_table[frec_count].visits = visits;
        frec_table[frec_count].last_time = last_time;
        frec_count++;
    }
    fclose(fp);
}

static void frec_save(void)
{
    char file[HOP_PATH_MAX];
    FILE *fp;

    frec_file_path(file, HOP_PATH_MAX);
    if (file[0] == '\0')
        return;

    fp = fopen(file, "w");
    if (fp == NULL)
        return;

    for (int i = 0; i < frec_count; i++)
        fprintf(fp, "%ld %ld %s\n", frec_table[i].visits,frec_table[i].last_time, frec_table[i].path);

    fclose(fp);
}


static long frec_score(const FrecEntry *entry, long now)
{
    long age = now - entry->last_time;

    if (age < 0)
        age = 0;
    if (age < 3600)         
        return entry->visits * 16;
    if (age < 86400)         
        return entry->visits * 8;
    if (age < 604800)     
        return entry->visits * 4;
    return entry->visits;    
}


static int frec_better(const FrecEntry *a, const FrecEntry *b, long now)
{
    long sa = frec_score(a, now);
    long sb = frec_score(b, now);

    if (sa != sb)
        return sa > sb;
    if (a->last_time != b->last_time)
        return a->last_time > b->last_time;
    return strcmp(a->path, b->path) < 0;
}

static void frec_record(const char *abs_path)
{
    long now = (long)time(NULL);
    int found = -1;

    if (strlen(abs_path) >= HOP_PATH_MAX)
        return;

    frec_load();

    for (int i = 0; i < frec_count; i++) {
        if (strcmp(frec_table[i].path, abs_path) == 0) {
            found = i;
            break;
        }
    }

    if (found >= 0) {
        frec_table[found].visits++;
        frec_table[found].last_time = now;
    } else {
        if (frec_count >= FREC_MAX_ENTRIES) {
            int worst = 0;
            for (int i = 1; i < frec_count; i++) {
                if (frec_better(&frec_table[worst], &frec_table[i], now) == 1)
                    worst = i;
            }
            found = worst;
        } else {
            found = frec_count;
            frec_count++;
        }
        strcpy(frec_table[found].path, abs_path);
        frec_table[found].visits = 1;
        frec_table[found].last_time = now;
    }

    frec_save();
}

static int path_is_dir(const char *path)
{
    struct stat info;

    if (stat(path, &info) != 0)
        return 0;
    if (S_ISDIR(info.st_mode))
        return 1;
    return 0;
}

int hop_frecency_lookup(const char *name, char *out, int out_size)
{
    long now = (long)time(NULL);
    int used[FREC_MAX_ENTRIES];

    frec_load();
    for (int i = 0; i < frec_count; i++)
        used[i] = 0;

    for (int round = 0; round < frec_count; round++) {
        int best = -1;

        for (int i = 0; i < frec_count; i++) {
            if (used[i] == 1)
                continue;
            if (strstr(frec_table[i].path, name) == NULL)
                continue;
            if (best == -1 || frec_better(&frec_table[i], &frec_table[best], now) == 1)
                best = i;
        }

        if (best == -1)
            return 0;               

        used[best] = 1;

        if (path_is_dir(frec_table[best].path) == 1) {
            if ((int)strlen(frec_table[best].path) >= out_size)
                return 0;
            strcpy(out, frec_table[best].path);
            return 1;
        }
        
    }
    return 0;
}


int hop_change_dir(const char *path)
{
    char old_dir[HOP_PATH_MAX];
    char new_dir[HOP_PATH_MAX];
    int had_old = 1;

    if (getcwd(old_dir, HOP_PATH_MAX) == NULL)
        had_old = 0;

    if (chdir(path) != 0)
        return 0;

    if (had_old == 1) {
        strcpy(previous_dir, old_dir);
        has_previous = 1;
    }

    if (getcwd(new_dir, HOP_PATH_MAX) != NULL)
        frec_record(new_dir);

    return 1;
}

int hop_expand_path(const char *arg, char *out, int out_size)
{
    char *home = shell_home();

    if (strcmp(arg, "~") == 0) {
        if ((int)strlen(home) >= out_size)
            return 0;
        strcpy(out, home);
        return 1;
    }

    if (arg[0] == '~' && arg[1] == '/') {
        int need = (int)strlen(home) + (int)strlen(arg + 1) + 1;
        if (need > out_size)
            return 0;
        strcpy(out, home);
        strcat(out, arg + 1);
        return 1;
    }

    if (strcmp(arg, "-") == 0) {
        if (has_previous == 0)
            return 0;                    
        if ((int)strlen(previous_dir) >= out_size)
            return 0;
        strcpy(out, previous_dir);
        return 1;
    }


    if ((int)strlen(arg) >= out_size)
        return 0;
    strcpy(out, arg);
    return 1;
}



static void hop_one(const char *arg)
{
    char target[HOP_PATH_MAX];
    char match[HOP_PATH_MAX];


    if (strcmp(arg, ".") == 0)
        return;

    if (hop_expand_path(arg, target, HOP_PATH_MAX) == 0)
        return;  


    if (hop_change_dir(target) == 1)
        return;

    if (strcmp(arg, "~") == 0 || strcmp(arg, "..") == 0 || strcmp(arg, "-") == 0
        || arg[0] == '~') {
        builtin_error("hop: no such directory");
        return;
    }

    if (hop_frecency_lookup(arg, match, HOP_PATH_MAX) == 1) {
        if (hop_change_dir(match) == 1)
            return;
    }

    builtin_error("hop: no such directory");
}

int hop_command(int argc, char **argv)
{
    if (argc == 1) {
        if (hop_change_dir(shell_home()) == 0)
            builtin_error("hop: no such directory");
        return 0;
    }

    for (int i = 1; i < argc; i++)
        hop_one(argv[i]);

    return 0;
}