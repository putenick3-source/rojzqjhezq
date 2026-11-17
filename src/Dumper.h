// Dumper.h - Shared state between the Dumper tab and its scans.
//
// One global instance: the tab owns the inputs, the Scanner owns the queue and
// the found projection/arrays are kept for assignment to INI offsets.

#pragma once
#include "Projection.h"
#include "Scan.h"

#include <cstdint>
#include <vector>

struct DumperState {
    scan::Scanner          scanner;
    char                   valueBuf[32] = "100";
    float                  tol         = 0.5f;
    int                    kind        = 0; // scan::Kind as int (combo-backed)
    uintptr_t              baseEntity  = 0;
    char                   baseBuf[32] = {};
    std::vector<scan::PointerRun> arrays;
    Projection             foundVp;
    int                    vpCandidates = 0;
    char                   status[192]  = "pret";
};

inline DumperState& gDumper() {
    static DumperState state;
    return state;
}
