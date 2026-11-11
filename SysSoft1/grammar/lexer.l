
%option noyywrap noinput nounput never-interactive
%option reentrant bison-bridge bison-locations
%option header-file="build/lex.yy.h"

%{
#include <stdlib.h>
#include <string.h>
#include "grammar.tab.h"

static char* copy_text(const char* text)
{
    size_t length = strlen(text) + 1;
    char* result = malloc(length);

    if (result != NULL) {
        memcpy(result, text, length);
    }

    return result;
}

#define YY_USER_ACTION \
    do { \
        yylloc->first_line = yylloc->last_line; \
        yylloc->first_column = yylloc->last_column; \
        for (int i = 0; i < yyleng; ++i) { \
            if (yytext[i] == '\n') { \
                ++yylloc->last_line; \
                yylloc->last_column = 1; \
            } else { \
                ++yylloc->last_column; \
            } \
        } \
    } while (0);

%}

%%

"def"       { return DEF; }
"end"       { return END; }
"of"        { return OF; }
"array"     { return ARRAY; }

"if"        { return IF; }
"then"      { return THEN; }
"else"      { return ELSE; }

"while"     { return WHILE; }
"until"     { return UNTIL; }
"break"     { return BREAK; }
"begin"     { return BEGIN_KW; }

"bool"|"byte"|"int"|"uint"|"long"|"ulong"|"char"|"string" {
    yylval->text = copy_text(yytext);
    if (yylval->text == NULL) return YYerror;
    return BUILTIN_TYPE;
}


"true"|"false" {
    yylval->text = copy_text(yytext);
    if (yylval->text == NULL) return YYerror;
    return BOOL_LITERAL;
}

0[xX][0-9A-Fa-f]+ {
    yylval->text = copy_text(yytext);
    if (yylval->text == NULL) return YYerror;
    return HEX_LITERAL;
}

0[bB][01]+ {
    yylval->text = copy_text(yytext);
    if (yylval->text == NULL) return YYerror;
    return BITS_LITERAL;
}

[0-9]+ {
    yylval->text = copy_text(yytext);
    if (yylval->text == NULL) return YYerror;
    return DECIMAL;
}

\"[^\"\\]*(\\.[^\"\\]*)*\" {
    yylval->text = copy_text(yytext);
    if (yylval->text == NULL) return YYerror;
    return STRING_LITERAL;
}

\'[^\']\' {
    yylval->text = copy_text(yytext);
    if (yylval->text == NULL) return YYerror;
    return CHAR_LITERAL;
}


"=="        { return EQ; }
"!="        { return NE; }
"<="        { return LE; }
">="        { return GE; }

"&&"        { return AND; }
"||"        { return OR; }

"<<"        { return SHL; }
">>"        { return SHR; }

".."        { return DOTDOT; }

[a-zA-Z_][a-zA-Z_0-9]* {
    yylval->text = copy_text(yytext);
    if (yylval->text == NULL) return YYerror;
    return IDENTIFIER;
}

[ \t\r\n]+  { }

.           { return yytext[0]; }

%%
