// Structuring and final BASIC source output.
#pragma once

#include "cfg.h"
#include "decompiler.h"
#include "lifter.h"
#include "postprocess.h"

#include <string>
#include <vector>

namespace qb {

std::string emitProgram(Program& program, std::vector<std::vector<LiftedBlock>>& results, GlobalInfo& info,
                        ProgramInfo& pinfo, const DecompileOptions& opt, const std::string& inputName,
                        std::vector<std::string>& warnings);

} // namespace qb
