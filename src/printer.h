// Printing of IR expressions and statements as QuickBASIC 3.0 source text.
#pragma once

#include "ir.h"

#include <functional>
#include <map>
#include <string>

namespace qb {

struct ArrayDecl;

struct NameResolver {
    // variable name for a DS address and type, e.g. "vari1856%"
    std::function<std::string(uint16_t addr, Ty t)> var;
    // array name for a base/descriptor address
    std::function<std::string(uint16_t addr, Ty t)> array;
    // SUB parameter name
    std::function<std::string(int index, Ty t)> param;
    // DEF FN function name and parameter types
    std::function<std::string(uint16_t addr)> fn;
    std::function<Ty(uint16_t addr, size_t i)> fnParamType;
    // label for a code address
    std::function<std::string(uint16_t addr)> label;
    // compiled with /R (arrays in row order): QB stores the subscripts last first,
    // so the subscripts found in the code are printed in reverse
    bool rowOrder = false;
};

class Printer {
public:
    explicit Printer(const NameResolver& names) : names_(names) {}

    // `want` is the type the context converts the value to; implied
    // conversions are left out.
    std::string expr(const ExprPtr& e, Ty want = Ty::Unknown) const;
    // condition of IF / WHILE / LOOP UNTIL
    std::string cond(const ExprPtr& e) const;
    std::string stmt(const Stmt& s) const;

    static std::string quote(const std::string& s);
    static std::string number(const ExprPtr& lit);

private:
    std::string expr(const ExprPtr& e, int parentPrec, bool rightSide, Ty want) const;
    const NameResolver& names_;
};

} // namespace qb
