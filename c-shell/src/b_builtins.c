#include "b_builtins.h"
#include "b_hop.h"
#include "a_shell.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_LEN 4096
#define CHUNK_SIZE 4096


#define BUILTIN_ERRORS_TO_STDOUT 0
#define REVEAL_SHOW_DOT_ENTRIES 0
#define PEEK_NUMBER_SEPARATOR " "


void builtin_error(const char *message)
{
    fflush(stdout);

    if (BUILTIN_ERRORS_TO_STDOUT == 1) {
        printf("%s\n", message);
        fflush(stdout);
    } else {
        fprintf(stderr, "%s\n", message);
    }
}


static int path_join(char *dest, int size, const char *dir, const char *name)
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

static int path_is_dir(const char *path)
{
    struct stat info;

    if (stat(path, &info) != 0)
        return 0;
    return S_ISDIR(info.st_mode) ? 1 : 0;
}

static int path_is_real_dir(const char *path)
{
    struct stat info;

    if (lstat(path, &info) != 0)
        return 0;
    return S_ISDIR(info.st_mode) ? 1 : 0;
}


static int compare_names(const void *a, const void *b)
{
    const char *first = *(const char *const *)a;
    const char *second = *(const char *const *)b;

    return strcmp(first, second);   
}


static int read_sorted_names(const char *dir_path, int show_all,char ***out_names, int *out_count)
{
    DIR *dir = opendir(dir_path);
    struct dirent *entry;
    char **names;
    int count = 0;
    int capacity = 16;

    if (dir == NULL)
        return 0;

    names = malloc((size_t)capacity * sizeof(char *));
    if (names == NULL) {
        closedir(dir);
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        int is_dot = (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0);

        if (is_dot == 1 && REVEAL_SHOW_DOT_ENTRIES == 0)
            continue;
        if (show_all == 0 && entry->d_name[0] == '.')
            continue;

        if (count == capacity) {
            char **bigger = realloc(names, (size_t)capacity * 2 * sizeof(char *));
            if (bigger == NULL)
                break;
            names = bigger;
            capacity = capacity * 2;
        }
        names[count] = strdup(entry->d_name);
        if (names[count] == NULL)
            break;
        count++;
    }
    closedir(dir);

    qsort(names, (size_t)count, sizeof(char *), compare_names);

    *out_names = names;
    *out_count = count;
    return 1;
}


static void reveal_list(const char *dir_path, const char *prefix,int show_all, int recursive)
{
    char **names;
    int count;

    if (read_sorted_names(dir_path, show_all, &names, &count) == 0)
        return;

    for (int i = 0; i < count; i++) {
        char full_path[PATH_LEN];
        int is_dir;

        if (path_join(full_path, PATH_LEN, dir_path, names[i]) == 0)
            continue;

        is_dir = path_is_real_dir(full_path);

        if (recursive == 1 && is_dir == 1) {
            char next_prefix[PATH_LEN];

            printf("%s%s/\n", prefix, names[i]);


            if (path_join(next_prefix, PATH_LEN, prefix, names[i]) == 1) {
                strcat(next_prefix, "/");
                reveal_list(full_path, next_prefix, show_all, recursive);
            }
        } else {
            printf("%s%s\n", prefix, names[i]);
        }
    }

    for (int i = 0; i < count; i++)
        free(names[i]);
    free(names);
}


static int is_flag_group(const char *arg)
{
    if (arg[0] != '-' || arg[1] == '\0')
        return 0;
    return 1;
}

int reveal_command(int argc, char **argv)
{
    int show_all = 0;
    int recursive = 0;
    char *target_arg = NULL;
    char target[PATH_LEN];


    for (int i = 1; i < argc; i++) {
        if (is_flag_group(argv[i]) == 1) {
            for (int j = 1; argv[i][j] != '\0'; j++) {
                if (argv[i][j] == 'a')
                    show_all = 1;
                else if (argv[i][j] == 't')
                    recursive = 1;
                else {
                    builtin_error("reveal: invalid syntax");
                    return 0;
                }
            }
        } else {
            if (target_arg != NULL) {          
                builtin_error("reveal: invalid syntax");
                return 0;
            }
            target_arg = argv[i];
        }
    }

    if (target_arg == NULL) {
        strcpy(target, ".");
    } else if (hop_expand_path(target_arg, target, PATH_LEN) == 0) {
        builtin_error("reveal: no such directory");
        return 0;
    }

    if (path_is_dir(target) == 0) {
        builtin_error("reveal: no such directory");
        return 0;
    }

    reveal_list(target, "", show_all, recursive);
    fflush(stdout);
    return 0;
}


typedef struct {
    char *data;
    int len;
    int capacity;
} Buffer;

static void buffer_init(Buffer *buf)
{
    buf->data = NULL;
    buf->len = 0;
    buf->capacity = 0;
}

static void buffer_free(Buffer *buf)
{
    free(buf->data);
    buffer_init(buf);
}

static int buffer_append(Buffer *buf, const char *bytes, int count)
{
    if (count <= 0)
        return 1;

    if (buf->len + count > buf->capacity) {
        int new_cap = (buf->capacity == 0) ? 256 : buf->capacity;
        char *bigger;

        while (new_cap < buf->len + count)
            new_cap = new_cap * 2;

        bigger = realloc(buf->data, (size_t)new_cap);
        if (bigger == NULL)
            return 0;
        buf->data = bigger;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->len, bytes, (size_t)count);
    buf->len += count;
    return 1;
}

static void print_line(const char *text, int len, int number, int add_newline)
{
    if (number > 0)
        printf("%d%s", number, PEEK_NUMBER_SEPARATOR);

    fwrite(text, 1, (size_t)len, stdout);

    if (add_newline == 1)
        printf("\n");
}

static int read_exactly(int fd, char *dest, int count)
{
    int got = 0;

    while (got < count) {
        ssize_t n = read(fd, dest + got, (size_t)(count - got));
        if (n <= 0)
            break;
        got += (int)n;
    }
    return got;
}

static void peek_forward(int fd, int flag_n, int *counter)
{
    char chunk[CHUNK_SIZE];
    Buffer line;
    ssize_t n;

    buffer_init(&line);

    while ((n = read(fd, chunk, CHUNK_SIZE)) > 0) {
        for (int i = 0; i < (int)n; i++) {
            if (chunk[i] == '\n') {
                int number = 0;
                if (flag_n == 1 && line.len > 0) {
                    *counter = *counter + 1;
                    number = *counter;
                }
                print_line(line.data, line.len, number, 1);
                line.len = 0;
            } else {
                buffer_append(&line, &chunk[i], 1);
            }
        }
    }

    if (line.len > 0) {
        int number = 0;
        if (flag_n == 1) {
            *counter = *counter + 1;
            number = *counter;
        }
        print_line(line.data, line.len, number, 0);
    }
    buffer_free(&line);
}

static int count_non_empty_lines(int fd)
{
    char chunk[CHUNK_SIZE];
    ssize_t n;
    int count = 0;
    int current_len = 0;

    if (lseek(fd, 0, SEEK_SET) < 0)
        return 0;

    while ((n = read(fd, chunk, CHUNK_SIZE)) > 0) {
        for (int i = 0; i < (int)n; i++) {
            if (chunk[i] == '\n') {
                if (current_len > 0)
                    count++;
                current_len = 0;
            } else {
                current_len++;
            }
        }
    }
    if (current_len > 0)
        count++;
    return count;
}

static void peek_reverse_seekable(int fd, int flag_n, int *counter)
{
    char chunk[CHUNK_SIZE];
    Buffer partial;
    off_t size;
    off_t pos;
    int number = 0;

    size = lseek(fd, 0, SEEK_END);
    if (size <= 0)
        return;                      

    if (flag_n == 1) {
        int total = count_non_empty_lines(fd);
        number = *counter + total;    
        *counter = *counter + total;  
    }


    {
        char last;
        if (lseek(fd, size - 1, SEEK_SET) >= 0 && read(fd, &last, 1) == 1) {
            if (last == '\n')
                size--;
        }
    }

    buffer_init(&partial);
    pos = size;

    while (pos > 0) {
        int n = (pos < CHUNK_SIZE) ? (int)pos : CHUNK_SIZE;
        int segment_end;
        int partial_used = 0;
        Buffer rest;

        pos = pos - n;
        if (lseek(fd, pos, SEEK_SET) < 0)
            break;
        n = read_exactly(fd, chunk, n);
        if (n <= 0)
            break;

        segment_end = n;

        for (int i = n - 1; i >= 0; i--) {
            if (chunk[i] != '\n')
                continue;

            Buffer line;
            buffer_init(&line);
            buffer_append(&line, chunk + i + 1, segment_end - i - 1);
            if (partial_used == 0) {
                buffer_append(&line, partial.data, partial.len);
                partial.len = 0;
                partial_used = 1;
            }

            if (flag_n == 1 && line.len > 0) {
                print_line(line.data, line.len, number, 1);
                number--;
            } else {
                print_line(line.data, line.len, 0, 1);
            }
            buffer_free(&line);

            segment_end = i;          
        }


        buffer_init(&rest);
        buffer_append(&rest, chunk, segment_end);
        if (partial_used == 0)
            buffer_append(&rest, partial.data, partial.len);
        buffer_free(&partial);
        partial = rest;
    }

    if (flag_n == 1 && partial.len > 0)
        print_line(partial.data, partial.len, number, 1);
    else
        print_line(partial.data, partial.len, 0, 1);

    buffer_free(&partial);
}

static void peek_reverse_stream(int fd, int flag_n, int *counter)
{
    Buffer all;
    char chunk[CHUNK_SIZE];
    ssize_t n;
    int *starts;
    int *lengths;
    int count = 0;
    int capacity = 64;
    int start = 0;
    int number;

    buffer_init(&all);
    while ((n = read(fd, chunk, CHUNK_SIZE)) > 0)
        buffer_append(&all, chunk, (int)n);

    if (all.len == 0) {
        buffer_free(&all);
        return;
    }

    starts = malloc((size_t)capacity * sizeof(int));
    lengths = malloc((size_t)capacity * sizeof(int));
    if (starts == NULL || lengths == NULL) {
        free(starts);
        free(lengths);
        buffer_free(&all);
        return;
    }

    for (int i = 0; i <= all.len; i++) {
        int end_of_line = (i == all.len) || (all.data[i] == '\n');

        if (end_of_line == 0)
            continue;
        if (i == all.len && i == start)
            break;                   

        if (count == capacity) {
            int *s2 = realloc(starts, (size_t)capacity * 2 * sizeof(int));
            int *l2 = realloc(lengths, (size_t)capacity * 2 * sizeof(int));
            if (s2 == NULL || l2 == NULL) {
                free(s2 == NULL ? starts : s2);
                free(l2 == NULL ? lengths : l2);
                buffer_free(&all);
                return;
            }
            starts = s2;
            lengths = l2;
            capacity = capacity * 2;
        }
        starts[count] = start;
        lengths[count] = i - start;
        count++;
        start = i + 1;
    }

    number = *counter;
    for (int i = 0; i < count; i++) {
        if (lengths[i] > 0)
            number++;
    }
    *counter = number;

    for (int i = count - 1; i >= 0; i--) {
        if (flag_n == 1 && lengths[i] > 0) {
            print_line(all.data + starts[i], lengths[i], number, 1);
            number--;
        } else {
            print_line(all.data + starts[i], lengths[i], 0, 1);
        }
    }

    free(starts);
    free(lengths);
    buffer_free(&all);
}

static void peek_one_input(int fd, int seekable, int flag_n, int flag_r,
                           int *counter)
{
    if (flag_r == 0)
        peek_forward(fd, flag_n, counter);
    else if (seekable == 1)
        peek_reverse_seekable(fd, flag_n, counter);
    else
        peek_reverse_stream(fd, flag_n, counter);
}

int peek_command(int argc, char **argv)
{
    int flag_n = 0;
    int flag_r = 0;
    int file_count = 0;
    int counter = 0;                   
    char *files[MAX_INPUT_LEN];

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (int j = 1; argv[i][j] != '\0'; j++) {
                if (argv[i][j] == 'n')
                    flag_n = 1;
                else if (argv[i][j] == 'r')
                    flag_r = 1;
                else {
                    builtin_error("peek: invalid syntax");
                    return 0;
                }
            }
        } else {
            if (file_count < MAX_INPUT_LEN) {
                files[file_count] = argv[i];  
                file_count++;
            }
        }
    }

    if (file_count == 0) {
        peek_one_input(STDIN_FILENO, 0, flag_n, flag_r, &counter);
        fflush(stdout);
        return 0;
    }

    for (int i = 0; i < file_count; i++) {
        int fd;
        struct stat info;

        if (strcmp(files[i], "-") == 0) {
            peek_one_input(STDIN_FILENO, 0, flag_n, flag_r, &counter);
            continue;
        }

        if (stat(files[i], &info) != 0) {
            builtin_error("peek: no such file or directory");
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            builtin_error("peek: is a directory");
            continue;
        }

        fd = open(files[i], O_RDONLY);
        if (fd < 0) {
            builtin_error("peek: no such file or directory");
            continue;
        }

        peek_one_input(fd, S_ISREG(info.st_mode) ? 1 : 0, flag_n, flag_r,&counter);
        close(fd);
    }

    fflush(stdout);
    return 0;
}

static int is_executable_file(const char *path)
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

static int make_absolute(const char *dir, const char *cwd, char *out, int size)
{
    if (dir[0] == '/') {
        if ((int)strlen(dir) >= size)
            return 0;
        strcpy(out, dir);
        return 1;
    }
    if (strcmp(dir, ".") == 0 || dir[0] == '\0') {
        if ((int)strlen(cwd) >= size)
            return 0;
        strcpy(out, cwd);
        return 1;
    }
    return path_join(out, size, cwd, dir);
}

static int locate_one(const char *name)
{
    char cwd[PATH_LEN];
    char candidate[PATH_LEN];
    char *path_env;
    char *path_copy;
    char *cursor;
    int found = 0;

    if (getcwd(cwd, PATH_LEN) == NULL)
        strcpy(cwd, ".");

    if (path_join(candidate, PATH_LEN, cwd, name) == 1
        && is_executable_file(candidate) == 1) {
        printf("%s\n", candidate);
        found = 1;
    }

    path_env = getenv("PATH");
    if (path_env == NULL)
        return found;

    path_copy = strdup(path_env);
    if (path_copy == NULL)
        return found;

    cursor = path_copy;
    while (cursor != NULL && *cursor != '\0') {
        char *colon = strchr(cursor, ':');
        char dir_abs[PATH_LEN];

        if (colon != NULL)
            *colon = '\0';

        if (make_absolute(cursor, cwd, dir_abs, PATH_LEN) == 1
            && path_join(candidate, PATH_LEN, dir_abs, name) == 1
            && is_executable_file(candidate) == 1) {
            printf("%s\n", candidate);
            found = 1;
        }

        cursor = (colon == NULL) ? NULL : colon + 1;
    }
    free(path_copy);
    return found;
}

int locate_command(int argc, char **argv)
{
    if (argc < 2) {
        builtin_error("locate: invalid syntax");
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (locate_one(argv[i]) == 0) {
            char message[PATH_LEN];

            strcpy(message, "locate: command not found (");
            if (strlen(argv[i]) < PATH_LEN - 64)
                strcat(message, argv[i]);
            strcat(message, ")");
            builtin_error(message);
        }
    }
    fflush(stdout);
    return 0;
}


int builtin_is_builtin(const char *name)
{
    if (strcmp(name, "hop") == 0)
        return 1;
    if (strcmp(name, "reveal") == 0)
        return 1;
    if (strcmp(name, "peek") == 0)
        return 1;
    if (strcmp(name, "locate") == 0)
        return 1;
    return 0;
}

int builtin_run(int argc, char **argv)
{
    if (argc <= 0 || argv[0] == NULL)
        return 0;

    if (strcmp(argv[0], "hop") == 0) return hop_command(argc, argv);
    if (strcmp(argv[0], "reveal") == 0) return reveal_command(argc, argv);
    if (strcmp(argv[0], "peek") == 0) return peek_command(argc, argv);
    if (strcmp(argv[0], "locate") == 0) return locate_command(argc, argv);
    return 0;
}