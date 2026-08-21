#include "a_command.h"
#include "a_input.h"
#include "a_lexer.h"
#include "a_parser.h"
#include "a_prompt.h"
#include "a_shell.h"
#include "b_builtins.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void run_job(Job *job)
{
    for (int i = 0; i < job->command_count; i++) {
        Command *cmd = job->commands[i];

        if (cmd->argc == 0)
            continue;

        if (builtin_is_builtin(cmd->argv[0]) == 1)
            builtin_run(cmd->argc, cmd->argv);
        else
            fprintf(stderr, "%s: command not found: %s\n", SHELL_NAME,cmd->argv[0]);
    }
}

int main(void)
{
    char line[MAX_INPUT_LEN];

    if (shell_init() != 0)
        return 1;

    while (1) {
        TokenList tokens;
        JobList jobs;
        char *prompt = prompt_build();
        int status;

        write(STDOUT_FILENO, prompt, strlen(prompt));

        status = read_line(line, MAX_INPUT_LEN);
        if (status == INPUT_EOF) {
            printf("\n");
            break;
        }
        if (status == INPUT_ERROR) break;
        if (status == INPUT_TOO_LONG) {
            shell_syntax_error();
            continue;
        }

        if (lex(line, &tokens) == 0) {
            shell_syntax_error();
            continue;
        }
        if (tokens.count == 0) {
            tokenlist_free(&tokens);
            continue;
        }
        if (parse(&tokens, &jobs) == 0) {
            shell_syntax_error();
            tokenlist_free(&tokens);
            continue;
        }

        for (int i = 0; i < jobs.job_count; i++)
            run_job(jobs.jobs[i]);

        joblist_free(&jobs);
        tokenlist_free(&tokens);
        fflush(stdout);
    }
    return 0;
}