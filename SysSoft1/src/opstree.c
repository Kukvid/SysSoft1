
#include "opstree.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static OpNode* convert_node(
    const AstNode* ast,
    OpsTreeStatus* status
);

void opstree_free(OpNode* root)
{
    if (root == NULL)
        return;

    for (size_t i = 0; i < root->child_count; ++i)
        opstree_free(root->children[i]);

    free(root->children);
    free(root->value);
    free(root);
}

static OpNode* create_operation(
    OpKind kind,
    const char* value,
    OpsTreeStatus* status
)
{
    OpNode* node = calloc(1, sizeof(*node));

    if (node == NULL) {
        *status = OPSTREE_OUT_OF_MEMORY;
        return NULL;
    }

    node->kind = kind;

    node->symbol.function_id = ANALYSIS_NO_ID;
    node->symbol.symbol_index = ANALYSIS_NO_ID;

    node->callee_function_id = ANALYSIS_NO_ID;

    if (value != NULL) {
        size_t length = strlen(value) + 1;

        node->value = malloc(length);

        if (node->value == NULL) {
            *status = OPSTREE_OUT_OF_MEMORY;
            opstree_free(node);
            return NULL;
        }

        memcpy(node->value, value, length);
    }

    return node;
}

static int reserve_children(
    OpNode* node,
    size_t count,
    OpsTreeStatus* status
)
{
    if (count > SIZE_MAX / sizeof(*node->children)) {
        *status = OPSTREE_OUT_OF_MEMORY;
        return -1;
    }

    if (count == 0)
        return 0;

    node->children = calloc(
        count,
        sizeof(*node->children)
    );

    if (node->children == NULL) {
        *status = OPSTREE_OUT_OF_MEMORY;
        return -1;
    }

    return 0;
}

static OpNode* convert_regular(
    const AstNode* ast,
    OpKind kind,
    OpsTreeStatus* status
)
{
    if (ast->child_count != 0 && ast->children == NULL) {
        *status = OPSTREE_INVALID_AST;
        return NULL;
    }

    OpNode* node = create_operation(
        kind,
        ast->value,
        status
    );

    if (node == NULL)
        return NULL;

    node->location = (SourceLocation){
       ast->line,
       ast->column
    };

    if (reserve_children(
        node,
        ast->child_count,
        status
    ) != 0) {

        opstree_free(node);
        return NULL;
    }

    for (size_t i = 0; i < ast->child_count; ++i) {

        OpNode* child = convert_node(
            ast->children[i],
            status
        );

        if (child == NULL) {
            opstree_free(node);
            return NULL;
        }

        node->children[node->child_count++] = child;
    }

    return node;
}


static OpNode* convert_with_list(
    const AstNode* ast,
    OpKind kind,
    AstNodeType expected_list,
    OpsTreeStatus* status
)
{
    if (ast->child_count != 2 ||
        ast->children == NULL ||
        ast->children[0] == NULL ||
        ast->children[1] == NULL ||
        ast->children[1]->type != expected_list) {

        *status = OPSTREE_INVALID_AST;
        return NULL;
    }

    const AstNode* list = ast->children[1];

    if (list->child_count != 0 &&
        list->children == NULL) {

        *status = OPSTREE_INVALID_AST;
        return NULL;
    }

    if (list->child_count == SIZE_MAX) {
        *status = OPSTREE_OUT_OF_MEMORY;
        return NULL;
    }

    OpNode* node = create_operation(
        kind,
        NULL,
        status
    );

    if (node == NULL)
        return NULL;

    size_t count = list->child_count + 1;

    if (reserve_children(node, count, status) != 0) {
        opstree_free(node);
        return NULL;
    }

    OpNode* first = convert_node(
        ast->children[0],
        status
    );

    if (first == NULL) {
        opstree_free(node);
        return NULL;
    }

    node->children[node->child_count++] = first;

    for (size_t i = 0; i < list->child_count; ++i) {

        OpNode* child = convert_node(
            list->children[i],
            status
        );

        if (child == NULL) {
            opstree_free(node);
            return NULL;
        }

        node->children[node->child_count++] = child;
    }

    return node;
}

static OpNode* convert_node(
    const AstNode* ast,
    OpsTreeStatus* status
)
{
    if (ast == NULL) {
        *status = OPSTREE_INVALID_AST;
        return NULL;
    }

    if (ast->type == AST_EXPRESSION) {

        if (ast->child_count != 1 ||
            ast->children == NULL) {

            *status = OPSTREE_INVALID_AST;
            return NULL;
        }

        return convert_node(
            ast->children[0],
            status
        );
    }

    switch (ast->type) {

    case AST_IDENTIFIER:
    case AST_BOOL_LITERAL:
    case AST_STRING_LITERAL:
    case AST_CHAR_LITERAL:
    case AST_HEX_LITERAL:
    case AST_BITS_LITERAL:
    case AST_DEC_LITERAL: {

        if (ast->child_count != 0 ||
            ast->value == NULL) {

            *status = OPSTREE_INVALID_AST;
            return NULL;
        }

        OpKind kind = OP_IDENTIFIER;

        switch (ast->type) {

        case AST_BOOL_LITERAL:
            kind = OP_BOOL_LITERAL;
            break;

        case AST_STRING_LITERAL:
            kind = OP_STRING_LITERAL;
            break;

        case AST_CHAR_LITERAL:
            kind = OP_CHAR_LITERAL;
            break;

        case AST_HEX_LITERAL:
            kind = OP_HEX_LITERAL;
            break;

        case AST_BITS_LITERAL:
            kind = OP_BITS_LITERAL;
            break;

        case AST_DEC_LITERAL:
            kind = OP_DEC_LITERAL;
            break;

        default:
            break;
        }

        return convert_regular(
            ast,
            kind,
            status
        );
    }

    case AST_UNARY_OP:

        if (ast->child_count != 1 ||
            ast->value == NULL) {

            *status = OPSTREE_INVALID_AST;
            return NULL;
        }

        return convert_regular(
            ast,
            OP_UNARY,
            status
        );

    case AST_BINARY_OP:

        if (ast->child_count != 2 ||
            ast->value == NULL) {

            *status = OPSTREE_INVALID_AST;
            return NULL;
        }

        return convert_regular(
            ast,
            OP_BINARY,
            status
        );

    case AST_CALL:

        return convert_with_list(
            ast,
            OP_CALL,
            AST_CALL_ARGUMENTS,
            status
        );

    case AST_SLICE:

        return convert_with_list(
            ast,
            OP_SLICE,
            AST_RANGES,
            status
        );

    case AST_RANGE:

        if (!(
            (ast->child_count == 1 &&
                ast->value == NULL) ||

            (ast->child_count == 2 &&
                ast->value != NULL &&
                strcmp(ast->value, "..") == 0)
            )) {

            *status = OPSTREE_INVALID_AST;
            return NULL;
        }

        return convert_regular(
            ast,
            OP_RANGE,
            status
        );

    default:

        *status = OPSTREE_INVALID_AST;
        return NULL;
    }
}

OpNode* opstree_from_ast(
    const AstNode* ast,
    OpsTreeStatus* status
)
{
    OpsTreeStatus current_status = OPSTREE_OK;

    OpNode* result = convert_node(
        ast,
        &current_status
    );

    if (status != NULL)
        *status = current_status;

    return result;
}
