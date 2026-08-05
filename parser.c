#include "shell.h"   /* Parser */
typedef struct {
  Token *tokens;
  int count;
  int pos;
} Parser;

/* AST functions */

/* AST node creation */
AST *create_ast_node(NodeType type, AST *left, AST *right) {
  AST *node = (AST *)calloc(1, sizeof(AST));
  if (node == NULL) {
    perror("calloc() failed");
    exit(1);
  }
  node->type = type;
  node->left = left;
  node->right = right;
  node->argv = NULL;
  node->argc = 0;
  node->file_in = NULL;
  node->file_out = NULL;
  node->append_out = 0;
  node->background = 0;

  return node;
}
/* Free AST recursively */
void free_ast(AST *node) {
  int i;

  if (node == NULL) {
    return;
  }
  free_ast(node->left);
  free_ast(node->right);

  if (node->argv != NULL) {
    for (i = 0; i < node->argc; i++) {
      free(node->argv[i]);
    }
    free(node->argv);
  }
  if (node->file_in)
    free(node->file_in);
  if (node->file_out)
    free(node->file_out);
  free(node);
}

/* Parser functions*/

/* Peek at the current token */
Token *parser_peek(Parser *p) { return &p->tokens[p->pos]; }
/* Match the current token with the expected type */
int parser_match(Parser *p, TokenType type) {
  if (parser_peek(p)->type == type) {
    p->pos++;
    return 1;
  }
  return 0;
}
/* Parse a command (a sequence of words) */
#ifdef _MSC_VER
#define strdup _strdup
#endif

AST *parse_command(Parser *p) {
  int cap = 8;
  int argc = 0;
  char **argv = (char **)malloc((size_t)cap * sizeof(char *));
  AST *node;
  char *file_in = NULL;
  char *file_out = NULL;
  int append_out = 0;

  if (argv == NULL) {
    perror("malloc() failed");
    return NULL;
  }

  while (parser_peek(p)->type == TOK_WORD ||
         parser_peek(p)->type == TOK_REDIR_IN ||
         parser_peek(p)->type == TOK_REDIR_OUT ||
         parser_peek(p)->type == TOK_REDIR_APPEND) {

    Token *tok = parser_peek(p);

    if (tok->type == TOK_REDIR_IN || tok->type == TOK_REDIR_OUT ||
        tok->type == TOK_REDIR_APPEND) {
      TokenType rtype = tok->type;
      p->pos++;
      if (parser_peek(p)->type != TOK_WORD) {
        fprintf(stderr, COL_BRED "parse error: " COL_RESET "expected file for redirection\n");
        for (int k = 0; k < argc; k++) {
          free(argv[k]);
        }
        free(argv);
        if (file_in)
          free(file_in);
        if (file_out)
          free(file_out);
        return NULL;
      }
      if (rtype == TOK_REDIR_IN) {
        if (file_in)
          free(file_in);
        file_in = strdup(parser_peek(p)->text);
      } else {
        if (file_out)
          free(file_out);
        file_out = strdup(parser_peek(p)->text);
        append_out = (rtype == TOK_REDIR_APPEND);
      }
      p->pos++;
      continue;
    }

    /* It's a TOK_WORD */
    int len = (int)strlen(tok->text);

    if (argc + 1 >= cap) {
      cap *= 2;
      argv = (char **)realloc(argv, (size_t)cap * sizeof(char *));
      if (argv == NULL) {
        perror("realloc() failed");
        for (int k = 0; k < argc; k++) {
          free(argv[k]);
        }
        if (file_in)
          free(file_in);
        if (file_out)
          free(file_out);
        return NULL;
      }
    }
    argv[argc++] = dup_n(tok->text, len);
    p->pos++;
  }

  if (argc == 0 && file_in == NULL && file_out == NULL) {
    free(argv);
    return NULL;
  }

  argv[argc] = NULL;
  node = create_ast_node(NODE_COMMAND, NULL, NULL);
  node->argv = argv;
  node->argc = argc;
  node->file_in = file_in;
  node->file_out = file_out;
  node->append_out = append_out;
  return node;
}
/* Parsing pipe expression */
AST *parse_pipeline(Parser *p) {
  AST *left = parse_command(p);

  if (left == NULL) {
    return NULL;
  }
  while (parser_match(p, TOK_PIPE)) {
    AST *right = parse_command(p);
    if (right == NULL) {
      free_ast(left);
      return NULL;
    }
    left = create_ast_node(NODE_PIPE, left, right);
  }

  return left;
}
/* Parse &&/|| expressions */
AST *parse_and_or(Parser *p) {
  AST *left = parse_pipeline(p);

  if (left == NULL) {
    return NULL;
  }
  while (parser_peek(p)->type == TOK_AND || parser_peek(p)->type == TOK_OR) {
    TokenType op = parser_peek(p)->type;
    AST *right;

    p->pos++;
    right = parse_pipeline(p);
    if (right == NULL) {
      free_ast(left);
      return NULL;
    }

    if (op == TOK_AND) {
      left = create_ast_node(NODE_AND, left, right);
    } else {
      left = create_ast_node(NODE_OR, left, right);
    }
  }

  return left;
}
/* Parsing sequence separated by ; or & */
AST *parse_sequence(Parser *p) {
  AST *left = parse_and_or(p);

  if (left == NULL) {
    return NULL;
  }

  if (parser_peek(p)->type == TOK_BG) {
    left->background = 1;
    p->pos++;
  } else if (parser_peek(p)->type == TOK_SEMI) {
    p->pos++;
  }

  while (parser_peek(p)->type != TOK_EOF) {
    AST *right = parse_and_or(p);
    if (right == NULL) {
      /* Trailing separator is allowed */
      break;
    }

    if (parser_peek(p)->type == TOK_BG) {
      right->background = 1;
      p->pos++;
    } else if (parser_peek(p)->type == TOK_SEMI) {
      p->pos++;
    }

    left = create_ast_node(NODE_SEMI, left, right);
  }

  return left;
}
/* Parsing tokens into AST */
int parse_tokens_to_ast(Token *tokens, int token_count, AST **out_root) {
  Parser p;
  AST *root;
  int i;

  for (i = 0; i < token_count; i++) {
    if (tokens[i].type == TOK_INVALID) {
      fprintf(stderr, COL_BRED "parse error: " COL_RESET "invalid token '%s'\n", tokens[i].text);
      return -1;
    }
  }

  p.tokens = tokens;
  p.count = token_count;
  p.pos = 0;

  root = parse_sequence(&p);
  if (root == NULL) {
    fprintf(stderr, COL_BRED "parse error: " COL_RESET "invalid command syntax\n");
    return -1;
  }

  if (parser_peek(&p)->type != TOK_EOF) {
    fprintf(stderr, COL_BRED "parse error: " COL_RESET "extra tokens after command\n");
    free_ast(root);
    return -1;
  }

  *out_root = root;
  return 0;
}

