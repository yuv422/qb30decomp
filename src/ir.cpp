#include "ir.h"

namespace qb {

const char* tySuffix(Ty t) {
    switch (t) {
    case Ty::Int: return "%";
    case Ty::Dbl: return "#";
    case Ty::Str: return "$";
    default: return "";
    }
}

const char* tyName(Ty t) {
    switch (t) {
    case Ty::Int: return "INTEGER";
    case Ty::Sng: return "SINGLE";
    case Ty::Dbl: return "DOUBLE";
    case Ty::Str: return "STRING";
    default: return "UNKNOWN";
    }
}

Ty promote(Ty a, Ty b) {
    if (a == Ty::Str || b == Ty::Str) return Ty::Str;
    if (a == Ty::Dbl || b == Ty::Dbl) return Ty::Dbl;
    if (a == Ty::Sng || b == Ty::Sng) return Ty::Sng;
    if (a == Ty::Int && b == Ty::Int) return Ty::Int;
    return Ty::Unknown;
}

bool isCompare(Op op) { return op >= Op::Eq && op <= Op::Ge; }

Op invertCompare(Op op) {
    switch (op) {
    case Op::Eq: return Op::Ne;
    case Op::Ne: return Op::Eq;
    case Op::Lt: return Op::Ge;
    case Op::Le: return Op::Gt;
    case Op::Gt: return Op::Le;
    case Op::Ge: return Op::Lt;
    default: return op;
    }
}

Op swapCompare(Op op) {
    switch (op) {
    case Op::Lt: return Op::Gt;
    case Op::Le: return Op::Ge;
    case Op::Gt: return Op::Lt;
    case Op::Ge: return Op::Le;
    default: return op;
    }
}

const char* opText(Op op) {
    switch (op) {
    case Op::Add: return "+";
    case Op::Sub: return "-";
    case Op::Mul: return "*";
    case Op::Div: return "/";
    case Op::IDiv: return "\\";
    case Op::Mod: return "MOD";
    case Op::Pow: return "^";
    case Op::Eq: return "=";
    case Op::Ne: return "<>";
    case Op::Lt: return "<";
    case Op::Le: return "<=";
    case Op::Gt: return ">";
    case Op::Ge: return ">=";
    case Op::And: return "AND";
    case Op::Or: return "OR";
    case Op::Xor: return "XOR";
    case Op::Eqv: return "EQV";
    case Op::Imp: return "IMP";
    case Op::Neg: return "-";
    case Op::Not: return "NOT";
    }
    return "?";
}

namespace {
std::shared_ptr<Expr> node(EK k, Ty t) {
    auto e = std::make_shared<Expr>();
    e->kind = k;
    e->type = t;
    return e;
}
} // namespace

ExprPtr mkInt(int32_t v) {
    auto e = node(EK::IntLit, Ty::Int);
    e->ival = v;
    return e;
}
ExprPtr mkSng(double v) {
    auto e = node(EK::SngLit, Ty::Sng);
    e->dval = v;
    return e;
}
ExprPtr mkDbl(double v) {
    auto e = node(EK::DblLit, Ty::Dbl);
    e->dval = v;
    return e;
}
ExprPtr mkStr(const std::string& s) {
    auto e = node(EK::StrLit, Ty::Str);
    e->sval = s;
    return e;
}
ExprPtr mkVar(uint16_t addr, Ty t) {
    auto e = node(EK::Var, t);
    e->addr = addr;
    return e;
}
ExprPtr mkParam(int index, Ty t) {
    auto e = node(EK::Param, t);
    e->ival = index;
    return e;
}
ExprPtr mkArrElem(uint16_t base, Ty t, std::vector<ExprPtr> idx, uint8_t flags, int paramIndex) {
    auto e = node(EK::ArrElem, t);
    e->addr = base;
    e->args = std::move(idx);
    e->flags = flags;
    e->ival = paramIndex;
    return e;
}
ExprPtr mkUnary(Op op, ExprPtr a, Ty t) {
    auto e = node(EK::Unary, t);
    e->op = op;
    e->args = {std::move(a)};
    return e;
}
ExprPtr mkBinary(Op op, ExprPtr a, ExprPtr b, Ty t) {
    auto e = node(EK::Binary, t);
    e->op = op;
    e->args = {std::move(a), std::move(b)};
    return e;
}
ExprPtr mkFunc(const std::string& name, std::vector<ExprPtr> args, Ty t) {
    auto e = node(EK::Func, t);
    e->sval = name;
    e->args = std::move(args);
    return e;
}
ExprPtr mkFnCall(uint16_t addr, std::vector<ExprPtr> args, Ty t) {
    auto e = node(EK::FnCall, t);
    e->addr = addr;
    e->args = std::move(args);
    return e;
}
ExprPtr mkConv(ExprPtr a, Ty t) {
    if (a && a->type == t) return a;
    auto e = node(EK::Conv, t);
    e->args = {std::move(a)};
    return e;
}
ExprPtr mkPtrTo(ExprPtr a) {
    auto e = node(EK::PtrTo, Ty::Int);
    e->args = {std::move(a)};
    return e;
}
ExprPtr mkPhi(uint32_t block, int loc) {
    auto e = node(EK::Phi, Ty::Unknown);
    e->addr = uint16_t(block);
    e->ival = loc;
    return e;
}
ExprPtr mkUnknown(const std::string& why, Ty t) {
    auto e = node(EK::Unknown, t);
    e->sval = why;
    return e;
}
ExprPtr mkRaw(const std::string& text, Ty t) {
    auto e = node(EK::Raw, t);
    e->sval = text;
    return e;
}

bool exprEqual(const ExprPtr& a, const ExprPtr& b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind || a->type != b->type || a->op != b->op || a->addr != b->addr || a->ival != b->ival ||
        a->flags != b->flags || a->sval != b->sval || a->args.size() != b->args.size())
        return false;
    if ((a->kind == EK::SngLit || a->kind == EK::DblLit) && a->dval != b->dval) return false;
    for (size_t i = 0; i < a->args.size(); ++i)
        if (!exprEqual(a->args[i], b->args[i])) return false;
    return true;
}

bool isIntLit(const ExprPtr& e, int32_t* v) {
    if (!e || e->kind != EK::IntLit) return false;
    if (v) *v = e->ival;
    return true;
}

bool isUnknown(const ExprPtr& e) { return !e || e->kind == EK::Unknown; }

ExprPtr stripConv(const ExprPtr& e) {
    ExprPtr r = e;
    while (r && r->kind == EK::Conv) r = r->args[0];
    return r;
}

Stmt mkAssign(uint16_t addr, ExprPtr lhs, ExprPtr rhs) {
    Stmt s;
    s.kind = SK::Assign;
    s.addr = addr;
    s.lhs = std::move(lhs);
    s.rhs = std::move(rhs);
    return s;
}

Stmt mkTextStmt(uint16_t addr, std::vector<Part> parts) {
    Stmt s;
    s.kind = SK::Text;
    s.addr = addr;
    s.parts = std::move(parts);
    return s;
}

Stmt mkComment(uint16_t addr, const std::string& text) {
    Stmt s;
    s.kind = SK::Comment;
    s.addr = addr;
    s.text = text;
    return s;
}

Part P(const std::string& text) {
    Part p;
    p.text = text;
    return p;
}

Part P(ExprPtr e, Ty want) {
    Part p;
    p.expr = std::move(e);
    p.want = want;
    return p;
}

Part PK(ExprPtr e) {
    Part p;
    p.expr = std::move(e);
    p.keepType = true;
    return p;
}

} // namespace qb

namespace qb {

int exprWeight(const ExprPtr& e) {
    if (!e) return 0;
    int w = 1;
    for (const auto& a : e->args) w += exprWeight(a);
    return w;
}

} // namespace qb
