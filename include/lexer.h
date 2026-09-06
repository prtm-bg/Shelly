#ifndef LEXER_H
#define LEXER_H

/* Tokenizer */
typedef enum {
    TOK_WORD,
    TOK_PIPE,
    TOK_AND,
    TOK_OR,
    TOK_SEMI,
    TOK_REDIR_IN,
    TOK_REDIR_OUT,
    TOK_REDIR_APPEND,
    TOK_BG,
    TOK_EOF,
    TOK_INVALID
} TokenType;

typedef struct {
    TokenType type;
    char *text;
} Token;

char *dup_n(const char *src, int n);
int push_token(Token **tokens, int *count, int *cap, TokenType type, const char *start, int n);
int tokenize_input(const char *input, Token **out_tokens, int *out_count);
void free_tokens(Token *tokens, int count);

#endif /* LEXER_H */
