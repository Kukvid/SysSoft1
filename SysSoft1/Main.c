
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "parser.h"
#include "dgml.h"

static char* read_file(const char* path)
{
    FILE* file = fopen(path, "rb");

    if (file == NULL) {
        return NULL;
    }

    char block[BUFSIZ];
    char* text = NULL;
    size_t length = 0;
    size_t count = 0;

    while ((count = fread(block, 1, sizeof(block), file)) > 0) {
        if (count > SIZE_MAX - length - 1) {
            free(text);
            fclose(file);
            return NULL;
        }

        char* new_text = realloc(text, length + count + 1);

        if (new_text == NULL) {
            free(text);
            fclose(file);
            return NULL;
        }

        text = new_text;
        memcpy(text + length, block, count);
        length += count;
        text[length] = '\0';
    }

    if (ferror(file)) {
        free(text);
        fclose(file);
        return NULL;
    }

    if (fclose(file) != 0) {
        free(text);
        return NULL;
    }

    if (text == NULL) {
        text = malloc(1);

        if (text == NULL) {
            return NULL;
        }

        text[0] = '\0';
    }

    return text;
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input-file> <output-file>\n", argv[0]);
        return 1;
    }

    char* text = read_file(argv[1]);

    if (text == NULL) {
        fprintf(stderr, "Cannot read input file: %s\n", argv[1]);
        return 1;
    }

    ParseResult result = parse(text);
    free(text);

    for (size_t i = 0; i < result.error_count; i++) {
        fprintf(stderr, "Parse error: %s\n", result.errors[i]);
    }

    int success = (result.root != NULL && result.error_count == 0);

    if (success) {
        if (dgml_write(result.root, argv[2]) != 0) {
            fprintf(stderr, "Cannot write DGML file: %s\n", argv[2]);
            success = 0;
        }
    }

    parse_result_free(&result);

    return success ? 0 : 1;
}
