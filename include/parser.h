#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

int parse_tokens_to_ast(Token *tokens, int token_count, AST **out_root);

#endif /* PARSER_H */
