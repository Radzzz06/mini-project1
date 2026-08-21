#ifndef PARSER_H
#define PARSER_H

#include "a_command.h"
#include "a_lexer.h"

int parse(TokenList *list, JobList *result);

#endif