
#include "dgml.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int write_xml_text(FILE* file, const char* text)
{
    const char* start = text;
    const char* current = text;

    while (*current != '\0') {
        const char* replacement = NULL;

        switch (*current) {
        case '&':  replacement = "&amp;";  break;
        case '<':  replacement = "&lt;";   break;
        case '>':  replacement = "&gt;";   break;
        case '"':  replacement = "&quot;"; break;
        case '\'': replacement = "&apos;"; break;
        default: break;
        }

        unsigned char ch = (unsigned char)*current;

        if (replacement == NULL &&
            ch < 32 && ch != '\t' && ch != '\n' && ch != '\r') {
            replacement = "?";
        }

        if (replacement != NULL) {
            size_t length = (size_t)(current - start);

            if (length > 0 &&
                fwrite(start, 1, length, file) != length) {
                return -1;
            }

            if (fputs(replacement, file) == EOF) {
                return -1;
            }

            start = current + 1;
        }

        current++;
    }

    size_t length = (size_t)(current - start);

    if (length > 0 &&
        fwrite(start, 1, length, file) != length) {
        return -1;
    }

    return 0;
}

static int write_nodes(FILE* file, const AstNode* node, size_t* next_id)
{
    if (node == NULL || *next_id == SIZE_MAX) {
        return -1;
    }

    size_t id = (*next_id)++;

    if (fprintf(file, "    <Node Id=\"n%zu\" Label=\"", id) < 0) {
        return -1;
    }

    if (write_xml_text(file, get_ast_type_name(node->type)) != 0) {
        return -1;
    }

    if (node->value != NULL) {
        if (fputs(": ", file) == EOF ||
            write_xml_text(file, node->value) != 0) {
            return -1;
        }
    }

    if (fputs("\" />\n", file) == EOF) {
        return -1;
    }

    for (size_t i = 0; i < node->child_count; i++) {
        if (write_nodes(file, node->children[i], next_id) != 0) {
            return -1;
        }
    }

    return 0;
}

static int write_links(FILE* file, const AstNode* node, size_t* next_id)
{
    if (node == NULL || *next_id == SIZE_MAX) {
        return -1;
    }

    size_t parent_id = (*next_id)++;

    for (size_t i = 0; i < node->child_count; i++) {
        const AstNode* child = node->children[i];

        if (child == NULL || *next_id == SIZE_MAX) {
            return -1;
        }

        size_t child_id = *next_id;

        if (fprintf(file,
            "    <Link Source=\"n%zu\" Target=\"n%zu\" />\n",
            parent_id, child_id) < 0) {
            return -1;
        }

        if (write_links(file, child, next_id) != 0) {
            return -1;
        }
    }

    return 0;
}

int dgml_write(const AstNode* root, const char* path)
{
    if (root == NULL || path == NULL) {
        return -1;
    }

    FILE* file = fopen(path, "wb");

    if (file == NULL) {
        return -1;
    }

    int status = 0;
    size_t next_id = 0;

    if (fputs(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<DirectedGraph xmlns=\"http://schemas.microsoft.com/vs/2009/dgml\">\n"
        "  <Nodes>\n",
        file
    ) == EOF) {
        status = -1;
    }

    if (status == 0 && write_nodes(file, root, &next_id) != 0) {
        status = -1;
    }

    if (status == 0 &&
        fputs("  </Nodes>\n  <Links>\n", file) == EOF) {
        status = -1;
    }

    next_id = 0;

    if (status == 0 && write_links(file, root, &next_id) != 0) {
        status = -1;
    }

    if (status == 0 &&
        fputs("  </Links>\n</DirectedGraph>\n", file) == EOF) {
        status = -1;
    }

    if (fclose(file) != 0) {
        status = -1;
    }

    if (status != 0) {
        remove(path);
    }

    return status;
}
