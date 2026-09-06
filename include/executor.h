#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "ast.h"

int execute_ast(AST *node);

/* Builtin commands */
int builtin_cd(char **argv);
int builtin_pwd(void);
int builtin_history(char **argv, int argc);

#endif /* EXECUTOR_H */
