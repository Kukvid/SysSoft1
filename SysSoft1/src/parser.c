
#include "parser.h"
#include "grammar.tab.h"
#include "lex.yy.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int parser_add_error(ParseResult* result, const char* message)
{
    if (result == NULL || message == NULL) {
        return -1;
    }

    if (result->error_count >= SIZE_MAX / sizeof(char*)) {
        return -1;
    }

    size_t length = strlen(message) + 1;
    char* copy = malloc(length);

    if (copy == NULL) {
        return -1;
    }

    memcpy(copy, message, length);

    size_t new_count = result->error_count + 1;

    char** new_errors = realloc(
        result->errors,
        new_count * sizeof(char*)
    );

    if (new_errors == NULL) {
        free(copy);
        return -1;
    }

    result->errors = new_errors;
    result->errors[result->error_count] = copy;
    result->error_count = new_count;

    return 0;
}

ParseResult parse(const char* text)
{
    ParseResult result = { 0 };

    if (text == NULL) {
        parser_add_error(&result, "Input text is NULL");
        return result;
    }

    size_t length = strlen(text);

    if (length > INT_MAX) {
        parser_add_error(&result, "Input text is too long");
        return result;
    }

    yyscan_t scanner = NULL;

    if (yylex_init(&scanner) != 0) {
        parser_add_error(&result, "Cannot initialize lexer");
        return result;
    }

    YY_BUFFER_STATE buffer = yy_scan_bytes(
        text,
        (int)length,
        scanner
    );

    if (buffer == NULL) {
        parser_add_error(&result, "Cannot create lexer buffer");
        yylex_destroy(scanner);
        return result;
    }

    int status = yyparse(&result, scanner);

    yy_delete_buffer(buffer, scanner);
    yylex_destroy(scanner);

    if (status != 0 || result.error_count != 0) {
        ast_free(result.root);
        result.root = NULL;

        if (result.error_count == 0) {
            parser_add_error(&result, "Parsing failed");
        }
    }

    return result;
}

void parse_result_free(ParseResult* result)
{
    if (result == NULL) {
        return;
    }

    ast_free(result->root);

    for (size_t i = 0; i < result->error_count; i++) {
        free(result->errors[i]);
    }

    free(result->errors);

    result->root = NULL;
    result->errors = NULL;
    result->error_count = 0;
}
