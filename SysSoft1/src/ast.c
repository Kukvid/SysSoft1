
#include "ast.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

AstNode* ast_create(AstNodeType type, const char* value)
{
    AstNode* node = malloc(sizeof(*node));

    if (node == NULL) {
        return NULL;
    }

    node->type = type;
    node->value = NULL;
    node->children = NULL;
    node->child_count = 0;

    if (value != NULL) {
        size_t length = strlen(value) + 1;

        node->value = malloc(length);

        if (node->value == NULL) {
            free(node);
            return NULL;
        }

        memcpy(node->value, value, length);
    }

    return node;
}

int add_ast_child(AstNode* parent, AstNode* child)
{
    if (parent == NULL || child == NULL) {
        return -1;
    }

    if (parent->child_count >= SIZE_MAX / sizeof(*parent->children)) {
        return -1;
    }

    size_t new_count = parent->child_count + 1;

    AstNode** new_children = realloc(
        parent->children,
        new_count * sizeof(*new_children)
    );

    if (new_children == NULL) {
        return -1;
    }

    parent->children = new_children;
    parent->children[parent->child_count] = child;
    parent->child_count = new_count;

    return 0;
}

void ast_free(AstNode* node)
{
    if (node == NULL) {
        return;
    }

    for (size_t i = 0; i < node->child_count; i++) {
        ast_free(node->children[i]);
    }

    free(node->children);
    free(node->value);
    free(node);
}

const char* get_ast_type_name(AstNodeType type)
{
    static const char* const names[] = {
        [AST_SOURCE] = "SOURCE",
        [AST_FUNCTION] = "FUNCTION",
        [AST_SIGNATURE] = "SIGNATURE",
        [AST_ARGUMENTS] = "ARGUMENTS",
        [AST_ARGUMENT] = "ARGUMENT",
        [AST_TYPE] = "TYPE",
        [AST_ARRAY_TYPE] = "ARRAY_TYPE",
        [AST_BODY] = "BODY",
        [AST_IDENTIFIER] = "IDENTIFIER",
        [AST_BOOL_LITERAL] = "BOOL_LITERAL",
        [AST_STRING_LITERAL] = "STRING_LITERAL",
        [AST_CHAR_LITERAL] = "CHAR_LITERAL",
        [AST_HEX_LITERAL] = "HEX_LITERAL",
        [AST_BITS_LITERAL] = "BITS_LITERAL",
        [AST_DEC_LITERAL] = "DEC_LITERAL",
        [AST_BINARY_OP] = "BINARY_OP",
        [AST_UNARY_OP] = "UNARY_OP",
        [AST_IF] = "IF",
        [AST_WHILE] = "WHILE",
        [AST_UNTIL] = "UNTIL",
        [AST_REPEAT] = "REPEAT",
        [AST_BREAK] = "BREAK",
        [AST_BLOCK] = "BLOCK",
        [AST_EXPRESSION] = "EXPRESSION",
        [AST_CALL] = "CALL",
        [AST_CALL_ARGUMENTS] = "CALL_ARGUMENTS",
        [AST_SLICE] = "SLICE",
        [AST_RANGES] = "RANGES",
        [AST_RANGE] = "RANGE"
    };

    if ((unsigned)type >= sizeof(names) / sizeof(names[0])) {
        return "UNKNOWN";
    }

    return names[type] != NULL ? names[type] : "UNKNOWN";
}

