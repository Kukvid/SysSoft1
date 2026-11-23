
#include "parser.h"
#include "analysis.h"
#include "cfg.h"
#include "cfg_dgml.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static char* read_source(const char* path)
{
    FILE* input = fopen(path, "rb");

    if (input == NULL)
        return NULL;

    char* text = NULL;
    size_t length = 0;
    size_t capacity = 0;

    char block[BUFSIZ];
    int failed = 0;
    size_t count = 0;

    while ((count = fread(
        block,
        1,
        sizeof(block),
        input
    )) != 0) {

        if (length > (size_t)INT_MAX - count) {
            failed = 1;
            break;
        }

        size_t needed = length + count + 1;

        if (needed > capacity) {
            size_t new_capacity = capacity == 0
                ? sizeof(block) + 1
                : capacity;

            while (new_capacity < needed) {
                if (new_capacity > SIZE_MAX / 2) {
                    new_capacity = needed;
                    break;
                }

                new_capacity *= 2;
            }

            char* resized = realloc(text, new_capacity);

            if (resized == NULL) {
                failed = 1;
                break;
            }

            text = resized;
            capacity = new_capacity;
        }

        memcpy(text + length, block, count);
        length += count;
    }

    if (ferror(input))
        failed = 1;

    if (fclose(input) != 0)
        failed = 1;

    if (failed) {
        free(text);
        return NULL;
    }

    if (text == NULL) {
        text = malloc(1);

        if (text == NULL)
            return NULL;
    }

    text[length] = '\0';

    return text;
}

static char* copy_n(const char* text, size_t count)
{
    char* copy = malloc(count + 1);

    if (copy == NULL)
        return NULL;

    memcpy(copy, text, count);
    copy[count] = '\0';

    return copy;
}

static char* source_dir(const char* path)
{
    const char* slash = strrchr(path, '/');

    if (slash == NULL)
        return copy_n(".", 1);

    if (slash == path)
        return copy_n("/", 1);

    return copy_n(path, (size_t)(slash - path));
}

static char* source_stem(const char* path)
{
    const char* start = strrchr(path, '/');

    start = start == NULL ? path : start + 1;

    const char* dot = strrchr(start, '.');

    size_t count = dot != NULL && dot != start
        ? (size_t)(dot - start)
        : strlen(start);

    return copy_n(start, count);
}

static char* join_path(
    const char* dir,
    const char* filename
)
{
    size_t a = strlen(dir);
    size_t b = strlen(filename);

    if (a > SIZE_MAX - b - 2)
        return NULL;

    char* path = malloc(a + b + 2);

    if (path == NULL)
        return NULL;

    memcpy(path, dir, a);
    path[a] = '/';

    memcpy(path + a + 1, filename, b + 1);

    return path;
}

static int ensure_directory(const char* path)
{
    size_t length = strlen(path);

    char* copy = copy_n(path, length);

    if (copy == NULL)
        return -1;

    for (size_t i = 1; i <= length; ++i) {
        if (copy[i] != '/' && copy[i] != '\0')
            continue;

        char saved = copy[i];
        copy[i] = '\0';

        if (copy[0] != '\0' &&
            mkdir(copy, 0777) != 0 &&
            errno != EEXIST) {

            free(copy);
            return -1;
        }

        copy[i] = saved;
    }

    struct stat info;

    int okay = stat(path, &info) == 0 &&
        S_ISDIR(info.st_mode);

    free(copy);

    return okay ? 0 : -1;
}

static char* function_filename(
    const AnalysisResult* result,
    const FunctionInfo* fn
)
{
    const char* source = result->files[fn->file_index].path;
    char* stem = source_stem(source);

    if (stem == NULL)
        return NULL;

    int collision = 0;

    for (size_t i = 0; i < result->function_count; ++i) {
        const FunctionInfo* other = result->functions[i];

        if (other == fn ||
            strcmp(other->name, fn->name) != 0)
            continue;

        char* other_stem = source_stem(
            result->files[other->file_index].path
        );

        if (other_stem == NULL) {
            free(stem);
            return NULL;
        }

        if (strcmp(stem, other_stem) == 0)
            collision = 1;

        free(other_stem);

        if (collision)
            break;
    }

    char suffix[32] = "";

    if (collision)
        snprintf(suffix, sizeof(suffix), "__f%zu", fn->id);

    size_t a = strlen(stem);
    size_t b = strlen(fn->name);
    size_t s = strlen(suffix);
    size_t c = strlen(".dgml");

    if (a > SIZE_MAX - b - 2 ||
        a + b + 1 > SIZE_MAX - s - c - 1) {
        free(stem);
        return NULL;
    }

    size_t length = a + b + 1 + s + c + 1;
    char* filename = malloc(length);

    if (filename != NULL) {
        snprintf(
            filename,
            length,
            "%s.%s%s.dgml",
            stem,
            fn->name,
            suffix
        );
    }

    free(stem);
    return filename;
}

static char* make_callgraph_path(
    const AnalysisResult* result,
    const char* output_dir
);

static int save_graphs(
    const AnalysisResult* result,
    const char* output_dir
)
{
    int failed = 0;

    for (size_t i = 0; i < result->function_count; ++i) {
        const FunctionInfo* fn = result->functions[i];

        const char* source =
            result->files[fn->file_index].path;

        char* dir = output_dir != NULL
            ? copy_n(output_dir, strlen(output_dir))
            : source_dir(source);

        char* filename = function_filename(result, fn);

        char* path = dir != NULL && filename != NULL
            ? join_path(dir, filename)
            : NULL;

        if (path == NULL ||
            cfg_write_grouped_dgml(&fn->graph, path) != 0) {

            fprintf(
                stderr,
                "Cannot write CFG for %s\n",
                fn->qualified_name
            );

            failed = 1;
        }
        else {
            printf("CFG: %s\n", path);
        }

        free(dir);
        free(filename);
        free(path);
    }

    char* callgraph_path = make_callgraph_path(
        result,
        output_dir
    );

    if (callgraph_path == NULL) {
        fprintf(stderr,
            "Cannot determine callgraph path: "
            "main is missing, duplicated, or memory allocation failed\n");
        failed = 1;
    }
    else {
        if (callgraph_write_dgml(result, callgraph_path) != 0)
            failed = 1;

        free(callgraph_path);
    }

    return failed;
}


static char* make_callgraph_path(
    const AnalysisResult* result,
    const char* output_dir
)
{
    const char* base = NULL;
    size_t prefix_length = 0;
    int add_separator = 0;

    if (output_dir && output_dir[0]) {
        base = output_dir;
        prefix_length = strlen(base);

        char last = base[prefix_length - 1];
        add_separator = last != '/' && last != '\\';
    }
    else {
        const FunctionInfo* main_function = NULL;

        for (size_t i = 0; i < result->function_count; ++i) {
            const FunctionInfo* function = result->functions[i];

            if (function->parent_function_id != ANALYSIS_NO_ID ||
                strcmp(function->name, "main") != 0)
                continue;

            if (main_function != NULL)
                return NULL;

            main_function = function;
        }

        if (!main_function ||
            main_function->file_index >= result->file_count)
            return NULL;

        base = result->files[main_function->file_index].path;

        const char* slash = strrchr(base, '/');
        const char* backslash = strrchr(base, '\\');

        if (!slash || (backslash && backslash > slash))
            slash = backslash;

        prefix_length = slash
            ? (size_t)(slash - base + 1)
            : 0;
    }

    const char* filename = "callgraph.dgml";
    size_t filename_length = strlen(filename);

    if (prefix_length >
        SIZE_MAX - filename_length - (size_t)add_separator - 1)
        return NULL;

    char* path = malloc(
        prefix_length + (size_t)add_separator +
        filename_length + 1
    );

    if (!path)
        return NULL;

    if (prefix_length)
        memcpy(path, base, prefix_length);

    size_t position = prefix_length;

    if (add_separator)
        path[position++] = '/';

    memcpy(path + position, filename, filename_length + 1);

    return path;
}


int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(
            stderr,
            "Usage: %s [-o output_directory] file1 [file2 ...]\n",
            argv[0]
        );

        return 1;
    }

    const char* output_dir = NULL;

    const char** paths = calloc(
        (size_t)argc,
        sizeof(*paths)
    );

    if (paths == NULL)
        return 1;

    size_t file_count = 0;
    int failed = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc || output_dir != NULL) {
                failed = 1;
                break;
            }

            output_dir = argv[i];
        }
        else {
            paths[file_count++] = argv[i];
        }
    }

    if (failed || file_count == 0) {
        fprintf(
            stderr,
            "Usage: %s [-o output_directory] file1 [file2 ...]\n",
            argv[0]
        );

        free(paths);
        return 1;
    }

    if (output_dir != NULL &&
        ensure_directory(output_dir) != 0) {

        fprintf(
            stderr,
            "Cannot create output directory: %s\n",
            output_dir
        );

        free(paths);
        return 1;
    }

    ParseResult* parsed = calloc(
        file_count,
        sizeof(*parsed)
    );

    AnalysisSource* sources = calloc(
        file_count,
        sizeof(*sources)
    );

    if (parsed == NULL || sources == NULL) {
        free(parsed);
        free(sources);
        free(paths);

        return 1;
    }

    for (size_t i = 0; i < file_count; ++i) {
        char* text = read_source(paths[i]);

        if (text == NULL) {
            fprintf(
                stderr,
                "Cannot read input file: %s\n",
                paths[i]
            );

            failed = 1;
            continue;
        }

        parsed[i] = parse(text);

        free(text);

        if (parsed[i].error_count != 0 ||
            parsed[i].root == NULL) {

            for (size_t j = 0; j < parsed[i].error_count; ++j) {
                fprintf(
                    stderr,
                    "%s: %s\n",
                    paths[i],
                    parsed[i].errors[j]
                );
            }

            if (parsed[i].error_count == 0) {
                fprintf(
                    stderr,
                    "%s: parsing failed\n",
                    paths[i]
                );
            }

            failed = 1;
        }

        sources[i] = (AnalysisSource){
            paths[i],
            parsed[i].root
        };
    }

    AnalysisResult result = { 0 };

    if (!failed) {

        result = analyze_program(sources, file_count);

        if (result.status != ANALYSIS_OK)
            failed = 1;

        for (size_t i = 0; i < result.error_count; ++i) {
            const AnalysisError* error = &result.errors[i];

            const char* path =
                error->file_index < result.file_count
                ? result.files[error->file_index].path
                : "<program>";

            fprintf(
                stderr,
                "%s:%zu:%zu: %s\n",
                path,
                error->location.line,
                error->location.column,
                error->message
            );
        }

        if (result.status == ANALYSIS_OUT_OF_MEMORY)
            fprintf(stderr, "Analysis failed: out of memory\n");

        if (!failed &&
            save_graphs(&result, output_dir) != 0)
            failed = 1;
    }

    analysis_result_free(&result);

    for (size_t i = 0; i < file_count; ++i)
        parse_result_free(&parsed[i]);

    free(parsed);
    free(sources);
    free(paths);

    return failed ? 1 : 0;
}
