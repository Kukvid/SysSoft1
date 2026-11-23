
#include "analysis.h"
#include "cfg.h"

#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static char* copy_string(const char* text)
{
    if (text == NULL) return NULL;

    size_t length = strlen(text) + 1;
    char* copy = malloc(length);

    if (copy != NULL)
        memcpy(copy, text, length);

    return copy;
}

static int no_memory(AnalysisResult* result)
{
    result->status = ANALYSIS_OUT_OF_MEMORY;
    return -1;
}

static int add_error(AnalysisResult* result, size_t file_index,
    const char* message)
{
    if (result->error_count >= SIZE_MAX / sizeof(*result->errors))
        return no_memory(result);

    char* text = copy_string(message);
    if (text == NULL)
        return no_memory(result);

    size_t count = result->error_count + 1;
    AnalysisError* errors =
        realloc(result->errors, count * sizeof(*errors));

    if (errors == NULL) {
        free(text);
        return no_memory(result);
    }

    result->errors = errors;

    result->errors[result->error_count] = (AnalysisError){
        .file_index = file_index,
        .location = {0, 0},
        .message = text
    };

    result->error_count = count;

    if (result->status == ANALYSIS_OK)
        result->status = ANALYSIS_HAS_ERRORS;

    return 0;
}

static int add_error_at(
    AnalysisResult* result,
    size_t file_index,
    const AstNode* node,
    const char* message
)
{
    if (add_error(result, file_index, message) != 0)
        return -1;

    if (node != NULL) {
        AnalysisError* error =
            &result->errors[result->error_count - 1];

        error->location = (SourceLocation){
            node->line,
            node->column
        };
    }

    return 0;
}

static void type_free(TypeInfo* type)
{
    if (type == NULL)
        return;

    type_free(type->element_type);
    free(type->name);
    free(type);
}

static void operation_free(OpNode* node)
{
    if (node == NULL)
        return;

    for (size_t i = 0; i < node->child_count; ++i)
        operation_free(node->children[i]);

    free(node->children);
    free(node->value);
    free(node);
}

static void graph_free(CfgGraph* graph)
{
    for (size_t i = 0; i < graph->block_count; ++i) {
        CfgBlock* block = graph->blocks[i];

        if (block == NULL)
            continue;

        for (size_t j = 0; j < block->operation_count; ++j)
            operation_free(block->operations[j]);

        free(block->operations);
        operation_free(block->condition);
        free(block);
    }

    free(graph->blocks);
    *graph = (CfgGraph){ 0 };
}

static void function_free(FunctionInfo* function)
{
    if (function == NULL)
        return;

    graph_free(&function->graph);

    for (size_t i = 0; i < function->symbol_count; ++i) {
        free(function->symbols[i].name);
        type_free(function->symbols[i].declared_type);
    }

    free(function->symbols);
    free(function->signature.parameter_symbol_indices);
    type_free(function->signature.return_type);

    free(function->name);
    free(function->qualified_name);
    free(function);
}

void analysis_result_free(AnalysisResult* result)
{
    if (result == NULL)
        return;

    for (size_t i = 0; i < result->file_count; ++i)
        free(result->files[i].path);

    free(result->files);

    for (size_t i = 0; i < result->function_count; ++i)
        function_free(result->functions[i]);

    free(result->functions);
    free(result->scopes);
    free(result->calls);

    for (size_t i = 0; i < result->error_count; ++i)
        free(result->errors[i].message);

    free(result->errors);

    *result = (AnalysisResult){ 0 };
}

static int is_builtin(const char* name)
{
    static const char* const names[] = {
        "bool", "byte", "int", "uint",
        "long", "ulong", "char", "string"
    };

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(name, names[i]) == 0)
            return 1;
    }

    return 0;
}

static int copy_type(const AstNode* ast, TypeInfo** output)
{
    *output = NULL;

    if (ast == NULL)
        return 0;

    if (ast->type != AST_TYPE && ast->type != AST_ARRAY_TYPE)
        return 1;

    TypeInfo* type = calloc(1, sizeof(*type));
    if (type == NULL)
        return -1;

    int status = 0;

    if (ast->type == AST_TYPE) {
        if (ast->value == NULL) {
            status = 1;
        }
        else {
            type->kind = is_builtin(ast->value)
                ? TYPE_BUILTIN
                : TYPE_CUSTOM;

            type->name = copy_string(ast->value);

            if (type->name == NULL)
                status = -1;
        }
    }
    else {
        type->kind = TYPE_ARRAY;

        if (ast->child_count != 2 ||
            ast->children[1] == NULL ||
            ast->children[1]->value == NULL) {
            status = 1;
        }
        else {
            const char* size_text = ast->children[1]->value;

            errno = 0;
            char* end = NULL;

            uintmax_t number = strtoumax(size_text, &end, 10);

            if (errno == ERANGE ||
                end == size_text ||
                *end != '\0' ||
                number > SIZE_MAX) {
                status = 1;
            }
            else {
                type->array_size = (size_t)number;

                status = copy_type(
                    ast->children[0],
                    &type->element_type
                );

                if (status == 0 && type->element_type == NULL)
                    status = 1;
            }
        }
    }

    if (status != 0) {
        type_free(type);
        return status;
    }

    *output = type;
    return 0;
}

static int copy_signature(const AstNode* signature,
    FunctionInfo* function)
{
    if (signature == NULL ||
        signature->type != AST_SIGNATURE ||
        signature->child_count < 2 ||
        signature->children[1] == NULL ||
        signature->children[1]->type != AST_ARGUMENTS)
        return 1;

    const AstNode* args = signature->children[1];

    if (args->child_count > SIZE_MAX / sizeof(SymbolInfo) ||
        args->child_count > SIZE_MAX / sizeof(size_t))
        return -1;

    if (args->child_count != 0) {
        function->symbols = calloc(
            args->child_count,
            sizeof(*function->symbols)
        );

        function->signature.parameter_symbol_indices = malloc(
            args->child_count * sizeof(size_t)
        );

        if (function->symbols == NULL ||
            function->signature.parameter_symbol_indices == NULL)
            return -1;
    }

    for (size_t i = 0; i < args->child_count; ++i) {
        const AstNode* arg = args->children[i];

        if (arg == NULL ||
            arg->type != AST_ARGUMENT ||
            arg->child_count == 0 ||
            arg->children[0] == NULL ||
            arg->children[0]->type != AST_IDENTIFIER ||
            arg->children[0]->value == NULL)
            return 1;

        SymbolInfo* symbol = &function->symbols[i];

        symbol->kind = SYMBOL_PARAMETER;
        symbol->scope_id = function->scope_id;
        symbol->location = (SourceLocation){
            arg->children[0]->line,
            arg->children[0]->column
        };
        symbol->name = copy_string(arg->children[0]->value);

        if (symbol->name == NULL)
            return -1;

        function->symbol_count = i + 1;

        if (arg->child_count > 1) {
            int status = copy_type(
                arg->children[1],
                &symbol->declared_type
            );

            if (status != 0)
                return status;
        }

        function->signature.parameter_symbol_indices[i] = i;
        function->signature.parameter_count = i + 1;
    }

    if (signature->child_count > 2) {
        return copy_type(
            signature->children[2],
            &function->signature.return_type
        );
    }

    return 0;
}

static int add_scope(AnalysisResult* result,
    size_t parent_id,
    size_t function_id,
    size_t* output)
{
    if (result->scope_count >= SIZE_MAX / sizeof(*result->scopes))
        return no_memory(result);

    size_t count = result->scope_count + 1;

    ScopeInfo* scopes = realloc(
        result->scopes,
        count * sizeof(*scopes)
    );

    if (scopes == NULL)
        return no_memory(result);

    result->scopes = scopes;
    *output = result->scope_count;

    result->scopes[result->scope_count] = (ScopeInfo){
        .id = *output,
        .parent_id = parent_id,
        .function_id = function_id
    };

    result->scope_count = count;
    return 0;
}

static char* qualified_name(const char* prefix, const char* name)
{
    size_t a = strlen(prefix);
    size_t b = strlen(name);

    if (a > SIZE_MAX - b - 3)
        return NULL;

    char* text = malloc(a + b + 3);

    if (text == NULL)
        return NULL;

    memcpy(text, prefix, a);

    text[a] = ':';
    text[a + 1] = ':';

    memcpy(text + a + 2, name, b + 1);

    return text;
}

static int collect_functions(AnalysisResult* result,
    const AstNode* node,
    size_t file_index,
    size_t scope_id,
    size_t parent_function_id);

static int collect_function(AnalysisResult* result,
    const AstNode* node,
    size_t file_index,
    size_t parent_scope,
    size_t parent_function_id)
{
    if (node->child_count < 2 ||
        node->children[0] == NULL ||
        node->children[1] == NULL ||
        node->children[0]->type != AST_SIGNATURE ||
        node->children[0]->child_count == 0 ||
        node->children[0]->children[0] == NULL ||
        node->children[0]->children[0]->type != AST_IDENTIFIER ||
        node->children[0]->children[0]->value == NULL) {
        return add_error_at(
            result,
            file_index,
            node,
            "Invalid function definition"
        );
    }

    if (result->function_count >= SIZE_MAX / sizeof(*result->functions))
        return no_memory(result);

    const AstNode* signature = node->children[0];
    const char* name = signature->children[0]->value;

    FunctionInfo* function = calloc(1, sizeof(*function));

    if (function == NULL)
        return no_memory(result);

    function->id = result->function_count;
    function->file_index = file_index;
    function->location = (SourceLocation){
        node->line,
        node->column
    };
    function->parent_function_id = parent_function_id;
    function->name = copy_string(name);

    const char* prefix = parent_function_id == ANALYSIS_NO_ID
        ? result->files[file_index].path
        : result->functions[parent_function_id]->qualified_name;

    function->qualified_name = qualified_name(prefix, name);

    if (function->name == NULL ||
        function->qualified_name == NULL) {
        function_free(function);
        return no_memory(result);
    }

    if (add_scope(
        result,
        parent_scope,
        function->id,
        &function->scope_id
    ) != 0) {
        function_free(function);
        return -1;
    }

    int signature_status = copy_signature(signature, function);

    if (signature_status == -1) {
        function_free(function);
        return no_memory(result);
    }

    size_t count = result->function_count + 1;

    FunctionInfo** functions = realloc(
        result->functions,
        count * sizeof(*functions)
    );

    if (functions == NULL) {
        function_free(function);
        return no_memory(result);
    }

    result->functions = functions;
    result->functions[result->function_count] = function;
    result->function_count = count;

    if (signature_status == 1) {
        if (add_error_at(
            result,
            file_index,
            signature,
            "Invalid function signature"
        ) != 0)
            return -1;
    }

    return collect_functions(
        result,
        node->children[1],
        file_index,
        function->scope_id,
        function->id
    );
}

static int collect_functions(AnalysisResult* result,
    const AstNode* node,
    size_t file_index,
    size_t scope_id,
    size_t parent_function_id)
{
    if (node == NULL)
        return 0;

    if (node->type == AST_FUNCTION) {
        return collect_function(
            result,
            node,
            file_index,
            scope_id,
            parent_function_id
        );
    }

    if (node->type == AST_BLOCK) {
        if (add_scope(
            result,
            scope_id,
            parent_function_id,
            &scope_id
        ) != 0)
            return -1;

        result->scopes[scope_id].ast_node = node;
    }

    for (size_t i = 0; i < node->child_count; ++i) {
        if (collect_functions(
            result,
            node->children[i],
            file_index,
            scope_id,
            parent_function_id
        ) != 0)
            return -1;
    }

    return 0;
}

AnalysisResult analyze_program(const AnalysisSource* sources,
    size_t source_count)
{
    AnalysisResult result = { 0 };

    if (source_count == 0)
        return result;

    if (sources == NULL) {
        add_error(
            &result,
            ANALYSIS_NO_ID,
            "Source collection is NULL"
        );
        return result;
    }

    if (source_count > SIZE_MAX / sizeof(*result.files)) {
        no_memory(&result);
        return result;
    }

    result.files = calloc(
        source_count,
        sizeof(*result.files)
    );

    if (result.files == NULL) {
        no_memory(&result);
        return result;
    }

    for (size_t i = 0; i < source_count; ++i) {
        const char* path = sources[i].path;

        if (path == NULL)
            path = "<unknown>";

        result.files[i].path = copy_string(path);

        if (result.files[i].path == NULL) {
            no_memory(&result);
            break;
        }

        result.file_count = i + 1;
    }

    for (size_t i = 0;
        i < result.file_count &&
        result.status != ANALYSIS_OUT_OF_MEMORY;
        ++i) {

        if (sources[i].root == NULL ||
            sources[i].root->type != AST_SOURCE) {
            if (add_error(
                &result,
                i,
                "Missing or invalid AST root"
            ) != 0)
                break;

            continue;
        }

        size_t file_scope = ANALYSIS_NO_ID;

        if (add_scope(
            &result,
            ANALYSIS_NO_ID,
            ANALYSIS_NO_ID,
            &file_scope
        ) != 0)
            break;

        if (collect_functions(
            &result,
            sources[i].root,
            i,
            file_scope,
            ANALYSIS_NO_ID
        ) != 0)
            break;
    }

    if (result.status == ANALYSIS_OK &&
        cfg_build_all(&result, sources, source_count) != 0 &&
        result.status == ANALYSIS_OK) {
        add_error(
            &result,
            ANALYSIS_NO_ID,
            "CFG construction failed"
        );
    }

    for (size_t i = 0; i < result.scope_count; ++i)
        result.scopes[i].ast_node = NULL;

    return result;
}
