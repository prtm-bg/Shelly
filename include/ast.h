#ifndef AST_H
#define AST_H

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

AST *create_ast_node(NodeType type, AST *left, AST *right);
void free_ast(AST *node);

#endif /* AST_H */
