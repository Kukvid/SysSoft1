
%code requires {
    #include "parser.h"
    typedef void* yyscan_t;
}

%code provides {
    int yylex(
        YYSTYPE* yylval_param,
        YYLTYPE* yylloc_param, 
        yyscan_t scanner
    );

    void yyerror(
        YYLTYPE* location,
        ParseResult* result,
        yyscan_t scanner,
        const char* message
    );
}

%{
#include <stdlib.h>
#include <stdio.h>
#include "parser.h"

int parser_add_error(ParseResult* result, const char* message);

static AstNode* make_node(
    AstNodeType type,
    const char* value,
    AstNode* first,
    AstNode* second,
    AstNode* third
);

static AstNode* make_named_node(
    AstNodeType type,
    const char* name,
    AstNode* first,
    AstNode* second
);

static int append_node(AstNode* parent, AstNode* child);

#define BINARY(op, left, right, output) \
    do { \
        (output) = make_node(AST_BINARY_OP, (op), (left), (right), NULL); \
        if ((output) == NULL) YYNOMEM; \
    } while (0)

#define UNARY(op, operand, output) \
    do { \
        (output) = make_node(AST_UNARY_OP, (op), (operand), NULL, NULL); \
        if ((output) == NULL) YYNOMEM; \
    } while (0)
%}

%glr-parser
%define api.pure true
%define parse.error detailed
%locations

%parse-param { ParseResult* result }
%parse-param { yyscan_t scanner }
%lex-param { yyscan_t scanner }

%union {
    char* text;
    AstNode* node;
}

%token DEF END OF ARRAY
%token IF THEN ELSE
%token BREAK BEGIN_KW
%token EQ NE LE GE AND OR SHL SHR DOTDOT

%token <text> IDENTIFIER BUILTIN_TYPE DECIMAL
%token <text> BOOL_LITERAL STRING_LITERAL CHAR_LITERAL
%token <text> HEX_LITERAL BITS_LITERAL

%type <node> source_items source_item
%type <node> function signature arguments argument
%type <node> optional_type type_ref
%type <node> statements statement
%type <node> block_items block_item
%type <node> expr unary_expr postfix_expr primary_expr
%type <node> call_arguments ranges range_list range

%destructor { free($$); } <text>
%destructor { ast_free($$); } <node>

%right '='
%left OR
%left AND
%left '|'
%left '^'
%left '&'
%left EQ NE
%left '<' '>' LE GE
%left SHL SHR
%left '+' '-'
%left '*' '/' '%'
%precedence UMINUS
%precedence '!' '~'
%precedence NO_ELSE
%precedence ELSE
%precedence WHILE UNTIL
%precedence POSTFIX

%%

program:
    source_items
    {
        result->root = $1;
    }
    ;

source_items:
    %empty
    {
        $$ = ast_create(AST_SOURCE, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | source_items source_item
    {
        if (append_node($1, $2) != 0) YYNOMEM;
        $$ = $1;
    }
    ;

source_item:
    function
    {
        $$ = $1;
    }
    ;

function:
    DEF signature statements END
    {
        $$ = make_node(AST_FUNCTION, NULL, $2, $3, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    ;

signature:
    IDENTIFIER '(' arguments ')' optional_type
    {
        $$ = make_named_node(AST_SIGNATURE, $1, $3, $5);
        free($1);
        if ($$ == NULL) YYNOMEM;
    }
    ;

arguments:
    %empty
    {
        $$ = ast_create(AST_ARGUMENTS, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | argument
    {
        $$ = make_node(AST_ARGUMENTS, NULL, $1, NULL, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | arguments ',' argument
    {
        if (append_node($1, $3) != 0) YYNOMEM;
        $$ = $1;
    }
    ;

argument:
    IDENTIFIER optional_type
    {
        $$ = make_named_node(AST_ARGUMENT, $1, $2, NULL);
        free($1);
        if ($$ == NULL) YYNOMEM;
    }
    ;

optional_type:
    %empty
    {
        $$ = NULL;
    }
    | OF type_ref
    {
        $$ = $2;
    }
    ;

type_ref:
    BUILTIN_TYPE
    {
        $$ = ast_create(AST_TYPE, $1);
        free($1);
        if ($$ == NULL) YYNOMEM;
    }
    | IDENTIFIER
    {
        $$ = ast_create(AST_TYPE, $1);
        free($1);
        if ($$ == NULL) YYNOMEM;
    }
    | type_ref ARRAY '[' DECIMAL ']'
    {
        AstNode* size = ast_create(AST_DEC_LITERAL, $4);
        free($4);

        if (size == NULL) YYNOMEM;

        $$ = make_node(AST_ARRAY_TYPE, NULL, $1, size, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    ;

statements:
    %empty
    {
        $$ = ast_create(AST_BODY, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | statements statement
    {
        if (append_node($1, $2) != 0) YYNOMEM;
        $$ = $1;
    }
    ;

statement:
    IF expr THEN statement %prec NO_ELSE
    {
        $$ = make_node(AST_IF, NULL, $2, $4, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | IF expr THEN statement ELSE statement %prec ELSE
    {
        $$ = make_node(AST_IF, NULL, $2, $4, $6);
        if ($$ == NULL) YYNOMEM;
    }
    | WHILE expr statements END %dprec 2
    {
        $$ = make_node(AST_WHILE, NULL, $2, $3, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | UNTIL expr statements END %dprec 2
    {
        $$ = make_node(AST_UNTIL, NULL, $2, $3, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | statement WHILE expr ';' %dprec 1
    {
        $$ = make_node(AST_REPEAT, "while", $1, $3, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | statement UNTIL expr ';' %dprec 1
    {
        $$ = make_node(AST_REPEAT, "until", $1, $3, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | BREAK ';'
    {
        $$ = ast_create(AST_BREAK, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | expr ';'
    {
        $$ = make_node(AST_EXPRESSION, NULL, $1, NULL, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | BEGIN_KW block_items END
    {
        $$ = $2;
    }
    | '{' block_items '}'
    {
        $$ = $2;
    }
    ;

block_items:
    %empty
    {
        $$ = ast_create(AST_BLOCK, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | block_items block_item
    {
        if (append_node($1, $2) != 0) YYNOMEM;
        $$ = $1;
    }
    ;

block_item:
    statement
    {
        $$ = $1;
    }
    | source_item
    {
        $$ = $1;
    }
    ;


expr:
    expr '=' expr
    {
        BINARY("=", $1, $3, $$);
    }
    | expr OR expr
    {
        BINARY("||", $1, $3, $$);
    }
    | expr AND expr
    {
        BINARY("&&", $1, $3, $$);
    }
    | expr '|' expr
    {
        BINARY("|", $1, $3, $$);
    }
    | expr '^' expr
    {
        BINARY("^", $1, $3, $$);
    }
    | expr '&' expr
    {
        BINARY("&", $1, $3, $$);
    }
    | expr EQ expr
    {
        BINARY("==", $1, $3, $$);
    }
    | expr NE expr
    {
        BINARY("!=", $1, $3, $$);
    }
    | expr '<' expr
    {
        BINARY("<", $1, $3, $$);
    }
    | expr '>' expr
    {
        BINARY(">", $1, $3, $$);
    }
    | expr LE expr
    {
        BINARY("<=", $1, $3, $$);
    }
    | expr GE expr
    {
        BINARY(">=", $1, $3, $$);
    }
    | expr SHL expr
    {
        BINARY("<<", $1, $3, $$);
    }
    | expr SHR expr
    {
        BINARY(">>", $1, $3, $$);
    }
    | expr '+' expr
    {
        BINARY("+", $1, $3, $$);
    }
    | expr '-' expr
    {
        BINARY("-", $1, $3, $$);
    }
    | expr '*' expr
    {
        BINARY("*", $1, $3, $$);
    }
    | expr '/' expr
    {
        BINARY("/", $1, $3, $$);
    }
    | expr '%' expr
    {
        BINARY("%", $1, $3, $$);
    }
    | unary_expr
    {
        $$ = $1;
    }
    ;

unary_expr:
    '-' unary_expr %prec UMINUS
    {
        UNARY("-", $2, $$);
    }
    | '!' unary_expr
    {
        UNARY("!", $2, $$);
    }
    | '~' unary_expr
    {
        UNARY("~", $2, $$);
    }
    | postfix_expr
    {
        $$ = $1;
    }
    ;

postfix_expr:
    primary_expr
    {
        $$ = $1;
    }
    | postfix_expr '(' call_arguments ')'
    {
        $$ = make_node(AST_CALL, NULL, $1, $3, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | postfix_expr '[' ranges ']'
    {
        $$ = make_node(AST_SLICE, NULL, $1, $3, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    ;

primary_expr:
    '(' expr ')'
    {
        $$ = $2;
    }
    | IDENTIFIER
    {
        $$ = ast_create(AST_IDENTIFIER, $1);
        free($1);
        if ($$ == NULL) YYNOMEM;
    }
    
    | BOOL_LITERAL
    {
        $$ = ast_create(AST_BOOL_LITERAL, $1);
        free($1);
        if ($$ == NULL) YYNOMEM;
    }
    | STRING_LITERAL
    {
        $$ = ast_create(AST_STRING_LITERAL, $1);
        free($1);
        if ($$ == NULL) YYNOMEM;
    }
    | CHAR_LITERAL
    {
        $$ = ast_create(AST_CHAR_LITERAL, $1);
        free($1);
        if ($$ == NULL) YYNOMEM;
    }
    | HEX_LITERAL
    {
        $$ = ast_create(AST_HEX_LITERAL, $1);
        free($1);
        if ($$ == NULL) YYNOMEM;
    }
    | BITS_LITERAL
    {
        $$ = ast_create(AST_BITS_LITERAL, $1);
        free($1);
        if ($$ == NULL) YYNOMEM;
    }
    | DECIMAL
    {
        $$ = ast_create(AST_DEC_LITERAL, $1);
        free($1);
        if ($$ == NULL) YYNOMEM;
    }
    ;


call_arguments:
    %empty
    {
        $$ = ast_create(AST_CALL_ARGUMENTS, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | expr
    {
        $$ = make_node(AST_CALL_ARGUMENTS, NULL, $1, NULL, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | call_arguments ',' expr
    {
        if (append_node($1, $3) != 0) YYNOMEM;
        $$ = $1;
    }
    ;



ranges:
    %empty
    {
        $$ = ast_create(AST_RANGES, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | range_list
    {
        $$ = $1;
    }
    ;

range_list:
    range
    {
        $$ = make_node(AST_RANGES, NULL, $1, NULL, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | range_list ',' range
    {
        if (append_node($1, $3) != 0) YYNOMEM;
        $$ = $1;
    }
    ;



range:
    expr
    {
        $$ = make_node(AST_RANGE, NULL, $1, NULL, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    | expr DOTDOT expr
    {
        $$ = make_node(AST_RANGE, "..", $1, $3, NULL);
        if ($$ == NULL) YYNOMEM;
    }
    ;

%%

static int append_node(AstNode* parent, AstNode* child)
{
    if (parent == NULL || child == NULL) {
        return -1;
    }

    return add_ast_child(parent, child);
}


static AstNode* make_node(
    AstNodeType type,
    const char* value,
    AstNode* first,
    AstNode* second,
    AstNode* third
)
{
    AstNode* node = ast_create(type, value);

    if (node == NULL) {
        return NULL;
    }

    if ((first != NULL && add_ast_child(node, first) != 0) ||
        (second != NULL && add_ast_child(node, second) != 0) ||
        (third != NULL && add_ast_child(node, third) != 0)) {

        node->child_count = 0;
        ast_free(node);
        return NULL;
    }

    return node;
}



static AstNode* make_named_node(
    AstNodeType type,
    const char* name,
    AstNode* first,
    AstNode* second
)
{
    AstNode* identifier = ast_create(AST_IDENTIFIER, name);

    if (identifier == NULL) {
        return NULL;
    }

    AstNode* node = make_node(
        type,
        NULL,
        identifier,
        first,
        second
    );

    if (node == NULL) {
        ast_free(identifier);
        return NULL;
    }

    return node;
}



void yyerror(
    YYLTYPE* location,
    ParseResult* result,
    yyscan_t scanner,
    const char* message
)
{
    (void)scanner;

    char buffer[BUFSIZ];

    snprintf(
        buffer,
        sizeof(buffer),
        "line %d, column %d: %s",
        location->first_line,
        location->first_column,
        message
    );

    parser_add_error(result, buffer);
}