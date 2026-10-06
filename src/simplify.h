// Expression simplification helpers.
#pragma once

#include "ir.h"

#include <map>
#include <vector>

namespace qb {

// Integer expression simplification: constant folding and normalisation of
// sums/products with constants (x*2*2 -> x*4, (x+1)-1 -> x, ...).
ExprPtr simplifyInt(const ExprPtr& e);

// Simplifies a condition (comparison of booleans with zero ...).
ExprPtr simplifyCond(const ExprPtr& e);
// Logical negation of a condition.
ExprPtr invertCond(const ExprPtr& e);
// True for comparisons and logical operators applied to comparisons.
bool isBoolean(const ExprPtr& e);

// Linear form of an integer expression: sum(coeff * atom) + constant.
struct Linear {
    std::vector<std::pair<ExprPtr, int32_t>> terms;
    int32_t constant = 0;
    void add(const ExprPtr& atom, int32_t c);
};
Linear toLinear(const ExprPtr& e);
ExprPtr fromLinear(const Linear& l);

// Simplifies a whole tree (all node types).
ExprPtr simplifyExpr(const ExprPtr& e);

} // namespace qb
