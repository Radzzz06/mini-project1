#ifndef INPUT_H
#define INPUT_H

#define INPUT_EOF -1       
#define INPUT_TOO_LONG -2  
#define INPUT_ERROR -3     
#define INPUT_INT -4       // line cancelled by Ctrl-C

int read_line(char *buffer, int size);

#endif