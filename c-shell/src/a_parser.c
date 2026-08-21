#include "a_parser.h"
#include <stdlib.h>

#define PARSE_START 0
#define PARSE_ARGS 1
#define PARSE_NEED_WORD 2
#define PARSE_AFTER_AMP 3

#define EXPECT_NOTHING 0
#define EXPECT_INFILE 1   
#define EXPECT_OUTFILE 2  
#define EXPECT_APPEND 3   
#define EXPECT_STAGE 4    
#define EXPECT_JOB 5     

int parse(TokenList *list, JobList *result)
{
    int state = PARSE_START;
    int expecting = EXPECT_NOTHING;
    Job *job = NULL;
    Command *cmd = NULL;
    int valid = 1;

    joblist_init(result);

    for (int i = 0; i < list->count && valid == 1; i++) {
        int type = list->tokens[i].type;
        char *text = list->tokens[i].text;

        if (state == PARSE_START || state == PARSE_AFTER_AMP) {
            
            if (type != TOK_WORD) {
                valid = 0;
            } else {
                job = joblist_add_job(result);
                if (job == NULL) {
                    valid = 0;
                } else {
                    cmd = job_add_command(job);
                    if (cmd == NULL)
                        valid = 0;
                    else
                        valid = command_add_arg(cmd, text);
                    state = PARSE_ARGS;
                }
            }
        } else if (state == PARSE_ARGS) {
            if (type == TOK_WORD) {
                valid = command_add_arg(cmd, text);
            } else if (type == TOK_LT) {
                expecting = EXPECT_INFILE;
                state = PARSE_NEED_WORD;
            } else if (type == TOK_GT) {
                expecting = EXPECT_OUTFILE;
                state = PARSE_NEED_WORD;
            } else if (type == TOK_GTGT) {
                expecting = EXPECT_APPEND;
                state = PARSE_NEED_WORD;
            } else if (type == TOK_PIPE) {
                expecting = EXPECT_STAGE;
                state = PARSE_NEED_WORD;
            } else if (type == TOK_SEMI) {
                
                job->background = 0;
                job = NULL;
                cmd = NULL;
                expecting = EXPECT_JOB;
                state = PARSE_NEED_WORD;
            } else {
                
                job->background = 1;
                job = NULL;
                cmd = NULL;
                state = PARSE_AFTER_AMP;
            }
        } else {
            if (type != TOK_WORD) {
                valid = 0;
            } else if (expecting == EXPECT_INFILE) {
                valid = command_add_redirect(cmd, REDIR_IN, text);
            } else if (expecting == EXPECT_OUTFILE) {
                valid = command_add_redirect(cmd, REDIR_OUT, text);
            } else if (expecting == EXPECT_APPEND) {
                valid = command_add_redirect(cmd, REDIR_APPEND, text);
            } else if (expecting == EXPECT_STAGE) {
                cmd = job_add_command(job);
                if (cmd == NULL)
                    valid = 0;
                else
                    valid = command_add_arg(cmd, text);
            } else {
                job = joblist_add_job(result);
                if (job == NULL) {
                    valid = 0;
                } else {
                    cmd = job_add_command(job);
                    if (cmd == NULL)
                        valid = 0;
                    else
                        valid = command_add_arg(cmd, text);
                }
            }

            if (valid == 1) {
                expecting = EXPECT_NOTHING;
                state = PARSE_ARGS;
            }
        }
    }

    if (state == PARSE_NEED_WORD && expecting != EXPECT_JOB)
        valid = 0;

    if (valid == 0) {
        joblist_free(result);
        return 0;
    }
    return 1;
}