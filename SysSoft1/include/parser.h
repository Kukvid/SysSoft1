#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include "ast.h"

typedef struct {
    AstNode* root;
    char** errors;
    size_t error_count;
} ParseResult;

ParseResult parse(const char* text);

void parse_result_free(ParseResult* result);

#endif
