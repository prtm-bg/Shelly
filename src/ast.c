#include "ast.h"

#include <stdio.h>
#include <stdlib.h>

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
    if (node->file_in) {
        free(node->file_in);
    }
    if (node->file_out) {
        free(node->file_out);
    }
    free(node);
}
