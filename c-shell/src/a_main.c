#include "a_command.h"
#include "a_input.h"
#include "a_lexer.h"
#include "a_parser.h"
#include "a_prompt.h"
#include "a_shell.h"
#include "b_builtins.h"
#include "c_exec.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void run_job(Job *job)
{
    for (int i = 0; i < job->command_count; i++) {
        Command *cmd = job->commands[i];

        if (cmd->argc == 0)
            continue;

        if (builtin_is_builtin(cmd->argv[0]) == 1) {
            Redirection redir;
            int saved_in = -1;
            int saved_out = -1;

            if (redir_open(cmd, &redir) == 0)
                continue;            

            if (redir.in_fd >= 0) {
                saved_in = dup(STDIN_FILENO);
                dup2(redir.in_fd, STDIN_FILENO);
            }
            if (redir.out_fd >= 0) {
                fflush(stdout);
                saved_out = dup(STDOUT_FILENO);
                dup2(redir.out_fd, STDOUT_FILENO);
            }

            builtin_run(cmd->argc, cmd->argv);

            fflush(stdout);

            if (saved_in >= 0) {
                dup2(saved_in, STDIN_FILENO);
                close(saved_in);
            }
            if (saved_out >= 0) {
                dup2(saved_out, STDOUT_FILENO);
                close(saved_out);
            }

            redir_finish(&redir);
        } else {
            exec_run_command(cmd);
        }
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

        if (jobs.job_count > 0)
            run_job(jobs.jobs[0]);

        joblist_free(&jobs);
        tokenlist_free(&tokens);
        fflush(stdout);
    }
    return 0;
}