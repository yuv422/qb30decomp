// Top level decompiler driver.
#pragma once

#include "common_layout.h"
#include "exe_loader.h"

#include <string>
#include <vector>

namespace qb {

struct DecompileOptions {
    bool dumpLifted = false;   // print lifted basic blocks instead of structured BASIC
    bool comments = true;      // emit header comments
    bool layoutOnly = false;   // only work out the COMMON layout (DecompileResult::common)
    // COMMON variables of programs this one CHAINs with (see commonLayout)
    std::vector<CommonItem> commonHints;
};

struct DecompileResult {
    std::string source;
    std::vector<std::string> warnings;
    std::vector<CommonItem> common;   // the program's COMMON variables (layoutOnly)
};

DecompileResult decompile(const LoadedProgram& prog, const DecompileOptions& opt);

// The variables of the program's COMMON block as far as the program shows them.
std::vector<CommonItem> commonLayout(const LoadedProgram& prog, const std::vector<CommonItem>& hints);

} // namespace qb
