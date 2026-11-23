
#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <stddef.h>
#include "ast.h"

#define ANALYSIS_NO_ID ((size_t)-1)

typedef struct {
    size_t line;
    size_t column;
} SourceLocation;

typedef enum {
    TYPE_BUILTIN,
    TYPE_CUSTOM,
    TYPE_ARRAY
} TypeKind;

typedef struct TypeInfo {
    TypeKind kind;
    char* name;
    struct TypeInfo* element_type;
    size_t array_size;
} TypeInfo;

typedef enum {
    SYMBOL_PARAMETER,
    SYMBOL_LOCAL
} SymbolKind;

typedef struct {
    size_t id;
    size_t parent_id;
    size_t function_id;
    const AstNode* ast_node;
} ScopeInfo;

typedef struct {
    char* name;
    SymbolKind kind;
    TypeInfo* declared_type;
    size_t scope_id;
    SourceLocation location;
} SymbolInfo;

typedef struct {
    size_t function_id;
    size_t symbol_index;
} SymbolRef;

typedef enum {
    OP_IDENTIFIER,
    OP_BOOL_LITERAL,
    OP_STRING_LITERAL,
    OP_CHAR_LITERAL,
    OP_HEX_LITERAL,
    OP_BITS_LITERAL,
    OP_DEC_LITERAL,
    OP_UNARY,
    OP_BINARY,
    OP_CALL,
    OP_SLICE,
    OP_RANGE
} OpKind;

typedef struct OpNode {
    OpKind kind;
    char* value;
    struct OpNode** children;
    size_t child_count;
    SymbolRef symbol;
    size_t callee_function_id;
    SourceLocation location;
} OpNode;

typedef enum {
    CFG_TERM_NONE,
    CFG_TERM_JUMP,
    CFG_TERM_BRANCH,
    CFG_TERM_EXIT
} CfgTerminatorKind;

typedef struct CfgBlock CfgBlock;

typedef struct {
    CfgTerminatorKind kind;
    CfgBlock* jump_target;
    CfgBlock* true_target;
    CfgBlock* false_target;
} CfgTerminator;

struct CfgBlock {
    size_t id;
    OpNode** operations;
    size_t operation_count;
    OpNode* condition;
    CfgTerminator terminator;
    SourceLocation location;
};

typedef struct {
    CfgBlock** blocks;
    size_t block_count;
    CfgBlock* entry;
    CfgBlock* exit;
} CfgGraph;

typedef struct {
    size_t* parameter_symbol_indices;
    size_t parameter_count;
    TypeInfo* return_type;
} FunctionSignature;

typedef struct {
    size_t id;
    size_t file_index;
    size_t parent_function_id;
    size_t scope_id;
    char* name;
    char* qualified_name;
    SourceLocation location;
    FunctionSignature signature;
    SymbolInfo* symbols;
    size_t symbol_count;
    CfgGraph graph;
} FunctionInfo;

typedef struct {
    char* path;
} AnalyzedFile;

typedef struct {
    size_t caller_function_id;
    size_t callee_function_id;
    SourceLocation location;
} CallEdge;

typedef struct {
    size_t file_index;
    SourceLocation location;
    char* message;
} AnalysisError;

typedef enum {
    ANALYSIS_OK,
    ANALYSIS_HAS_ERRORS,
    ANALYSIS_OUT_OF_MEMORY
} AnalysisStatus;

typedef struct {
    AnalyzedFile* files;
    size_t file_count;

    FunctionInfo** functions;
    size_t function_count;

    ScopeInfo* scopes;
    size_t scope_count;

    CallEdge* calls;
    size_t call_count;

    AnalysisError* errors;
    size_t error_count;

    AnalysisStatus status;
} AnalysisResult;

typedef struct {
    const char* path;
    const AstNode* root;
} AnalysisSource;

AnalysisResult analyze_program(
    const AnalysisSource* sources,
    size_t source_count
);

void analysis_result_free(AnalysisResult* result);

#endif
