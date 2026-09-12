#ifndef D_JOBS_H
#define D_JOBS_H

#include "a_command.h"
#include <sys/types.h>

// How many background/stopped jobs we can track at once, and how long a command name we remember for the status messages
#define JOBS_MAX 256
#define JOB_NAME_MAX 1024

// A job is either running or stopped
typedef enum {
    JS_RUNNING,
    JS_STOPPED
} JobStatus;

// pgid is the process-group id, which equals the pid of the FIRST command in the pipeline (that is the pid)
typedef struct {
    int in_use;                 // is this slot occupied?
    int job_id;                 // session-wide number, never reused
    pid_t pgid;                   //process-group id (== first pid) 
    pid_t pids[MAX_COMMANDS];     // every pid in the pipeline 
    int npids;                  // how many pids we stored
    int nalive;                 // how many are still not reaped 
    JobStatus status;              // running / stopped  
    int abnormal;               // set if any member died by signal 
    char name[JOB_NAME_MAX];     // first command name (e.g. "sleep")
    char cmdline[JOB_NAME_MAX];  // full text (used by Part E)       
} Job_t;

// Call once, right after shell_init(): installs the SIGCHLD handler and clears the table
void jobs_init(void);

// D1/D2 dispatcher. Runs every job in the list in order:
 // 1. background jobs (ended with &) are launched and NOT waited for,
 // 2. foreground jobs are run and waited for,
 // 3. if a foreground command cannot be started at all, print "command not found" and stop the rest of the sequence (D1 rule 3).
 // Pass the raw input line so we can remember each job's command text. 
void jobs_run_sequence(JobList *list, const char *raw_line);

#endif