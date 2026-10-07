// What is known about the variables of a COMMON block. Programs that CHAIN to
// each other share the block by position, so what one program knows about it
// helps decompiling the others.
#pragma once

#include "ir.h"

#include <cstdint>
#include <vector>

namespace qb {

struct CommonItem {
    uint16_t addr = 0;
    uint32_t size = 0;      // bytes
    Ty type = Ty::Unknown;
    bool array = false;     // static array (addr is its first element)
    std::vector<int> dims;  // number of elements per dimension (arrays)
};

} // namespace qb
