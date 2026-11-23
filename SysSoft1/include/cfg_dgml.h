
#ifndef CFG_DGML_H
#define CFG_DGML_H

#include "analysis.h"

int cfg_write_dgml(
    const CfgGraph* graph,
    const char* path
);

int callgraph_write_dgml(
    const AnalysisResult* result,
    const char* path
);

int cfg_write_grouped_dgml(
    const CfgGraph* graph,
    const char* path
);

#endif
