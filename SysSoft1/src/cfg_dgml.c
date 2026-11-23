
#include "cfg_dgml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int xml_text(FILE* out, const char* text)
{
    if (text == NULL)
        return 0;

    const char* start = text;
    const char* p = text;

    while (*p != '\0') {
        const char* escape = NULL;

        switch (*p) {
        case '&':  escape = "&amp;";  break;
        case '<':  escape = "&lt;";   break;
        case '>':  escape = "&gt;";   break;
        case '"':  escape = "&quot;"; break;
        case '\'': escape = "&apos;"; break;
        case '\n': escape = "&#10;";  break;
        case '\r': escape = "&#13;";  break;
        default: break;
        }

        if (escape != NULL) {
            size_t length = (size_t)(p - start);

            if (length != 0 &&
                fwrite(start, 1, length, out) != length)
                return -1;

            if (fputs(escape, out) == EOF)
                return -1;

            start = p + 1;
        }

        ++p;
    }

    size_t length = (size_t)(p - start);

    if (length != 0 &&
        fwrite(start, 1, length, out) != length)
        return -1;

    return 0;
}

static const char* op_name(OpKind kind)
{
    switch (kind) {
    case OP_IDENTIFIER:     return "IDENTIFIER";
    case OP_BOOL_LITERAL:   return "BOOL";
    case OP_STRING_LITERAL: return "STRING";
    case OP_CHAR_LITERAL:   return "CHAR";
    case OP_HEX_LITERAL:    return "HEX";
    case OP_BITS_LITERAL:   return "BITS";
    case OP_DEC_LITERAL:    return "DEC";
    case OP_UNARY:          return "UNARY";
    case OP_BINARY:         return "BINARY";
    case OP_CALL:           return "CALL";
    case OP_SLICE:          return "SLICE";
    case OP_RANGE:          return "RANGE";
    default:                return "UNKNOWN";
    }
}

static int operation_nodes(
    FILE* out,
    const OpNode* node,
    size_t* next_id
)
{
    if (node == NULL)
        return -1;

    size_t id = (*next_id)++;

    if (fprintf(
        out,
        "    <Node Id=\"o%zu\" Label=\"%s",
        id,
        op_name(node->kind)
    ) < 0)
        return -1;

    if (node->value != NULL) {
        if (fputs(": ", out) == EOF ||
            xml_text(out, node->value) != 0)
            return -1;
    }

    if (node->kind == OP_IDENTIFIER &&
        node->symbol.function_id != ANALYSIS_NO_ID &&
        node->symbol.symbol_index != ANALYSIS_NO_ID) {

        if (fprintf(
            out,
            " [f%zu:s%zu]",
            node->symbol.function_id,
            node->symbol.symbol_index
        ) < 0)
            return -1;
    }


    if (fputs(
        "\" Category=\"Operation\" />\n",
        out
    ) == EOF)
        return -1;

    for (size_t i = 0; i < node->child_count; ++i) {
        if (operation_nodes(
            out,
            node->children[i],
            next_id
        ) != 0)
            return -1;
    }

    return 0;
}

static int operation_links(
    FILE* out,
    const OpNode* node,
    size_t* next_id,
    size_t parent_id,
    int parent_is_block,
    const char* category
)
{
    if (node == NULL)
        return -1;

    size_t id = (*next_id)++;

    if (parent_is_block) {
        if (fprintf(
            out,
            "    <Link Source=\"b%zu\" Target=\"o%zu\" Category=\"%s\" />\n",
            parent_id,
            id,
            category
        ) < 0)
            return -1;
    }
    else {
        if (fprintf(
            out,
            "    <Link Source=\"o%zu\" Target=\"o%zu\" Category=\"Operand\" />\n",
            parent_id,
            id
        ) < 0)
            return -1;
    }

    for (size_t i = 0; i < node->child_count; ++i) {
        if (operation_links(
            out,
            node->children[i],
            next_id,
            id,
            0,
            "Operand"
        ) != 0)
            return -1;
    }

    return 0;
}

static int dgml_start(FILE* out)
{
    return fputs(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<DirectedGraph xmlns=\"http://schemas.microsoft.com/vs/2009/dgml\">\n",
        out
    ) == EOF ? -1 : 0;
}

static int finish_file(
    FILE* out,
    const char* path,
    int status
)
{
    if (fclose(out) != 0)
        status = -1;

    if (status != 0)
        remove(path);

    return status;
}

int cfg_write_dgml(
    const CfgGraph* graph,
    const char* path
)
{
    if (graph == NULL || path == NULL)
        return -1;

    FILE* out = fopen(path, "wb");

    if (out == NULL)
        return -1;

    int status = 0;
    size_t next_id = 0;

    if (dgml_start(out) != 0 ||
        fputs("  <Nodes>\n", out) == EOF)
        status = -1;

    for (size_t i = 0;
        i < graph->block_count && status == 0;
        ++i) {

        const CfgBlock* block = graph->blocks[i];

        const char* label =
            block == graph->entry ? "ENTRY" :
            block == graph->exit ? "EXIT" : "BLOCK";

        if (fprintf(
            out,
            "    <Node Id=\"b%zu\" Label=\"%s B%zu\" Category=\"BasicBlock\" />\n",
            block->id,
            label,
            block->id
        ) < 0)
            status = -1;
    }

    for (size_t i = 0;
        i < graph->block_count && status == 0;
        ++i) {

        const CfgBlock* block = graph->blocks[i];

        for (size_t j = 0; j < block->operation_count; ++j) {
            if (operation_nodes(
                out,
                block->operations[j],
                &next_id
            ) != 0)
                status = -1;
        }

        if (block->condition != NULL &&
            operation_nodes(
                out,
                block->condition,
                &next_id
            ) != 0)
            status = -1;
    }

    if (status == 0 &&
        fputs("  </Nodes>\n  <Links>\n", out) == EOF)
        status = -1;

    for (size_t i = 0;
        i < graph->block_count && status == 0;
        ++i) {

        const CfgBlock* block = graph->blocks[i];
        const CfgTerminator* t = &block->terminator;

        if (t->kind == CFG_TERM_JUMP &&
            t->jump_target != NULL) {

            if (fprintf(
                out,
                "    <Link Source=\"b%zu\" Target=\"b%zu\" Category=\"Control\" />\n",
                block->id,
                t->jump_target->id
            ) < 0)
                status = -1;
        }
        else if (t->kind == CFG_TERM_BRANCH) {
            if (t->true_target == NULL ||
                t->false_target == NULL) {
                status = -1;
                break;
            }

            if (fprintf(
                out,
                "    <Link Source=\"b%zu\" Target=\"b%zu\" Label=\"True\" Category=\"Control\" />\n"
                "    <Link Source=\"b%zu\" Target=\"b%zu\" Label=\"False\" Category=\"Control\" />\n",
                block->id,
                t->true_target->id,
                block->id,
                t->false_target->id
            ) < 0)
                status = -1;
        }
    }

    next_id = 0;

    for (size_t i = 0;
        i < graph->block_count && status == 0;
        ++i) {

        const CfgBlock* block = graph->blocks[i];

        for (size_t j = 0; j < block->operation_count; ++j) {
            if (operation_links(
                out,
                block->operations[j],
                &next_id,
                block->id,
                1,
                "Contains"
            ) != 0)
                status = -1;
        }

        if (block->condition != NULL &&
            operation_links(
                out,
                block->condition,
                &next_id,
                block->id,
                1,
                "Condition"
            ) != 0)
            status = -1;
    }

    if (status == 0 &&
        fputs("  </Links>\n</DirectedGraph>\n", out) == EOF)
        status = -1;

    if (ferror(out))
        status = -1;

    return finish_file(out, path, status);
}

int callgraph_write_dgml(
    const AnalysisResult* result,
    const char* path
)
{
    if (result == NULL || path == NULL)
        return -1;

    FILE* out = fopen(path, "wb");

    if (out == NULL)
        return -1;

    int status = 0;

    if (dgml_start(out) != 0 ||
        fputs("  <Nodes>\n", out) == EOF)
        status = -1;

    for (size_t i = 0;
        i < result->function_count && status == 0;
        ++i) {

        const FunctionInfo* fn = result->functions[i];

        if (fprintf(
            out,
            "    <Node Id=\"f%zu\" Label=\"",
            fn->id
        ) < 0 ||
            xml_text(out, fn->qualified_name) != 0 ||
            fputs("\" />\n", out) == EOF)
            status = -1;
    }

    if (status == 0 &&
        fputs("  </Nodes>\n  <Links>\n", out) == EOF)
        status = -1;

    for (size_t i = 0;
        i < result->call_count && status == 0;
        ++i) {

        const CallEdge* call = &result->calls[i];

        if (fprintf(
            out,
            "    <Link Source=\"f%zu\" Target=\"f%zu\" />\n",
            call->caller_function_id,
            call->callee_function_id
        ) < 0)
            status = -1;
    }

    if (status == 0 &&
        fputs("  </Links>\n</DirectedGraph>\n", out) == EOF)
        status = -1;

    if (ferror(out))
        status = -1;

    return finish_file(out, path, status);
}


static void dgml_g_xml(FILE* out, const char* text)
{
    if (!text) return;

    for (const unsigned char* p =
        (const unsigned char*)text; *p; ++p) {
        switch (*p) {
        case '&': fputs("&amp;", out); break;
        case '<': fputs("&lt;", out); break;
        case '>': fputs("&gt;", out); break;
        case '"': fputs("&quot;", out); break;
        case '\'': fputs("&apos;", out); break;
        case '\n': fputs("&#10;", out); break;
        case '\r': fputs("&#13;", out); break;
        default: fputc(*p, out); break;
        }
    }
}

static const char* dgml_g_kind(OpKind kind)
{
    switch (kind) {
    case OP_IDENTIFIER: return "IDENTIFIER";
    case OP_BOOL_LITERAL: return "BOOL";
    case OP_STRING_LITERAL: return "STRING";
    case OP_CHAR_LITERAL: return "CHAR";
    case OP_HEX_LITERAL: return "HEX";
    case OP_BITS_LITERAL: return "BITS";
    case OP_DEC_LITERAL: return "DEC";
    case OP_UNARY: return "UNARY";
    case OP_BINARY: return "BINARY";
    case OP_CALL: return "CALL";
    case OP_SLICE: return "SLICE";
    case OP_RANGE: return "RANGE";
    }

    return "OP";
}

static void dgml_g_op_nodes(
    FILE* out,
    const OpNode* op,
    size_t* id,
    const char* prefix
)
{
    if (!op) return;

    size_t current = (*id)++;

    fprintf(out, "    <Node Id=\"o%zu\" Label=\"", current);

    if (prefix)
        dgml_g_xml(out, prefix);

    dgml_g_xml(out, dgml_g_kind(op->kind));

    if (op->value) {
        fputs(": ", out);
        dgml_g_xml(out, op->value);
    }

    fputs("\" Category=\"Operation\" />\n", out);

    for (size_t i = 0; i < op->child_count; ++i) {
        dgml_g_op_nodes(
            out,
            op->children[i],
            id,
            NULL
        );
    }
}

static void dgml_g_op_links(
    FILE* out,
    const OpNode* op,
    size_t block_id,
    size_t parent,
    size_t* id
)
{
    if (!op) return;

    size_t current = (*id)++;

    fprintf(
        out,
        "    <Link Source=\"b%zu\" Target=\"o%zu\" "
        "Category=\"Contains\" />\n",
        block_id,
        current
    );

    if (parent != ANALYSIS_NO_ID) {
        fprintf(
            out,
            "    <Link Source=\"o%zu\" Target=\"o%zu\" "
            "Category=\"Operand\" />\n",
            parent,
            current
        );
    }

    for (size_t i = 0; i < op->child_count; ++i) {
        dgml_g_op_links(
            out,
            op->children[i],
            block_id,
            current,
            id
        );
    }
}


static void dgml_g_print_expr(FILE* out, const OpNode* op)
{
    if (op == NULL) {
        fputs("?", out);
        return;
    }

    switch (op->kind) {
    case OP_IDENTIFIER:
    case OP_BOOL_LITERAL:
    case OP_STRING_LITERAL:
    case OP_CHAR_LITERAL:
    case OP_HEX_LITERAL:
    case OP_BITS_LITERAL:
    case OP_DEC_LITERAL:
        dgml_g_xml(out, op->value ? op->value : "?");
        break;

    case OP_UNARY:
        dgml_g_xml(out, op->value ? op->value : "?");

        if (op->child_count > 0)
            dgml_g_print_expr(out, op->children[0]);

        break;

    case OP_BINARY:
        if (op->child_count >= 2) {
            fputc('(', out);

            dgml_g_print_expr(out, op->children[0]);

            fputc(' ', out);
            dgml_g_xml(out, op->value ? op->value : "?");
            fputc(' ', out);

            dgml_g_print_expr(out, op->children[1]);

            fputc(')', out);
        }
        break;

    case OP_CALL:
        if (op->child_count > 0)
            dgml_g_print_expr(out, op->children[0]);

        fputc('(', out);

        for (size_t i = 1; i < op->child_count; ++i) {
            if (i > 1)
                fputs(", ", out);

            dgml_g_print_expr(out, op->children[i]);
        }

        fputc(')', out);
        break;

    case OP_SLICE:
        if (op->child_count > 0)
            dgml_g_print_expr(out, op->children[0]);

        fputc('[', out);

        for (size_t i = 1; i < op->child_count; ++i) {
            if (i > 1)
                fputs(", ", out);

            dgml_g_print_expr(out, op->children[i]);
        }

        fputc(']', out);
        break;

    case OP_RANGE:
        if (op->child_count > 0)
            dgml_g_print_expr(out, op->children[0]);

        if (op->child_count > 1) {
            fputs("..", out);
            dgml_g_print_expr(out, op->children[1]);
        }

        break;
    }
}


int cfg_write_grouped_dgml(
    const CfgGraph* graph,
    const char* path
)
{
    if (!graph || !path)
        return -1;

    FILE* out = fopen(path, "wb");

    if (!out)
        return -1;

    fputs(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<DirectedGraph "
        "xmlns=\"http://schemas.microsoft.com/vs/2009/dgml\">\n"
        "  <Nodes>\n",
        out
    );


    for (size_t i = 0; i < graph->block_count; ++i) {
        const CfgBlock* b = graph->blocks[i];

        if (!b)
            continue;

        const char* role = b == graph->entry
            ? "ENTRY"
            : b == graph->exit
            ? "EXIT"
            : "BLOCK";

        fprintf(
            out,
            "    <Node Id=\"b%zu\" Label=\"%s B%zu",
            b->id,
            role,
            b->id
        );

        for (size_t j = 0; j < b->operation_count; ++j) {
            fprintf(out, "&#10;%zu. ", j + 1);

            dgml_g_print_expr(
                out,
                b->operations[j]
            );

            fputc(';', out);
        }

        if (b->condition != NULL) {
            fputs("&#10;if ", out);
            dgml_g_print_expr(out, b->condition);
        }

        switch (b->terminator.kind) {
        case CFG_TERM_JUMP:
            if (b->terminator.jump_target) {
                fprintf(
                    out,
                    "&#10;goto B%zu",
                    b->terminator.jump_target->id
                );
            }
            break;

        case CFG_TERM_BRANCH:
            if (b->terminator.true_target) {
                fprintf(
                    out,
                    "&#10;True -&gt; B%zu",
                    b->terminator.true_target->id
                );
            }

            if (b->terminator.false_target) {
                fprintf(
                    out,
                    "&#10;False -&gt; B%zu",
                    b->terminator.false_target->id
                );
            }
            break;

        case CFG_TERM_EXIT:
            fputs("&#10;exit", out);
            break;

        default:
            break;
        }

        fputs("\" Category=\"BasicBlock\"", out);

        if (b->operation_count != 0 ||
            b->condition != NULL) {
            fputs(" Group=\"Collapsed\"", out);
        }

        fputs(" />\n", out);
    }


    size_t id = 0;

    for (size_t i = 0; i < graph->block_count; ++i) {
        const CfgBlock* b = graph->blocks[i];

        if (!b)
            continue;

        for (size_t j = 0; j < b->operation_count; ++j) {
            char prefix[48];

            snprintf(
                prefix,
                sizeof(prefix),
                "%zu. ",
                j + 1
            );

            dgml_g_op_nodes(
                out,
                b->operations[j],
                &id,
                prefix
            );
        }

        dgml_g_op_nodes(
            out,
            b->condition,
            &id,
            "IF: "
        );
    }

    fputs("  </Nodes>\n  <Links>\n", out);

    id = 0;

    for (size_t i = 0; i < graph->block_count; ++i) {
        const CfgBlock* b = graph->blocks[i];

        if (!b)
            continue;

        for (size_t j = 0; j < b->operation_count; ++j) {
            dgml_g_op_links(
                out,
                b->operations[j],
                b->id,
                ANALYSIS_NO_ID,
                &id
            );
        }

        dgml_g_op_links(
            out,
            b->condition,
            b->id,
            ANALYSIS_NO_ID,
            &id
        );

        switch (b->terminator.kind) {
        case CFG_TERM_JUMP:
            if (b->terminator.jump_target) {
                fprintf(
                    out,
                    "    <Link Source=\"b%zu\" "
                    "Target=\"b%zu\" "
                    "Category=\"Control\" />\n",
                    b->id,
                    b->terminator.jump_target->id
                );
            }
            break;

        case CFG_TERM_BRANCH:
            if (b->terminator.true_target) {
                fprintf(
                    out,
                    "    <Link Source=\"b%zu\" "
                    "Target=\"b%zu\" Label=\"True\" "
                    "Category=\"Control\" />\n",
                    b->id,
                    b->terminator.true_target->id
                );
            }

            if (b->terminator.false_target) {
                fprintf(
                    out,
                    "    <Link Source=\"b%zu\" "
                    "Target=\"b%zu\" Label=\"False\" "
                    "Category=\"Control\" />\n",
                    b->id,
                    b->terminator.false_target->id
                );
            }
            break;

        default:
            break;
        }
    }

    fputs(
        "  </Links>\n"
        "  <Categories>\n"
        "    <Category Id=\"BasicBlock\" "
        "Label=\"Basic Block\" />\n"
        "    <Category Id=\"Operation\" "
        "Label=\"Operation\" />\n"
        "    <Category Id=\"Operand\" "
        "Label=\"Operand\" />\n"
        "    <Category Id=\"Control\" "
        "Label=\"Control Flow\" />\n"
        "  </Categories>\n"
        "</DirectedGraph>\n",
        out
    );

    int failed = ferror(out);

    if (fclose(out) != 0)
        failed = 1;

    return failed ? -1 : 0;
}

