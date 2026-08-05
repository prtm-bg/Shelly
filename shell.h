#ifndef SHELL_H
#define SHELL_H

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_PATH 512
#define MAX_SUB_CMD_SIZE 128
#define MAX_CMD_SIZE 64

/* ANSI Color & Style Codes */
#define COL_RESET   "\033[0m"
#define COL_BOLD    "\033[1m"
#define COL_DIM     "\033[2m"
#define COL_RED     "\033[31m"
#define COL_GREEN   "\033[32m"
#define COL_YELLOW  "\033[33m"
#define COL_BLUE    "\033[34m"
#define COL_MAGENTA "\033[35m"
#define COL_CYAN    "\033[36m"
#define COL_WHITE   "\033[37m"
#define COL_BRED    "\033[1;31m"
#define COL_BGREEN  "\033[1;32m"
#define COL_BYELLOW "\033[1;33m"
#define COL_BBLUE   "\033[1;34m"
#define COL_BCYAN   "\033[1;36m"

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

/* Abstract Syntax Tree (AST) */
typedef enum { NODE_COMMAND, NODE_PIPE, NODE_AND, NODE_OR, NODE_SEMI } NodeType;
typedef struct AST {
  NodeType type;
  struct AST *left;
  struct AST *right;
  char **argv;
  int argc;
  char *file_in;
  char *file_out;
  int append_out;
  int background;
} AST;

/* Global Variables */
extern int g_has_shell_home;
extern char g_shell_home[MAX_PATH];

/* Input Function */
char *read_line(void);

/* Lexer Functions */
char *dup_n(const char *src, int n);
int push_token(Token **tokens, int *count, int *cap, TokenType type, const char *start, int n);
int tokenize_input(const char *input, Token **out_tokens, int *out_count);
void free_tokens(Token *tokens, int count);

/* Parser Functions */
AST *create_ast_node(NodeType type, AST *left, AST *right);
void free_ast(AST *node);
int parse_tokens_to_ast(Token *tokens, int token_count, AST **out_root);

/* Executor Functions */
int execute_ast(AST *node);

#endif /* SHELL_H */
