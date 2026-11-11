CC = gcc
BISON = bison
FLEX = flex

CFLAGS = -std=c11 -Wall -Wextra -g -Iinclude -Ibuild

TARGET = build/spo-parser

.PHONY: all test clean

all: $(TARGET)

build:
	mkdir -p build

build/grammar.tab.c build/grammar.tab.h &: grammar/grammar.y include/ast.h | build
	$(BISON) -d -o build/grammar.tab.c grammar/grammar.y

build/lex.yy.c: grammar/lexer.l build/grammar.tab.h | build
	$(FLEX) -o build/lex.yy.c grammar/lexer.l

$(TARGET): Main.c src/ast.c src/parser.c src/dgml.c build/grammar.tab.c build/lex.yy.c include/ast.h include/parser.h include/dgml.h build/lex.yy.h
	$(CC) $(CFLAGS) Main.c src/ast.c src/parser.c src/dgml.c build/grammar.tab.c build/lex.yy.c -o $@

test: all
	./$(TARGET) tests/full_test.txt build/full_test.dgml

clean:
	rm -rf build
