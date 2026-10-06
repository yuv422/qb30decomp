// Top level decompiler driver.
#pragma once

#include "exe_loader.h"

#include <string>
#include <vector>

namespace qb {

struct DecompileOptions {
    bool dumpLifted = false;   // print lifted basic blocks instead of structured BASIC
    bool comments = true;      // emit header comments
};

struct DecompileResult {
    std::string source;
    std::vector<std::string> warnings;
};

DecompileResult decompile(const LoadedProgram& prog, const DecompileOptions& opt);

} // namespace qb
