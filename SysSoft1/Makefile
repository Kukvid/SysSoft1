
CC = gcc

CFLAGS ?= -std=c11 -Wall -Wextra -g
CPPFLAGS = -Iinclude -Ibuild

PARSER_TARGET = build/spo-parser
CFG_TARGET = build/spo-cfg

GENERATED = build/grammar.tab.c build/grammar.tab.h build/lex.yy.c build/lex.yy.h

COMMON = src/ast.c src/parser.c build/grammar.tab.c build/lex.yy.c

.PHONY: all clean test cfg-test

all: $(PARSER_TARGET) $(CFG_TARGET)

build:
	mkdir -p build

build/grammar.tab.c build/grammar.tab.h &: grammar/grammar.y | build
	bison -d -o build/grammar.tab.c grammar/grammar.y

build/lex.yy.c build/lex.yy.h &: grammar/lexer.l build/grammar.tab.h | build
	flex -o build/lex.yy.c grammar/lexer.l

$(PARSER_TARGET): Main.c src/ast.c src/parser.c src/dgml.c include/ast.h include/parser.h include/dgml.h $(GENERATED)
	$(CC) $(CFLAGS) $(CPPFLAGS) Main.c src/ast.c src/parser.c src/dgml.c build/grammar.tab.c build/lex.yy.c -o $@

$(CFG_TARGET): CfgMain.c $(COMMON) src/analysis.c src/opstree.c src/cfg.c src/cfg_dgml.c include/analysis.h include/opstree.h include/cfg.h include/cfg_dgml.h $(GENERATED)
	$(CC) $(CFLAGS) $(CPPFLAGS) CfgMain.c $(COMMON) src/analysis.c src/opstree.c src/cfg.c src/cfg_dgml.c -o $@

test: $(PARSER_TARGET)
	./$(PARSER_TARGET) tests/full_test.txt build/full_test.dgml

cfg-test: $(CFG_TARGET)
	./$(CFG_TARGET) -o build/cfg-output tests/full_test.txt

clean:
	rm -rf build
