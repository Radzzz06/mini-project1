#ifndef LEXER_H
#define LEXER_H

#define MAX_TOKENS 1025

#define TOK_WORD 0
#define TOK_PIPE 1
#define TOK_AMP 2
#define TOK_SEMI 3
#define TOK_LT 4
#define TOK_GT 5
#define TOK_GTGT 6

typedef struct {
    int type;
    char *text;  
} Token;

typedef struct {
    Token tokens[MAX_TOKENS];
    int count;
} TokenList;


int lex(char *line, TokenList *list);

void tokenlist_free(TokenList *list);

#endif