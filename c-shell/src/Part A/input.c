#include "input.h"
#include <errno.h>
#include <unistd.h>

int read_line(char *buffer, int size)
{
    int len = 0;
    int too_long = 0;

    if (size <= 0)
        return INPUT_ERROR;

    while (1) {
        char ch;
        int bytes = (int)read(STDIN_FILENO, &ch, 1);

        if (bytes < 0) {
            if (errno == EINTR)
                continue; 
            return INPUT_ERROR;
        }
        if (bytes == 0) {
            if (len == 0 && too_long == 0)
                return INPUT_EOF;
            break;
        }
        if (ch == '\n')
            break;

        if (len + 1 < size)
            buffer[len] = ch;
        else
            too_long = 1;

        if (len + 1 < size)
            len++;
    }

    buffer[len] = '\0';
    if (too_long == 1)
        return INPUT_TOO_LONG;
    return len;
}