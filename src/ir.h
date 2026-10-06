// Intermediate representation: typed BASIC expressions and statements.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace qb {

enum class Ty : uint8_t { Unknown, Int, Sng, Dbl, Str };

const char* tySuffix(Ty t); // "%", "", "#", "$"
const char* tyName(Ty t);
// Type of a binary arithmetic operation on operands of the given types.
Ty promote(Ty a, Ty b);

enum class Op : uint8_t {
    // binary
    Add, Sub, Mul, Div, IDiv, Mod, Pow,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or, Xor, Eqv, Imp,
    // unary
    Neg, Not,
};

bool isCompare(Op op);
Op invertCompare(Op op);   // = <-> <>, < <-> >=, ...
Op swapCompare(Op op);     // a < b  <=> b > a
const char* opText(Op op);

struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;

enum class EK : uint8_t {
    IntLit,    // ival
    SngLit,    // dval
    DblLit,    // dval
    StrLit,    // sval
    Var,       // scalar variable in DS: addr, type
    Param,     // SUB parameter (by reference): index (0 based), type
    ArrElem,   // array element: addr = array base / descriptor, args = indices (or one byte offset)
    Unary,     // op, args[0]
    Binary,    // op, args[0], args[1]
    Func,      // builtin function: name, args
    FnCall,    // DEF FN call: addr = function entry, args
    Conv,      // conversion of args[0] to type (implicit unless needed)
    PtrTo,     // register holding a pointer to the value args[0] (string temps, ...)
    Phi,       // value that differs between predecessors of a join: addr = block, ival = location id
    Unknown,   // value that could not be determined: sval = reason
    Raw,       // raw text inserted verbatim (labels, keywords)
};

// Flags for ArrElem.
enum : uint8_t {
    AF_ByteOffset = 1,  // args[0] is a byte offset from the array start, not yet converted
    AF_Dynamic = 2,     // $DYNAMIC array (addr is the descriptor)
    AF_Param = 4,       // array SUB parameter (ival = parameter index)
};

struct Expr {
    EK kind = EK::Unknown;
    Ty type = Ty::Unknown;
    Op op = Op::Add;
    uint8_t flags = 0;
    uint16_t addr = 0;
    int32_t ival = 0;
    double dval = 0;
    std::string sval;
    std::vector<ExprPtr> args;
};

// Constructors
ExprPtr mkInt(int32_t v);
ExprPtr mkSng(double v);
ExprPtr mkDbl(double v);
ExprPtr mkStr(const std::string& s);
ExprPtr mkVar(uint16_t addr, Ty t);
ExprPtr mkParam(int index, Ty t);
ExprPtr mkArrElem(uint16_t base, Ty t, std::vector<ExprPtr> idx, uint8_t flags, int paramIndex = -1);
ExprPtr mkUnary(Op op, ExprPtr a, Ty t);
ExprPtr mkBinary(Op op, ExprPtr a, ExprPtr b, Ty t);
ExprPtr mkFunc(const std::string& name, std::vector<ExprPtr> args, Ty t);
ExprPtr mkFnCall(uint16_t addr, std::vector<ExprPtr> args, Ty t);
ExprPtr mkConv(ExprPtr a, Ty t);
ExprPtr mkPtrTo(ExprPtr a);
ExprPtr mkPhi(uint32_t block, int loc);
ExprPtr mkUnknown(const std::string& why, Ty t = Ty::Unknown);
ExprPtr mkRaw(const std::string& text, Ty t = Ty::Unknown);

bool exprEqual(const ExprPtr& a, const ExprPtr& b);
// number of nodes of an expression tree
int exprWeight(const ExprPtr& e);
bool isIntLit(const ExprPtr& e, int32_t* v = nullptr);
bool isUnknown(const ExprPtr& e);
// Strips implicit conversions.
ExprPtr stripConv(const ExprPtr& e);

// Calls f on every node of the tree (pre-order).
template <class F>
void walkExpr(const ExprPtr& e, F&& f) {
    if (!e) return;
    f(e);
    for (const auto& a : e->args) walkExpr(a, f);
}

// Rebuilds a tree bottom-up: f receives a node whose children were already
// rewritten and returns the replacement (or the node itself).
template <class F>
ExprPtr rewriteExpr(const ExprPtr& e, F&& f) {
    if (!e) return e;
    bool changed = false;
    std::vector<ExprPtr> na;
    na.reserve(e->args.size());
    for (const auto& a : e->args) {
        ExprPtr r = rewriteExpr(a, f);
        if (r != a) changed = true;
        na.push_back(r);
    }
    ExprPtr cur = e;
    if (changed) {
        auto c = std::make_shared<Expr>(*e);
        c->args = std::move(na);
        cur = c;
    }
    return f(cur);
}

// ---------------------------------------------------------------------------
// Statements

enum class SK : uint8_t {
    Assign,     // lhs = rhs
    Text,       // generic statement: parts (text and expressions)
    Goto,       // GOTO target
    Gosub,      // GOSUB target
    Return,     // RETURN [target]
    End,        // END / SYSTEM / STOP ... (text)
    Label,      // label definition (only in structured output)
    Comment,
};

struct Part {
    std::string text;  // emitted verbatim when expr is null
    ExprPtr expr;
    Ty want = Ty::Unknown;  // context type for expr (conversions that are implied are dropped)
    bool keepType = false;  // print literals with their exact type (PRINT items)
};

struct Stmt {
    SK kind = SK::Text;
    uint16_t addr = 0;          // code address the statement came from
    ExprPtr lhs, rhs;           // Assign
    std::vector<Part> parts;    // Text / End
    uint16_t target = 0;        // Goto / Gosub / Return target
    bool hasTarget = false;
    std::string text;           // Comment / Label
    std::vector<uint16_t> targets; // ON n GOSUB targets (appended as labels)
};

Stmt mkAssign(uint16_t addr, ExprPtr lhs, ExprPtr rhs);
Stmt mkTextStmt(uint16_t addr, std::vector<Part> parts);
Stmt mkComment(uint16_t addr, const std::string& text);

// Helpers to build parts
Part P(const std::string& text);
Part P(ExprPtr e, Ty want = Ty::Unknown);
// an expression whose literal type must be kept (PRINT items)
Part PK(ExprPtr e);

} // namespace qb
