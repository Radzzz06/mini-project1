#ifndef COMMAND_H
#define COMMAND_H

#define MAX_ARGS 520
#define MAX_REDIRECTS 520
#define MAX_COMMANDS 520
#define MAX_JOBS 520

#define REDIR_IN 0      
#define REDIR_OUT 1     
#define REDIR_APPEND 2  


typedef struct {
    int type;
    char *file;
} Redirect;

typedef struct {
    char *argv[MAX_ARGS];  
    int argc;
    Redirect redirects[MAX_REDIRECTS];
    int redirect_count;
} Command;

typedef struct {
    Command *commands[MAX_COMMANDS];
    int command_count;
    int background; 
} Job;

typedef struct {
    Job *jobs[MAX_JOBS];
    int job_count;
} JobList;

void joblist_init(JobList *list);
Job *joblist_add_job(JobList *list);
Command *job_add_command(Job *job);
int command_add_arg(Command *cmd, char *arg);
int command_add_redirect(Command *cmd, int type, char *file);
void joblist_free(JobList *list);

void joblist_dump(JobList *list);

#endif