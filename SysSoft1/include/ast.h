#ifndef AST_H
#define AST_H

#include <stddef.h>

typedef enum {
    AST_SOURCE,
    AST_FUNCTION,
    AST_SIGNATURE,
    AST_ARGUMENTS,
    AST_ARGUMENT,
    AST_TYPE,
    AST_ARRAY_TYPE,
    AST_BODY,
    AST_IDENTIFIER,
    AST_BOOL_LITERAL,
    AST_STRING_LITERAL,
    AST_CHAR_LITERAL,
    AST_HEX_LITERAL,
    AST_BITS_LITERAL,
    AST_DEC_LITERAL,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_IF,
    AST_WHILE,
    AST_UNTIL,
    AST_REPEAT,
    AST_BREAK,
    AST_BLOCK,
    AST_EXPRESSION,
    AST_CALL,
    AST_CALL_ARGUMENTS,
    AST_SLICE,
    AST_RANGES,
    AST_RANGE
} AstNodeType;


typedef struct AstNode {
    AstNodeType type;
    char* value;

    struct AstNode** children;
    size_t child_count;
} AstNode;

AstNode* ast_create(AstNodeType type, const char* value);

const char* get_ast_type_name(AstNodeType type);

int add_ast_child(AstNode* parent, AstNode* child);

void ast_free(AstNode* node);

#endif
