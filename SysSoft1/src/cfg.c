
#include "cfg.h"
#include "opstree.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    AnalysisResult* result;
    FunctionInfo* function;
    size_t file_index;
    SourceLocation location;
    size_t scope_id;
    size_t next_scope;
} Builder;

static int report_error(Builder* b, const char* message)
{
    AnalysisResult* result = b->result;

    if (result->error_count >= SIZE_MAX / sizeof(*result->errors)) {
        result->status = ANALYSIS_OUT_OF_MEMORY;
        return -1;
    }

    size_t length = strlen(message) + 1;
    char* copy = malloc(length);

    if (copy == NULL) {
        result->status = ANALYSIS_OUT_OF_MEMORY;
        return -1;
    }

    memcpy(copy, message, length);

    size_t count = result->error_count + 1;

    AnalysisError* errors = realloc(
        result->errors,
        count * sizeof(*errors)
    );

    if (errors == NULL) {
        free(copy);
        result->status = ANALYSIS_OUT_OF_MEMORY;
        return -1;
    }

    result->errors = errors;

    result->errors[result->error_count] = (AnalysisError){
        .file_index = b->file_index,
        .location = b->location,
        .message = copy
    };

    result->error_count = count;

    if (result->status == ANALYSIS_OK)
        result->status = ANALYSIS_HAS_ERRORS;

    return 0;
}

static int memory_error(Builder* b)
{
    b->result->status = ANALYSIS_OUT_OF_MEMORY;
    return -1;
}

static CfgBlock* new_block(Builder* b)
{
    CfgGraph* graph = &b->function->graph;

    if (graph->block_count >= SIZE_MAX / sizeof(*graph->blocks)) {
        memory_error(b);
        return NULL;
    }

    CfgBlock* block = calloc(1, sizeof(*block));

    if (block == NULL) {
        memory_error(b);
        return NULL;
    }

    CfgBlock** list = realloc(
        graph->blocks,
        (graph->block_count + 1) * sizeof(*list)
    );

    if (list == NULL) {
        free(block);
        memory_error(b);
        return NULL;
    }

    graph->blocks = list;

    block->id = graph->block_count;
    graph->blocks[graph->block_count++] = block;

    return block;
}

static void jump_to(CfgBlock* from, CfgBlock* target)
{
    from->terminator.kind = CFG_TERM_JUMP;
    from->terminator.jump_target = target;
}

static SymbolRef find_symbol(Builder* b, const char* name)
{
    AnalysisResult* result = b->result;
    size_t scope_id = b->scope_id;

    while (scope_id != ANALYSIS_NO_ID) {
        if (scope_id >= result->scope_count)
            break;

        const ScopeInfo* scope = &result->scopes[scope_id];
        size_t function_id = scope->function_id;

        if (function_id != ANALYSIS_NO_ID &&
            function_id < result->function_count) {

            FunctionInfo* fn = result->functions[function_id];

            for (size_t i = fn->symbol_count; i > 0; --i) {
                const SymbolInfo* symbol = &fn->symbols[i - 1];

                if (symbol->scope_id == scope_id &&
                    strcmp(symbol->name, name) == 0) {
                    return (SymbolRef) {
                        function_id,
                            i - 1
                    };
                }
            }
        }

        scope_id = scope->parent_id;
    }

    return (SymbolRef) {
        ANALYSIS_NO_ID,
            ANALYSIS_NO_ID
    };
}

static int bind_identifier(Builder* b, OpNode* node)
{
    b->location = node->location;

    if (node->value == NULL)
        return report_error(b, "Identifier without name");

    SymbolRef ref = find_symbol(b, node->value);

    if (ref.function_id != ANALYSIS_NO_ID) {
        node->symbol = ref;
        return 0;
    }

    FunctionInfo* fn = b->function;

    if (fn->symbol_count >= SIZE_MAX / sizeof(*fn->symbols))
        return memory_error(b);

    size_t count = fn->symbol_count + 1;

    SymbolInfo* symbols = realloc(
        fn->symbols,
        count * sizeof(*symbols)
    );

    if (symbols == NULL)
        return memory_error(b);

    fn->symbols = symbols;

    SymbolInfo* symbol = &fn->symbols[fn->symbol_count];
    *symbol = (SymbolInfo){ 0 };

    size_t length = strlen(node->value) + 1;
    symbol->name = malloc(length);

    if (symbol->name == NULL)
        return memory_error(b);

    memcpy(symbol->name, node->value, length);

    symbol->kind = SYMBOL_LOCAL;
    symbol->scope_id = b->scope_id;

    node->symbol = (SymbolRef){
        fn->id,
        fn->symbol_count
    };

    fn->symbol_count = count;
    return 0;
}

static size_t resolve_callee(
    Builder* b,
    const char* name,
    SymbolRef* variable,
    int* ambiguous
)
{
    AnalysisResult* result = b->result;

    *variable = (SymbolRef){
        ANALYSIS_NO_ID,
        ANALYSIS_NO_ID
    };
    *ambiguous = 0;

    for (size_t scope_id = b->scope_id;
        scope_id != ANALYSIS_NO_ID;
        scope_id = result->scopes[scope_id].parent_id) {

        const ScopeInfo* scope = &result->scopes[scope_id];

        if (scope->function_id != ANALYSIS_NO_ID) {
            FunctionInfo* owner =
                result->functions[scope->function_id];

            for (size_t i = owner->symbol_count; i > 0; --i) {
                SymbolInfo* symbol = &owner->symbols[i - 1];

                if (symbol->scope_id == scope_id &&
                    strcmp(symbol->name, name) == 0) {
                    *variable = (SymbolRef){
                        owner->id,
                        i - 1
                    };

                    return ANALYSIS_NO_ID;
                }
            }
        }

        size_t found = ANALYSIS_NO_ID;

        for (size_t i = 0; i < result->function_count; ++i) {
            FunctionInfo* fn = result->functions[i];

            size_t declaring_scope =
                result->scopes[fn->scope_id].parent_id;

            if (declaring_scope == scope_id &&
                strcmp(fn->name, name) == 0) {

                if (found != ANALYSIS_NO_ID) {
                    *ambiguous = 1;
                    return ANALYSIS_NO_ID;
                }

                found = fn->id;
            }
        }

        if (found != ANALYSIS_NO_ID)
            return found;
    }

    size_t found = ANALYSIS_NO_ID;

    for (size_t i = 0; i < result->function_count; ++i) {
        FunctionInfo* fn = result->functions[i];

        if (fn->file_index != b->file_index &&
            fn->parent_function_id == ANALYSIS_NO_ID &&
            strcmp(fn->name, name) == 0) {

            if (found != ANALYSIS_NO_ID) {
                *ambiguous = 1;
                return ANALYSIS_NO_ID;
            }

            found = fn->id;
        }
    }

    return found;
}

static int append_call(
    Builder* b,
    size_t callee_id,
    SourceLocation location
)
{
    AnalysisResult* result = b->result;

    if (result->call_count >= SIZE_MAX / sizeof(*result->calls))
        return memory_error(b);

    size_t count = result->call_count + 1;

    CallEdge* calls = realloc(
        result->calls,
        count * sizeof(*calls)
    );

    if (calls == NULL)
        return memory_error(b);

    result->calls = calls;

    result->calls[result->call_count] = (CallEdge){
        .caller_function_id = b->function->id,
        .callee_function_id = callee_id,
        .location = location
    };

    result->call_count = count;
    return 0;
}


static int bind_call_target(Builder* b, OpNode* call)
{
    OpNode* target = call->children[0];

    if (target->kind != OP_IDENTIFIER ||
        target->value == NULL)
        return 0;

    SymbolRef variable;
    int ambiguous = 0;

    size_t id = resolve_callee(
        b,
        target->value,
        &variable,
        &ambiguous
    );

    if (variable.function_id != ANALYSIS_NO_ID) {
        target->symbol = variable;
        return 0;
    }

    if (ambiguous) {
        b->location = target->location;
        return report_error(b, "Ambiguous function call");
    }

    if (id == ANALYSIS_NO_ID)
        return 0;

    call->callee_function_id = id;

    return append_call(
        b,
        id,
        target->location
    );
}


static int bind_tree(Builder* b, OpNode* node)
{
    if (node == NULL)
        return 0;

    if (node->kind == OP_IDENTIFIER)
        return bind_identifier(b, node);

    if (node->kind == OP_CALL && node->child_count > 0) {
        OpNode* callee = node->children[0];

        if (callee->kind == OP_IDENTIFIER) {
            if (bind_call_target(b, node) != 0)
                return -1;
        }
        else if (bind_tree(b, callee) != 0) {
            return -1;
        }

        for (size_t i = 1; i < node->child_count; ++i) {
            if (bind_tree(b, node->children[i]) != 0)
                return -1;
        }

        return 0;
    }

    for (size_t i = 0; i < node->child_count; ++i) {
        if (bind_tree(b, node->children[i]) != 0)
            return -1;
    }

    return 0;
}

static OpNode* convert(Builder* b, const AstNode* ast)
{
    b->location = (SourceLocation){
        ast != NULL ? ast->line : 0,
        ast != NULL ? ast->column : 0
    };

    OpsTreeStatus status = OPSTREE_OK;

    OpNode* op = opstree_from_ast(ast, &status);

    if (op == NULL) {
        if (status == OPSTREE_OUT_OF_MEMORY)
            memory_error(b);
        else
            report_error(b, "Invalid expression AST");

        return NULL;
    }

    if (bind_tree(b, op) != 0) {
        opstree_free(op);
        return NULL;
    }

    return op;
}

static int append_operation(
    Builder* b,
    CfgBlock* block,
    OpNode* operation
)
{
    if (block->operation_count >= SIZE_MAX / sizeof(*block->operations))
        return memory_error(b);

    OpNode** list = realloc(
        block->operations,
        (block->operation_count + 1) * sizeof(*list)
    );

    if (list == NULL)
        return memory_error(b);

    block->operations = list;
    block->operations[block->operation_count++] = operation;

    return 0;
}

static int contains_short_circuit(const AstNode* ast)
{
    if (ast == NULL)
        return 0;

    if (ast->child_count != 0 && ast->children == NULL)
        return 0;

    if (ast->type == AST_BINARY_OP &&
        ast->value != NULL &&
        (strcmp(ast->value, "&&") == 0 ||
            strcmp(ast->value, "||") == 0))
        return 1;

    for (size_t i = 0; i < ast->child_count; ++i) {
        if (contains_short_circuit(ast->children[i]))
            return 1;
    }

    return 0;
}

static OpNode* new_operation_node(
    Builder* b,
    OpKind kind,
    const char* value,
    size_t children
)
{
    OpNode* op = calloc(1, sizeof(*op));

    if (op == NULL) {
        memory_error(b);
        return NULL;
    }

    op->kind = kind;
    op->symbol = (SymbolRef){
        ANALYSIS_NO_ID,
        ANALYSIS_NO_ID
    };
    op->callee_function_id = ANALYSIS_NO_ID;
    op->location = b->location;

    if (value != NULL) {
        size_t size = strlen(value) + 1;
        op->value = malloc(size);

        if (op->value == NULL) {
            opstree_free(op);
            memory_error(b);
            return NULL;
        }

        memcpy(op->value, value, size);
    }

    if (children != 0) {
        if (children > SIZE_MAX / sizeof(*op->children)) {
            opstree_free(op);
            memory_error(b);
            return NULL;
        }

        op->children = calloc(
            children,
            sizeof(*op->children)
        );

        if (op->children == NULL) {
            opstree_free(op);
            memory_error(b);
            return NULL;
        }
    }

    return op;
}

static int create_boolean_temp(
    Builder* b,
    char name[64],
    SymbolRef* ref
)
{
    FunctionInfo* fn = b->function;

    if (fn->symbol_count >= SIZE_MAX / sizeof(*fn->symbols))
        return memory_error(b);

    size_t suffix = fn->symbol_count;

    for (;;) {
        snprintf(name, 64, "@spo_bool_%zu", suffix);

        int occupied = 0;

        for (size_t i = 0; i < fn->symbol_count; ++i) {
            if (strcmp(fn->symbols[i].name, name) == 0) {
                occupied = 1;
                break;
            }
        }

        if (!occupied)
            break;

        if (suffix == SIZE_MAX)
            return memory_error(b);

        ++suffix;
    }

    SymbolInfo* symbols = realloc(
        fn->symbols,
        (fn->symbol_count + 1) * sizeof(*symbols)
    );

    if (symbols == NULL)
        return memory_error(b);

    fn->symbols = symbols;

    SymbolInfo* symbol = &fn->symbols[fn->symbol_count];
    *symbol = (SymbolInfo){ 0 };

    symbol->name = malloc(strlen(name) + 1);

    if (symbol->name == NULL)
        return memory_error(b);

    strcpy(symbol->name, name);

    symbol->kind = SYMBOL_LOCAL;
    symbol->scope_id = b->scope_id;
    symbol->location = b->location;

    *ref = (SymbolRef){
        fn->id,
        fn->symbol_count
    };

    ++fn->symbol_count;
    return 0;
}

static OpNode* make_temp_identifier(
    Builder* b,
    const char* name,
    SymbolRef ref
)
{
    OpNode* op = new_operation_node(
        b,
        OP_IDENTIFIER,
        name,
        0
    );

    if (op != NULL)
        op->symbol = ref;

    return op;
}

static int write_boolean_temp(
    Builder* b,
    CfgBlock* block,
    const char* name,
    SymbolRef ref,
    int value
)
{
    OpNode* assignment = new_operation_node(
        b,
        OP_BINARY,
        "=",
        2
    );

    if (assignment == NULL)
        return -1;

    OpNode* id = make_temp_identifier(b, name, ref);

    if (id == NULL) {
        opstree_free(assignment);
        return -1;
    }

    assignment->children[assignment->child_count++] = id;

    OpNode* literal = new_operation_node(
        b,
        OP_BOOL_LITERAL,
        value ? "true" : "false",
        0
    );

    if (literal == NULL) {
        opstree_free(assignment);
        return -1;
    }

    assignment->children[assignment->child_count++] = literal;

    if (append_operation(b, block, assignment) != 0) {
        opstree_free(assignment);
        return -1;
    }

    return 0;
}

static int build_condition(
    Builder* b,
    const AstNode* ast,
    CfgBlock* start,
    CfgBlock* on_true,
    CfgBlock* on_false
);

static OpNode* lower_expression(
    Builder* b,
    const AstNode* ast,
    CfgBlock** current
)
{
    if (ast == NULL ||
        current == NULL ||
        *current == NULL) {
        report_error(b, "Invalid expression AST");
        return NULL;
    }

    if (ast->type == AST_EXPRESSION) {
        if (ast->child_count != 1 ||
            ast->children == NULL) {
            report_error(b, "Invalid expression AST");
            return NULL;
        }

        return lower_expression(
            b,
            ast->children[0],
            current
        );
    }

    if (!contains_short_circuit(ast))
        return convert(b, ast);

    if (ast->type == AST_BINARY_OP &&
        ast->value != NULL &&
        (strcmp(ast->value, "&&") == 0 ||
            strcmp(ast->value, "||") == 0)) {

        char temp_name[64];
        SymbolRef temp_ref;

        b->location = (SourceLocation){
            ast->line,
            ast->column
        };

        if (create_boolean_temp(
            b,
            temp_name,
            &temp_ref
        ) != 0)
            return NULL;

        CfgBlock* when_true = new_block(b);
        CfgBlock* when_false = new_block(b);
        CfgBlock* after = new_block(b);

        if (when_true == NULL ||
            when_false == NULL ||
            after == NULL)
            return NULL;

        if (build_condition(
            b,
            ast,
            *current,
            when_true,
            when_false
        ) != 0)
            return NULL;

        if (write_boolean_temp(
            b,
            when_true,
            temp_name,
            temp_ref,
            1
        ) != 0 ||
            write_boolean_temp(
                b,
                when_false,
                temp_name,
                temp_ref,
                0
            ) != 0)
            return NULL;

        jump_to(when_true, after);
        jump_to(when_false, after);

        *current = after;

        return make_temp_identifier(
            b,
            temp_name,
            temp_ref
        );
    }

    OpKind kind;
    const AstNode* list = NULL;
    size_t child_count = 0;

    switch (ast->type) {
    case AST_BINARY_OP:
        kind = OP_BINARY;
        child_count = 2;
        break;

    case AST_UNARY_OP:
        kind = OP_UNARY;
        child_count = 1;
        break;

    case AST_CALL:
        kind = OP_CALL;

        if (ast->child_count == 2 &&
            ast->children != NULL)
            list = ast->children[1];

        if (list == NULL ||
            list->type != AST_CALL_ARGUMENTS ||
            list->child_count == SIZE_MAX) {
            report_error(b, "Invalid call AST");
            return NULL;
        }

        child_count = list->child_count + 1;
        break;

    case AST_SLICE:
        kind = OP_SLICE;

        if (ast->child_count == 2 &&
            ast->children != NULL)
            list = ast->children[1];

        if (list == NULL ||
            list->type != AST_RANGES ||
            list->child_count == SIZE_MAX) {
            report_error(b, "Invalid slice AST");
            return NULL;
        }

        child_count = list->child_count + 1;
        break;

    case AST_RANGE:
        kind = OP_RANGE;
        child_count = ast->child_count;
        break;

    default:
        report_error(
            b,
            "Unsupported expression with short circuit"
        );
        return NULL;
    }

    if (ast->children == NULL ||
        (list != NULL &&
            list->child_count != 0 &&
            list->children == NULL) ||
        (list == NULL &&
            ast->child_count != child_count)) {
        report_error(b, "Invalid expression operands");
        return NULL;
    }

    OpNode* root = new_operation_node(
        b,
        kind,
        ast->value,
        child_count
    );

    if (root == NULL)
        return NULL;

    root->location = (SourceLocation){
        ast->line,
        ast->column
    };

    for (size_t i = 0; i < child_count; ++i) {
        const AstNode* child = list != NULL && i > 0
            ? list->children[i - 1]
            : ast->children[i];

        OpNode* op;

        if (kind == OP_CALL &&
            i == 0 &&
            child != NULL &&
            child->type == AST_IDENTIFIER) {
            op = new_operation_node(
                b,
                OP_IDENTIFIER,
                child->value,
                0
            );

            if (op != NULL) {
                op->location = (SourceLocation){
                    child->line,
                    child->column
                };
            }
        }
        else {
            op = lower_expression(
                b,
                child,
                current
            );
        }

        if (op == NULL) {
            opstree_free(root);
            return NULL;
        }

        root->children[root->child_count++] = op;
    }

    if (kind == OP_CALL &&
        root->child_count > 0 &&
        root->children[0]->kind == OP_IDENTIFIER &&
        bind_call_target(b, root) != 0) {
        opstree_free(root);
        return NULL;
    }

    return root;
}

static int build_condition(
    Builder* b,
    const AstNode* ast,
    CfgBlock* start,
    CfgBlock* on_true,
    CfgBlock* on_false
)
{
    if (ast == NULL)
        return report_error(b, "Missing condition");

    if (ast->type == AST_EXPRESSION) {
        if (ast->child_count != 1 ||
            ast->children == NULL)
            return report_error(b, "Invalid condition AST");

        return build_condition(
            b,
            ast->children[0],
            start,
            on_true,
            on_false
        );
    }

    if (ast->type == AST_BINARY_OP &&
        ast->value != NULL &&
        ast->children != NULL &&
        ast->child_count == 2 &&
        (strcmp(ast->value, "&&") == 0 ||
            strcmp(ast->value, "||") == 0)) {

        CfgBlock* right = new_block(b);

        if (right == NULL)
            return -1;

        if (strcmp(ast->value, "&&") == 0) {
            if (build_condition(
                b,
                ast->children[0],
                start,
                right,
                on_false
            ) != 0)
                return -1;
        }
        else {
            if (build_condition(
                b,
                ast->children[0],
                start,
                on_true,
                right
            ) != 0)
                return -1;
        }

        return build_condition(
            b,
            ast->children[1],
            right,
            on_true,
            on_false
        );
    }

    if (ast->type == AST_UNARY_OP &&
        ast->value != NULL &&
        strcmp(ast->value, "!") == 0 &&
        ast->child_count == 1 &&
        ast->children != NULL) {

        return build_condition(
            b,
            ast->children[0],
            start,
            on_false,
            on_true
        );
    }

    if (start->condition != NULL ||
        start->terminator.kind != CFG_TERM_NONE)
        return report_error(
            b,
            "Condition block already terminated"
        );

    CfgBlock* evaluation_end = start;

    OpNode* condition = lower_expression(
        b,
        ast,
        &evaluation_end
    );

    if (condition == NULL)
        return -1;

    evaluation_end->condition = condition;

    evaluation_end->terminator.kind = CFG_TERM_BRANCH;
    evaluation_end->terminator.true_target = on_true;
    evaluation_end->terminator.false_target = on_false;

    return 0;
}

static int build_statement(
    Builder* b,
    const AstNode* statement,
    CfgBlock** current,
    CfgBlock* break_target
)
{
    if (statement == NULL)
        return report_error(b, "Missing statement");

    b->location = (SourceLocation){
        statement->line,
        statement->column
    };

    switch (statement->type) {
    case AST_BODY:
    case AST_BLOCK: {
        size_t previous_scope = b->scope_id;

        if (statement->type == AST_BLOCK) {
            size_t selected = ANALYSIS_NO_ID;

            for (size_t i = 0;
                i < b->result->scope_count;
                ++i) {

                const ScopeInfo* scope =
                    &b->result->scopes[i];

                if (scope->ast_node == statement &&
                    scope->parent_id == previous_scope &&
                    scope->function_id ==
                    b->function->id) {

                    selected = i;
                    break;
                }
            }

            if (selected == ANALYSIS_NO_ID)
                return report_error(
                    b,
                    "Block scope not found"
                );

            b->scope_id = selected;
        }

        for (size_t i = 0;
            i < statement->child_count;
            ++i) {

            const AstNode* child =
                statement->children[i];

            if (child != NULL &&
                child->type == AST_FUNCTION)
                continue;

            if (*current == NULL)
                continue;

            if (build_statement(
                b,
                child,
                current,
                break_target
            ) != 0) {

                b->scope_id = previous_scope;
                return -1;
            }
        }

        b->scope_id = previous_scope;
        return 0;
    }

    case AST_FUNCTION:
        return 0;

    case AST_EXPRESSION: {
        if (*current == NULL)
            return 0;

        OpNode* op = lower_expression(
            b,
            statement,
            current
        );

        if (op == NULL)
            return -1;

        if (append_operation(
            b,
            *current,
            op
        ) != 0) {
            opstree_free(op);
            return -1;
        }

        return 0;
    }

    case AST_BREAK:
        if (*current == NULL)
            return 0;

        if (break_target == NULL) {
            if (report_error(
                b,
                "break outside loop"
            ) != 0)
                return -1;
        }
        else {
            jump_to(*current, break_target);
        }

        *current = NULL;
        return 0;

    case AST_IF: {
        if (*current == NULL)
            return 0;

        if (statement->child_count < 2 ||
            statement->child_count > 3)
            return report_error(
                b,
                "Invalid if statement"
            );

        CfgBlock* test = *current;
        CfgBlock* then_block = new_block(b);
        CfgBlock* after = new_block(b);

        if (then_block == NULL ||
            after == NULL)
            return -1;

        CfgBlock* else_block = NULL;

        if (statement->child_count == 3) {
            else_block = new_block(b);

            if (else_block == NULL)
                return -1;
        }

        if (build_condition(
            b,
            statement->children[0],
            test,
            then_block,
            else_block != NULL
            ? else_block
            : after
        ) != 0)
            return -1;

        CfgBlock* then_tail = then_block;

        if (build_statement(
            b,
            statement->children[1],
            &then_tail,
            break_target
        ) != 0)
            return -1;

        if (then_tail != NULL)
            jump_to(then_tail, after);

        if (else_block != NULL) {
            CfgBlock* else_tail = else_block;

            if (build_statement(
                b,
                statement->children[2],
                &else_tail,
                break_target
            ) != 0)
                return -1;

            if (else_tail != NULL)
                jump_to(else_tail, after);

            if (then_tail == NULL &&
                else_tail == NULL) {
                *current = NULL;
                return 0;
            }
        }

        *current = after;
        return 0;
    }

    case AST_WHILE:
    case AST_UNTIL: {
        if (*current == NULL)
            return 0;

        if (statement->child_count != 2)
            return report_error(
                b,
                "Invalid loop statement"
            );

        CfgBlock* condition_block = new_block(b);
        CfgBlock* body_block = new_block(b);
        CfgBlock* after = new_block(b);

        if (condition_block == NULL ||
            body_block == NULL ||
            after == NULL)
            return -1;

        jump_to(*current, condition_block);

        CfgBlock* on_true =
            statement->type == AST_WHILE
            ? body_block
            : after;

        CfgBlock* on_false =
            statement->type == AST_WHILE
            ? after
            : body_block;

        if (build_condition(
            b,
            statement->children[0],
            condition_block,
            on_true,
            on_false
        ) != 0)
            return -1;

        CfgBlock* body_tail = body_block;

        if (build_statement(
            b,
            statement->children[1],
            &body_tail,
            after
        ) != 0)
            return -1;

        if (body_tail != NULL)
            jump_to(body_tail, condition_block);

        *current = after;
        return 0;
    }

    case AST_REPEAT: {
        if (*current == NULL)
            return 0;

        if (statement->child_count != 2 ||
            statement->value == NULL ||
            (strcmp(statement->value, "while") != 0 &&
                strcmp(statement->value, "until") != 0))
            return report_error(
                b,
                "Invalid post-condition loop"
            );

        CfgBlock* body_block = new_block(b);
        CfgBlock* condition_block = new_block(b);
        CfgBlock* after = new_block(b);

        if (body_block == NULL ||
            condition_block == NULL ||
            after == NULL)
            return -1;

        jump_to(*current, body_block);

        CfgBlock* body_tail = body_block;

        if (build_statement(
            b,
            statement->children[0],
            &body_tail,
            after
        ) != 0)
            return -1;

        if (body_tail != NULL)
            jump_to(body_tail, condition_block);

        int is_while =
            strcmp(statement->value, "while") == 0;

        CfgBlock* on_true =
            is_while ? body_block : after;

        CfgBlock* on_false =
            is_while ? after : body_block;

        if (build_condition(
            b,
            statement->children[1],
            condition_block,
            on_true,
            on_false
        ) != 0)
            return -1;

        *current = after;
        return 0;
    }

    default:
        return report_error(
            b,
            "Unsupported AST statement"
        );
    }
}

static int build_function(
    AnalysisResult* result,
    FunctionInfo* fn,
    const AstNode* function_ast
)
{
    Builder b = {
        .result = result,
        .function = fn,
        .file_index = fn->file_index,
        .location = {
            function_ast->line,
            function_ast->column
        }
    };

    if (function_ast->child_count < 2 ||
        function_ast->children == NULL)
        return report_error(
            &b,
            "Invalid AST_FUNCTION"
        );

    b.scope_id = fn->scope_id;

    CfgBlock* entry = new_block(&b);
    CfgBlock* exit_block = new_block(&b);

    if (entry == NULL ||
        exit_block == NULL)
        return -1;

    fn->graph.entry = entry;
    fn->graph.exit = exit_block;

    exit_block->terminator.kind = CFG_TERM_EXIT;

    CfgBlock* current = entry;

    if (build_statement(
        &b,
        function_ast->children[1],
        &current,
        NULL
    ) != 0)
        return -1;

    if (current != NULL)
        jump_to(current, exit_block);

    return 0;
}

static int build_functions_in_ast(
    AnalysisResult* result,
    const AstNode* ast,
    size_t file_index,
    size_t* next_function
)
{
    if (ast == NULL)
        return 0;

    if (ast->type == AST_FUNCTION) {
        if (*next_function >= result->function_count) {
            result->status = ANALYSIS_HAS_ERRORS;
            return -1;
        }

        FunctionInfo* fn =
            result->functions[(*next_function)++];

        if (fn->file_index != file_index)
            return -1;

        if (build_function(
            result,
            fn,
            ast
        ) != 0)
            return -1;

        return build_functions_in_ast(
            result,
            ast->children[1],
            file_index,
            next_function
        );
    }

    for (size_t i = 0; i < ast->child_count; ++i) {
        if (build_functions_in_ast(
            result,
            ast->children[i],
            file_index,
            next_function
        ) != 0)
            return -1;
    }

    return 0;
}

int cfg_build_all(
    AnalysisResult* result,
    const AnalysisSource* sources,
    size_t source_count
)
{
    if (result == NULL ||
        (source_count != 0 && sources == NULL))
        return -1;

    if (result->status == ANALYSIS_OUT_OF_MEMORY)
        return -1;

    if (source_count != result->file_count)
        return -1;

    size_t next_function = 0;

    for (size_t i = 0; i < source_count; ++i) {
        if (sources[i].root == NULL)
            continue;

        if (build_functions_in_ast(
            result,
            sources[i].root,
            i,
            &next_function
        ) != 0)
            return -1;
    }

    return next_function == result->function_count
        ? 0
        : -1;
}
