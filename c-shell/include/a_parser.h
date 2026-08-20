#ifndef PARSER_H
#define PARSER_H

#include "joblist.h"
#include "lexer.h"

int parse(TokenList *list, JobList *result);

#endif