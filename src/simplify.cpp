#include "simplify.h"

namespace qb {

namespace {

int32_t wrap16(int64_t v) { return int16_t(uint16_t(v & 0xffff)); }

bool isLinearOp(const ExprPtr& e) {
    if (e->kind == EK::Unary && e->op == Op::Neg && e->type == Ty::Int) return true;
    if (e->kind != EK::Binary || e->type != Ty::Int) return false;
    if (e->op == Op::Add || e->op == Op::Sub) return true;
    if (e->op == Op::Mul) return isIntLit(e->args[0]) || isIntLit(e->args[1]);
    return false;
}

void linearize(const ExprPtr& e, int32_t scale, Linear& l) {
    int32_t v;
    if (isIntLit(e, &v)) {
        l.constant = wrap16(int64_t(l.constant) + int64_t(scale) * v);
        return;
    }
    if (!isLinearOp(e)) {
        l.add(e, scale);
        return;
    }
    if (e->kind == EK::Unary) {
        linearize(e->args[0], -scale, l);
        return;
    }
    switch (e->op) {
    case Op::Add:
        linearize(e->args[0], scale, l);
        linearize(e->args[1], scale, l);
        break;
    case Op::Sub:
        linearize(e->args[0], scale, l);
        linearize(e->args[1], -scale, l);
        break;
    case Op::Mul:
        if (isIntLit(e->args[0], &v)) linearize(e->args[1], wrap16(int64_t(scale) * v), l);
        else if (isIntLit(e->args[1], &v)) linearize(e->args[0], wrap16(int64_t(scale) * v), l);
        break;
    default:
        l.add(e, scale);
    }
}

ExprPtr term(const ExprPtr& atom, int32_t c) {
    if (c == 1) return atom;
    return mkBinary(Op::Mul, atom, mkInt(c), Ty::Int);
}

} // namespace

void Linear::add(const ExprPtr& atom, int32_t c) {
    for (auto& t : terms)
        if (exprEqual(t.first, atom)) {
            t.second = wrap16(int64_t(t.second) + c);
            return;
        }
    terms.emplace_back(atom, c);
}

Linear toLinear(const ExprPtr& e) {
    Linear l;
    linearize(e, 1, l);
    return l;
}

ExprPtr fromLinear(const Linear& l) {
    std::vector<std::pair<ExprPtr, int32_t>> ts;
    for (const auto& t : l.terms)
        if (t.second != 0) ts.push_back(t);
    // start with a positive term if there is one
    for (size_t i = 0; i < ts.size(); ++i)
        if (ts[i].second > 0) {
            if (i) std::rotate(ts.begin(), ts.begin() + long(i), ts.begin() + long(i) + 1);
            break;
        }
    ExprPtr r;
    for (const auto& t : ts) {
        if (!r) {
            r = t.second < 0 ? (t.second == -1 ? mkUnary(Op::Neg, t.first, Ty::Int) : term(t.first, t.second))
                             : term(t.first, t.second);
            continue;
        }
        if (t.second < 0) r = mkBinary(Op::Sub, r, term(t.first, -t.second), Ty::Int);
        else r = mkBinary(Op::Add, r, term(t.first, t.second), Ty::Int);
    }
    if (!r) return mkInt(l.constant);
    if (l.constant > 0) r = mkBinary(Op::Add, r, mkInt(l.constant), Ty::Int);
    else if (l.constant < 0 && l.constant != -32768) r = mkBinary(Op::Sub, r, mkInt(-l.constant), Ty::Int);
    else if (l.constant < 0) r = mkBinary(Op::Add, r, mkInt(l.constant), Ty::Int);
    return r;
}

ExprPtr simplifyInt(const ExprPtr& e) {
    if (!e || e->type != Ty::Int) return e;
    int32_t a, b;
    if (e->kind == EK::Binary) {
        bool ca = isIntLit(e->args[0], &a), cb = isIntLit(e->args[1], &b);
        if (ca && cb) {
            switch (e->op) {
            case Op::Add: return mkInt(wrap16(int64_t(a) + b));
            case Op::Sub: return mkInt(wrap16(int64_t(a) - b));
            case Op::Mul: return mkInt(wrap16(int64_t(a) * b));
            case Op::And: return mkInt(a & b);
            case Op::Or: return mkInt(a | b);
            case Op::Xor: return mkInt(a ^ b);
            case Op::IDiv: if (b) return mkInt(wrap16(a / b)); break;
            case Op::Mod: if (b) return mkInt(wrap16(a % b)); break;
            default: break;
            }
        }
        if (isLinearOp(e)) return fromLinear(toLinear(e));
        return e;
    }
    if (e->kind == EK::Unary && isIntLit(e->args[0], &a)) {
        if (e->op == Op::Neg) return mkInt(wrap16(-int64_t(a)));
        if (e->op == Op::Not) return mkInt(~a);
    }
    if (e->kind == EK::Unary && e->op == Op::Neg) return fromLinear(toLinear(e));
    return e;
}

bool isBoolean(const ExprPtr& e) {
    if (!e) return false;
    if (e->kind == EK::Binary && isCompare(e->op)) return true;
    if (e->kind == EK::Binary && (e->op == Op::And || e->op == Op::Or || e->op == Op::Xor || e->op == Op::Eqv ||
                                  e->op == Op::Imp))
        return isBoolean(e->args[0]) && isBoolean(e->args[1]);
    if (e->kind == EK::Unary && e->op == Op::Not) return isBoolean(e->args[0]);
    return false;
}

ExprPtr simplifyCond(const ExprPtr& e) {
    if (!e || e->kind != EK::Binary || !isCompare(e->op)) return e;
    int32_t z;
    if (isIntLit(e->args[1], &z) && z == 0 && isBoolean(e->args[0])) {
        if (e->op == Op::Ne) return e->args[0];
        if (e->op == Op::Eq) return invertCond(e->args[0]);
    }
    return e;
}

ExprPtr invertCond(const ExprPtr& e) {
    if (!e) return e;
    if (e->kind == EK::Binary && isCompare(e->op)) {
        int32_t z;
        // "x <> 0" with a boolean x was already reduced to x
        if (isIntLit(e->args[1], &z) && z == 0 && isBoolean(e->args[0])) {
            if (e->op == Op::Ne) return invertCond(e->args[0]);
            if (e->op == Op::Eq) return e->args[0];
        }
        return mkBinary(invertCompare(e->op), e->args[0], e->args[1], e->type);
    }
    if (e->kind == EK::Unary && e->op == Op::Not && isBoolean(e->args[0])) return e->args[0];
    if (isBoolean(e)) return mkUnary(Op::Not, e, Ty::Int);
    return mkBinary(Op::Eq, e, mkInt(0), Ty::Int);
}

ExprPtr simplifyExpr(const ExprPtr& e) {
    return rewriteExpr(e, [](const ExprPtr& n) -> ExprPtr {
        if (n->type == Ty::Int && (n->kind == EK::Binary || n->kind == EK::Unary)) return simplifyInt(n);
        return n;
    });
}

} // namespace qb
