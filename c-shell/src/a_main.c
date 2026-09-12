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
    int eof_warned = 0;          // armed after "there are stopped jobs"

    if (shell_init() != 0)
        return 1;

    jobs_init();

    while (1) {
        TokenList tokens;
        JobList jobs;
        char *prompt = prompt_build();
        int status;

        write(STDOUT_FILENO, prompt, strlen(prompt));

        status = read_line(line, MAX_INPUT_LEN);

        if (status == INPUT_INT) {            // Ctrl-C: fresh prompt 
            eof_warned = 0;
            continue;
        }
        if (status == INPUT_EOF) {            // Ctrl-D on empty line
            if (jobs_has_stopped() && eof_warned == 0) {
                fprintf(stderr, "cshell: there are stopped jobs\n");
                eof_warned = 1;               // second Ctrl-D will exit
                continue;
            }
            jobs_hangup_all();                // SIGHUP tracked jobs (E2 r10)
            printf("\n");
            break;
        }
        eof_warned = 0;                       // any real input disarms it

        if (status == INPUT_ERROR) break;
        if (status == INPUT_TOO_LONG) { shell_syntax_error(); continue; }

        if (lex(line, &tokens) == 0) { shell_syntax_error(); continue; }
        if (tokens.count == 0) { tokenlist_free(&tokens); continue; }
        if (parse(&tokens, &jobs) == 0) {
            shell_syntax_error();
            tokenlist_free(&tokens);
            continue;
        }

        jobs_run_sequence(&jobs, line);

        joblist_free(&jobs);
        tokenlist_free(&tokens);
        fflush(stdout);
    }
    return 0;
}