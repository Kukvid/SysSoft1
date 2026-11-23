
#ifndef OPSTREE_H
#define OPSTREE_H

#include "analysis.h"

typedef enum {
    OPSTREE_OK,             
    OPSTREE_INVALID_AST,    
    OPSTREE_OUT_OF_MEMORY
} OpsTreeStatus;

OpNode* opstree_from_ast(
    const AstNode* ast,
    OpsTreeStatus* status
);

void opstree_free(OpNode* root);

#endif
