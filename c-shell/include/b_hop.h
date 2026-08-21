#ifndef HOP_H
#define HOP_H

#define HOP_PATH_MAX 4096

int hop_command(int argc, char **argv);
int hop_change_dir(const char *path);
int hop_expand_path(const char *arg, char *out, int out_size);
int hop_frecency_lookup(const char *name, char *out, int out_size);

#endif