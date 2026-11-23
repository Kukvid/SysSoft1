
#ifndef CFG_H
#define CFG_H

#include "analysis.h"


int cfg_build_all(
    AnalysisResult* result,
    const AnalysisSource* sources,
    size_t source_count
);

#endif
