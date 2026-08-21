#include "a_lexer.h"
#include "a_shell.h"
#include <stdlib.h>
#include <string.h>

#define LEX_EOF -1

#define LEX_START 0
#define LEX_WORD 1
#define LEX_ESCAPE 2
#define LEX_DQUOTE 3
#define LEX_DQ_ESCAPE 4
#define LEX_SQUOTE 5

static int is_space(int ch)
{
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
        return 1;
    return 0;
}

static int is_special(int ch)
{
    if (ch == '|' || ch == '&' || ch == '>' || ch == '<' || ch == ';')
        return 1;
    return 0;
}

static int add_token(TokenList *list, int type, char *text)
{
    if (list->count >= MAX_TOKENS)
        return 0;

    char *copy = strdup(text);
    if (copy == NULL)
        return 0;

    list->tokens[list->count].type = type;
    list->tokens[list->count].text = copy;
    list->count++;
    return 1;
}

void tokenlist_free(TokenList *list)
{
    for (int i = 0; i < list->count; i++)
        free(list->tokens[i].text);
    list->count = 0;
}

int lex(char *line, TokenList *list)
{
    char word[MAX_INPUT_LEN + 1];
    int word_len = 0;
    int state = LEX_START;
    int pos = 0;
    int error = 0;
    int finished = 0;

    list->count = 0;

    while (finished == 0 && error == 0) {
        int ch;
        if (line[pos] != '\0')
            ch = (unsigned char)line[pos];
        else
            ch = LEX_EOF;

        if (state == LEX_START) {
            if (ch == LEX_EOF) {
                finished = 1;
            } else if (is_space(ch) == 1) {
                pos++;  
            } else if (is_special(ch) == 1) {
                if (ch == '>' && line[pos + 1] == '>') {
                    
                    if (add_token(list, TOK_GTGT, ">>") == 0)
                        error = 1;
                    pos = pos + 2;
                } else if (ch == '>') {
                    if (add_token(list, TOK_GT, ">") == 0)
                        error = 1;
                    pos++;
                } else if (ch == '<') {
                    if (add_token(list, TOK_LT, "<") == 0)
                        error = 1;
                    pos++;
                } else if (ch == '|') {
                    if (add_token(list, TOK_PIPE, "|") == 0)
                        error = 1;
                    pos++;
                } else if (ch == '&') {
                    if (add_token(list, TOK_AMP, "&") == 0)
                        error = 1;
                    pos++;
                } else {
                    if (add_token(list, TOK_SEMI, ";") == 0)
                        error = 1;
                    pos++;
                }
            } else {
                state = LEX_WORD;
                word_len = 0;
            }
        } else if (state == LEX_WORD) {
            if (ch == LEX_EOF || is_space(ch) == 1 || is_special(ch) == 1) {
                word[word_len] = '\0';
                if (add_token(list, TOK_WORD, word) == 0)
                    error = 1;
                word_len = 0;
                state = LEX_START;  
            } else if (ch == '\\') {
                state = LEX_ESCAPE;
                pos++;
            } else if (ch == '"') {
                state = LEX_DQUOTE;
                pos++;
            } else if (ch == '\'') {
                state = LEX_SQUOTE;
                pos++;
            } else {
                if (word_len < MAX_INPUT_LEN)
                    word[word_len] = (char)ch;
                word_len++;
                pos++;
            }
        } else if (state == LEX_ESCAPE) {
            if (ch == LEX_EOF) {
                error = 1;  
            } else {
                if (word_len < MAX_INPUT_LEN)
                    word[word_len] = (char)ch;
                word_len++;
                pos++;
                state = LEX_WORD;
            }
        } else if (state == LEX_DQUOTE) {
            if (ch == LEX_EOF) {
                error = 1;  
            } else if (ch == '"') {
                pos++;
                state = LEX_WORD;
            } else if (ch == '\\') {
                pos++;
                state = LEX_DQ_ESCAPE;
            } else {
                if (word_len < MAX_INPUT_LEN)
                    word[word_len] = (char)ch;
                word_len++;
                pos++;
            }
        } else if (state == LEX_DQ_ESCAPE) {
            if (ch == LEX_EOF) {
                error = 1;
            } else if (ch == '"' || ch == '\\') {
                if (word_len < MAX_INPUT_LEN)
                    word[word_len] = (char)ch;
                word_len++;
                pos++;
                state = LEX_DQUOTE;
            } else {
                if (word_len < MAX_INPUT_LEN)
                    word[word_len] = '\\';
                word_len++;
                if (word_len < MAX_INPUT_LEN)
                    word[word_len] = (char)ch;
                word_len++;
                pos++;
                state = LEX_DQUOTE;
            }
        } else {
            if (ch == LEX_EOF) {
                error = 1;  
            } else if (ch == '\'') {
                pos++;
                state = LEX_WORD;
            } else {
                if (word_len < MAX_INPUT_LEN)
                    word[word_len] = (char)ch;
                word_len++;
                pos++;
            }
        }
    }

    if (error == 1) {
        tokenlist_free(list);
        return 0;
    }
    return 1;
}