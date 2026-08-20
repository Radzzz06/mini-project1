#include "command.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void joblist_init(JobList *list)
{
    list->job_count = 0;
}

Job *joblist_add_job(JobList *list)
{
    if (list->job_count >= MAX_JOBS)
        return NULL;

    Job *job = malloc(sizeof(Job));
    if (job == NULL)
        return NULL;

    job->command_count = 0;
    job->background = 0;

    list->jobs[list->job_count] = job;
    list->job_count++;
    return job;
}

Command *job_add_command(Job *job)
{
    if (job->command_count >= MAX_COMMANDS)
        return NULL;

    Command *cmd = malloc(sizeof(Command));
    if (cmd == NULL)
        return NULL;

    cmd->argc = 0;
    cmd->argv[0] = NULL;
    cmd->redirect_count = 0;

    job->commands[job->command_count] = cmd;
    job->command_count++;
    return cmd;
}

int command_add_arg(Command *cmd, char *arg)
{
    if (cmd->argc + 1 >= MAX_ARGS)
        return 0;

    char *copy = strdup(arg);
    if (copy == NULL)
        return 0;

    cmd->argv[cmd->argc] = copy;
    cmd->argc++;
    cmd->argv[cmd->argc] = NULL;
    return 1;
}

int command_add_redirect(Command *cmd, int type, char *file)
{
    if (cmd->redirect_count >= MAX_REDIRECTS)
        return 0;

    char *copy = strdup(file);
    if (copy == NULL)
        return 0;

    cmd->redirects[cmd->redirect_count].type = type;
    cmd->redirects[cmd->redirect_count].file = copy;
    cmd->redirect_count++;
    return 1;
}

void joblist_free(JobList *list)
{
    for (int i = 0; i < list->job_count; i++) {
        Job *job = list->jobs[i];

        for (int j = 0; j < job->command_count; j++) {
            Command *cmd = job->commands[j];

            for (int k = 0; k < cmd->argc; k++)
                free(cmd->argv[k]);
            for (int k = 0; k < cmd->redirect_count; k++)
                free(cmd->redirects[k].file);
            free(cmd);
        }
        free(job);
    }
    list->job_count = 0;
}

static char *redir_arrow(int type)
{
    if (type == REDIR_IN)
        return "<";
    if (type == REDIR_OUT)
        return ">";
    return ">>";
}

void joblist_dump(JobList *list)
{
    for (int i = 0; i < list->job_count; i++) {
        Job *job = list->jobs[i];

        if (job->background == 1)
            printf("job %d [background]\n", i+1);
        else
            printf("job %d [foreground]\n", i + 1);

        for (int j = 0; j < job->command_count; j++) {
            Command *cmd = job->commands[j];

            printf("  stage %d: argv =", j+1);
            for (int k = 0; k < cmd->argc; k++)
                printf(" [%s]", cmd->argv[k]);
            printf("\n");

            for (int k = 0; k < cmd->redirect_count; k++)
                printf("           redirect %s [%s]\n",
                       redir_arrow(cmd->redirects[k].type),
                       cmd->redirects[k].file);
        }
    }
    fflush(stdout);
}