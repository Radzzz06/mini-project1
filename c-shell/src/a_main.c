#include "a_command.h"
#include "a_input.h"
#include "a_lexer.h"
#include "a_parser.h"
#include "a_prompt.h"
#include "a_shell.h"
#include "d_jobs.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char line[MAX_INPUT_LEN];

    if (shell_init() != 0)
        return 1;

    jobs_init();                 // Part D: SIGCHLD reaper + job table

    while (1) {
        TokenList tokens;
        JobList jobs;
        char *prompt = prompt_build();
        int status;

        write(STDOUT_FILENO, prompt, strlen(prompt));

        status = read_line(line, MAX_INPUT_LEN);
        if (status == INPUT_EOF) 
        {
            printf("\n");
            break;
        }
        if (status == INPUT_ERROR) break;
        if (status == INPUT_TOO_LONG) 
        {
            shell_syntax_error();
            continue;
        }

        if (lex(line, &tokens) == 0) 
        {
            shell_syntax_error();
            continue;
        }
        if (tokens.count == 0) 
        {
            tokenlist_free(&tokens);
            continue;
        }
        if (parse(&tokens, &jobs) == 0) 
        {
            shell_syntax_error();
            tokenlist_free(&tokens);
            continue;
        }

        jobs_run_sequence(&jobs, line);   // Part D: run every job in order

        joblist_free(&jobs);
        tokenlist_free(&tokens);
        fflush(stdout);
    }
    return 0;
}