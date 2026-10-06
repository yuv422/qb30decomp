// Whole program analyses run after lifting: array recovery, variable types,
// inlining of compiler temporaries.
#pragma once

#include "cfg.h"
#include "lifter.h"

#include <functional>
#include <map>
#include <set>
#include <vector>

namespace qb {

struct ArrayInfo {
    uint16_t base = 0;            // data address (static) or descriptor (dynamic)
    Ty type = Ty::Unknown;
    bool dynamic = false;
    std::vector<int> dims;        // number of elements per dimension (static arrays)
    int dynDims = 0;              // dynamic arrays: number of dimensions
    uint32_t bytes = 0;           // static: size in bytes
};

struct ProgramInfo {
    std::map<uint16_t, ArrayInfo> arrays;           // by base
    std::map<uint16_t, Ty> varTypes;                // scalar variables by address
    std::map<uint16_t, int> varRefs;                // number of references of each scalar variable
    std::map<uint16_t, std::set<uint32_t>> varProcs; // procs referencing a scalar variable
    std::map<uint16_t, std::set<uint32_t>> arrayProcs;
    std::map<uint16_t, std::pair<uint16_t, int>> arrayAliases; // displacement -> (array base, byte delta)
};

// Calls f for every expression slot of the lifted program (statements,
// conditions, selectors, DEF FN bodies); f may replace the expression.
void forEachExprSlot(std::vector<std::vector<LiftedBlock>>& results, GlobalInfo& info,
                     const std::function<void(ExprPtr&, uint32_t proc)>& f);

ProgramInfo postprocess(Program& program, std::vector<std::vector<LiftedBlock>>& results, GlobalInfo& info,
                        std::vector<std::string>& warnings);

// Static and dynamic arrays found in the lifted program (base, type, size, dimensions).
std::map<uint16_t, ArrayInfo> computeArrays(const LoadedProgram& prog, std::vector<std::vector<LiftedBlock>>& results,
                                            GlobalInfo& info, std::map<uint16_t, std::set<Ty>>& varTypes,
                                            std::map<uint16_t, std::pair<uint16_t, int>>& aliases);

} // namespace qb
