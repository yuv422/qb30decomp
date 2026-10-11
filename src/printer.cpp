#include "printer.h"

#include "mbf.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace qb {

namespace {

int precOf(const ExprPtr& e) {
    if (e->kind == EK::Binary) {
        switch (e->op) {
        case Op::Pow: return 13;
        case Op::Mul: case Op::Div: return 11;
        case Op::IDiv: return 10;
        case Op::Mod: return 9;
        case Op::Add: case Op::Sub: return 8;
        case Op::Eq: case Op::Ne: case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: return 7;
        case Op::And: return 5;
        case Op::Or: return 4;
        case Op::Xor: return 3;
        case Op::Eqv: return 2;
        case Op::Imp: return 1;
        default: return 0;
        }
    }
    if (e->kind == EK::Unary) return e->op == Op::Neg ? 12 : 6;
    if ((e->kind == EK::IntLit && e->ival < 0) ||
        ((e->kind == EK::SngLit || e->kind == EK::DblLit) && std::signbit(e->dval)))
        return 12; // a negative literal behaves like unary minus
    return 100;
}

// Natural result type of a binary operation in BASIC.
Ty naturalType(Op op, Ty a, Ty b) {
    switch (op) {
    case Op::Div:
    case Op::Pow:
        if (a == Ty::Dbl || b == Ty::Dbl) return Ty::Dbl;
        return Ty::Sng;
    case Op::IDiv: case Op::Mod: case Op::And: case Op::Or: case Op::Xor: case Op::Eqv: case Op::Imp:
        return Ty::Int;
    default:
        return promote(a, b);
    }
}

Ty baseType(const ExprPtr& e) {
    ExprPtr s = stripConv(e);
    return s ? s->type : Ty::Unknown;
}

const char* convName(Ty t) {
    switch (t) {
    case Ty::Int: return "CINT";
    case Ty::Sng: return "CSNG";
    case Ty::Dbl: return "CDBL";
    default: return "";
    }
}

// Argument types expected by builtin functions (Int means the argument is
// rounded to an integer by the function itself).
Ty funcArgType(const std::string& name, size_t i, size_t nargs) {
    static const char* intFuncs[] = {"CHR$", "SPACE$", "LPOS", "POS", "PEN", "STICK", "STRIG", "EOF", "LOC", "LOF",
                                     "PLAY", "IOCTL$", "INPUT$", "SCREEN", "MKI$", "HEX$", "OCT$", "STR$", "ERR",
                                     "CSRLIN", "ENVIRON$", "INP"};
    for (const char* f : intFuncs)
        if (name == f) return Ty::Int;
    if (name == "LEFT$" || name == "RIGHT$") return i == 1 ? Ty::Int : Ty::Str;
    if (name == "MID$") return i >= 1 ? Ty::Int : Ty::Str;
    if (name == "STRING$") return i == 0 ? Ty::Int : Ty::Unknown;
    if (name == "INSTR") return (nargs == 3 && i == 0) ? Ty::Int : Ty::Str;
    if (name == "POINT") return nargs == 1 ? Ty::Int : Ty::Unknown;
    if (name == "PMAP") return i == 1 ? Ty::Int : Ty::Sng;
    return Ty::Unknown;
}

bool isMathFunc(const std::string& n) {
    static const char* f[] = {"ATN", "COS", "EXP", "LOG", "SIN", "SQR", "TAN", "FIX", "INT", "ABS", "SGN", "STR$",
                              "MKS$", "MKD$", "HEX$", "OCT$"};
    for (const char* x : f)
        if (n == x) return true;
    return false;
}

} // namespace

std::string Printer::quote(const std::string& s) {
    std::string out;
    bool inStr = false;
    auto closeStr = [&]() {
        if (inStr) {
            out += "\"";
            inStr = false;
        }
    };
    for (unsigned char c : s) {
        if (c == '"' || c < 32 || c == 127) {
            closeStr();
            if (!out.empty()) out += " + ";
            out += "CHR$(" + std::to_string(int(c)) + ")";
        } else {
            if (!inStr) {
                if (!out.empty()) out += " + ";
                out += "\"";
                inStr = true;
            }
            out += char(c);
        }
    }
    closeStr();
    if (out.empty()) out = "\"\"";
    return out;
}

std::string Printer::number(const ExprPtr& lit) {
    switch (lit->kind) {
    case EK::IntLit: return std::to_string(lit->ival);
    case EK::SngLit: return formatSingle(lit->dval);
    case EK::DblLit: return formatDouble(lit->dval);
    default: return "?";
    }
}

std::string Printer::expr(const ExprPtr& e, Ty want) const { return expr(e, 0, false, want); }

std::string Printer::cond(const ExprPtr& e) const {
    if (e && e->kind == EK::Binary && e->op == Op::Ne && e->flags == 1) return expr(e->args[0], 0, false, Ty::Unknown);
    if (e && e->kind == EK::Binary && e->op == Op::Ne) {
        int32_t z;
        // "and bx,bx / jnz" on an integer: IF x THEN
        if (isIntLit(e->args[1], &z) && z == 0 && e->args[0]->type == Ty::Int && e->flags == 2)
            return expr(e->args[0], 0, false, Ty::Unknown);
    }
    return expr(e, 0, false, Ty::Unknown);
}

namespace {

bool isNegLit(const ExprPtr& e) {
    ExprPtr s = stripConv(e);
    return s && ((s->kind == EK::IntLit && s->ival < 0 && s->ival != -32768) ||
                 ((s->kind == EK::SngLit || s->kind == EK::DblLit) && s->dval < 0));
}

// Is e printed as an addition ("x + 5", or "x - -5" printed as "x + 5")?
bool printsAsPlus(const ExprPtr& e) {
    if (!e || e->kind != EK::Binary) return false;
    if (e->op == Op::Add) return !isNegLit(e->args[1]);
    if (e->op == Op::Sub) return isNegLit(e->args[1]);
    return false;
}

} // namespace

std::string Printer::expr(const ExprPtr& e, int parentPrec, bool rightSide, Ty want) const {
    if (!e) return "???";
    int prec = precOf(e);
    auto wrap = [&](const std::string& s) {
        bool need = prec < parentPrec || (rightSide && prec == parentPrec && prec != 100);
        return need ? "(" + s + ")" : s;
    };
    switch (e->kind) {
    case EK::IntLit:
        return wrap(std::to_string(e->ival));
    case EK::SngLit: {
        std::string s = formatSingle(e->dval);
        bool integral = s.find_first_of(".E!") == std::string::npos;
        if (integral && want != Ty::Sng) s += "!";
        return wrap(s);
    }
    case EK::DblLit:
        return wrap(formatDouble(e->dval));
    case EK::StrLit: {
        std::string q = quote(e->sval);
        if (q.find(" + ") != std::string::npos && parentPrec > 8) return "(" + q + ")";
        return q;
    }
    case EK::Var:
        return names_.var(e->addr, e->type);
    case EK::Param:
        return names_.param(e->ival, e->type);
    case EK::ArrElem: {
        std::string name = (e->flags & AF_Param) ? names_.param(e->ival, e->type) : names_.array(e->addr, e->type);
        std::string s = name + "(";
        if (e->flags & AF_ByteOffset) return name + "(???)";
        for (size_t i = 0; i < e->args.size(); ++i) {
            if (i) s += ", ";
            size_t k = names_.rowOrder ? e->args.size() - 1 - i : i;
            s += expr(e->args[k], 0, false, Ty::Int);
        }
        return s + ")";
    }
    case EK::Unary: {
        const ExprPtr& a = e->args[0];
        if (e->op == Op::Neg) {
            std::string inner = expr(a, prec, true, e->type);
            return wrap("-" + inner);
        }
        return wrap("NOT " + expr(a, prec, true, Ty::Int));
    }
    case EK::Binary: {
        const ExprPtr& l = e->args[0];
        const ExprPtr& r = e->args[1];
        Ty lt = baseType(l), rt = baseType(r);
        ExprPtr pl = l, pr = r;
        if (isCompare(e->op)) {
            // comparisons are done in the larger of the operand types
            Ty t = promote(l->type, r->type);
            Ty nat = promote(lt, rt);
            if (nat == t || t == Ty::Unknown) {
                pl = stripConv(l);
                pr = stripConv(r);
            } else {
                pr = stripConv(r);
            }
            std::string ls = expr(pl, prec + 1, false, pl == l ? Ty::Unknown : pl->type);
            std::string rs = expr(pr, prec + 1, true, pr->type == Ty::Sng ? Ty::Sng : Ty::Unknown);
            if (pl == l && l->kind == EK::Conv) ls = expr(l, prec + 1, false, Ty::Unknown);
            return wrap(ls + " " + opText(e->op) + " " + rs);
        }
        Ty t = e->type;
        if (e->op == Op::IDiv || e->op == Op::Mod || e->op == Op::And || e->op == Op::Or || e->op == Op::Xor ||
            e->op == Op::Eqv || e->op == Op::Imp) {
            // operands are rounded to integers by the operator itself
            pl = stripConv(l);
            pr = stripConv(r);
            if (e->op != Op::IDiv && e->op != Op::Mod) {
                // keep the operand types of logical operators on booleans
            }
            Ty wl = Ty::Int, wr = Ty::Int;
            if (e->op == Op::IDiv || e->op == Op::Mod) wl = wr = Ty::Unknown;
            std::string ls = expr(pl, prec, false, wl);
            std::string rs = expr(pr, prec, true, wr);
            return wrap(ls + " " + opText(e->op) + " " + rs);
        }
        // x + -5 is printed as x - 5
        Op op = e->op;
        {
            ExprPtr sr = stripConv(r);
            bool negLit = sr && ((sr->kind == EK::IntLit && sr->ival < 0 && sr->ival != -32768) ||
                                 ((sr->kind == EK::SngLit || sr->kind == EK::DblLit) && sr->dval < 0));
            if (negLit && (op == Op::Add || op == Op::Sub)) {
                auto n = std::make_shared<Expr>(*sr);
                if (n->kind == EK::IntLit) n->ival = -n->ival;
                else n->dval = -n->dval;
                op = op == Op::Add ? Op::Sub : Op::Add;
                ExprPtr nr = n;
                if (r->kind == EK::Conv) nr = mkConv(n, r->type);
                ExprPtr ne = mkBinary(op, l, nr, e->type);
                return expr(ne, parentPrec, rightSide, want);
            }
        }
        // QB computes "a / b / c" as "a / (b * c)" (and reorders a product written
        // as the divisor): write such a division as successive divisions
        if (op == Op::Div) {
            ExprPtr sl = stripConv(l), sr = stripConv(r);
            bool chainLeft = sl && sl->kind == EK::Binary && (sl->op == Op::Mul || sl->op == Op::Div);
            if (sr && sr->kind == EK::Binary && sr->op == Op::Mul && sr->type == e->type && !chainLeft) {
                std::vector<ExprPtr> factors;
                ExprPtr f = sr;
                while (f->kind == EK::Binary && f->op == Op::Mul && f->type == e->type) {
                    factors.push_back(f->args[1]);
                    f = f->args[0];
                }
                factors.push_back(f);
                std::reverse(factors.begin(), factors.end());
                ExprPtr ne = l;
                for (const ExprPtr& x : factors) ne = mkBinary(Op::Div, ne, x, e->type);
                return expr(ne, parentPrec, rightSide, want);
            }
        }
        Ty nat = naturalType(e->op, lt, rt);
        bool keepLeft = false;
        if (nat == t || t == Ty::Unknown) {
            pl = stripConv(l);
            pr = stripConv(r);
        } else {
            // the operation is done in a wider type than the operands imply
            pr = stripConv(r);
            if (naturalType(e->op, l->type, baseType(r)) != t) pr = r;
            pl = l;
            keepLeft = l->kind == EK::Conv;
        }
        std::string ls = keepLeft ? std::string(convName(l->type)) + "(" + expr(l->args[0], 0, false, Ty::Unknown) + ")"
                                  : expr(pl, prec, false, t);
        std::string rs = expr(pr, prec, true, t);
        // a negative number on the right of an operator needs parentheses
        if (!rs.empty() && rs[0] == '-') rs = "(" + rs + ")";
        // QB moves a subtraction in front of the addition before it ("a + b - c"
        // is computed as "a - c + b"), but not past parentheses: keep the order
        // the code computes in
        if (e->op == Op::Sub && printsAsPlus(stripConv(l))) ls = "(" + ls + ")";
        return wrap(ls + " " + opText(e->op) + " " + rs);
    }
    case EK::Func: {
        const std::string& n = e->sval;
        if (n.rfind("@index", 0) == 0) return "???";
        std::string s = n;
        if (e->args.empty()) return s;
        s += "(";
        for (size_t i = 0; i < e->args.size(); ++i) {
            const ExprPtr& a = e->args[i];
            if (a->kind == EK::Raw && a->sval == "#") {
                // "#" marks a file number argument: INPUT$(n, #f), VARPTR(#f)
                if (i) s += ", ";
                s += "#";
                ++i;
                if (i < e->args.size()) s += expr(e->args[i], 0, false, Ty::Int);
                continue;
            }
            if (i) s += ", ";
            Ty wantArg = funcArgType(n, i, e->args.size());
            // functions without type specific versions convert their arguments themselves
            if (wantArg == Ty::Unknown && !isMathFunc(n) && a->kind != EK::Conv) wantArg = a->type;
            ExprPtr pa = a;
            if (wantArg == Ty::Int) pa = stripConv(a);
            else if (isMathFunc(n) && a->kind == EK::Conv) {
                // SIN(i%) uses the single precision version: the int->single conversion is implied
                Ty inner = baseType(a);
                if (a->type == Ty::Sng && inner == Ty::Int) pa = stripConv(a);
            }
            s += expr(pa, 0, false, wantArg);
        }
        return s + ")";
    }
    case EK::FnCall: {
        std::string s = names_.fn(e->addr);
        if (e->args.empty()) return s;
        s += "(";
        for (size_t i = 0; i < e->args.size(); ++i) {
            if (i) s += ", ";
            Ty pt = names_.fnParamType ? names_.fnParamType(e->addr, i) : Ty::Unknown;
            s += expr(e->args[i], 0, false, pt);
        }
        return s + ")";
    }
    case EK::Conv: {
        const ExprPtr& a = e->args[0];
        if (want == e->type || (e->flags == 1 && want == Ty::Int)) return expr(a, parentPrec, rightSide, a->type);
        // VARPTR and friends already return a number
        if (e->type == Ty::Int && want == Ty::Unknown && a->kind == EK::Func && a->sval == "VARPTR")
            return expr(a, parentPrec, rightSide, Ty::Unknown);
        return std::string(convName(e->type)) + "(" + expr(a, 0, false, Ty::Unknown) + ")";
    }
    case EK::PtrTo:
        return expr(e->args[0], parentPrec, rightSide, want);
    case EK::Raw:
        if (e->sval == "@array") return names_.array(e->addr, e->type);
        if (e->sval == "@arrayarg") return names_.array(e->addr, e->type) + "()";
        if (e->sval == "@paramarray") return names_.param(int(e->ival), e->type) + "()";
        if (e->sval == "@paramname") return names_.param(int(e->ival), e->type);
        return e->sval;
    case EK::Unknown:
        if (std::getenv("QBDEBUG")) return "???<" + e->sval + ">";
        return "???";
    case EK::Phi:
    default:
        return "???";
    }
}

std::string Printer::stmt(const Stmt& s) const {
    switch (s.kind) {
    case SK::Assign: {
        Ty t = s.lhs ? s.lhs->type : Ty::Unknown;
        std::string rhs = expr(s.rhs, 0, false, t);
        if (s.rhs && s.rhs->kind == EK::Binary && isCompare(s.rhs->op)) rhs = "(" + rhs + ")";
        return expr(s.lhs, Ty::Unknown) + " = " + rhs;
    }
    case SK::Text:
    case SK::End: {
        std::string r;
        for (const Part& p : s.parts) {
            if (p.expr) r += expr(p.expr, 0, false, (p.want == Ty::Unknown && !p.keepType) ? p.expr->type : p.want);
            else r += p.text;
        }
        if (s.hasTarget) r += names_.label(s.target);
        for (size_t i = 0; i < s.targets.size(); ++i) {
            if (i) r += ", ";
            r += names_.label(s.targets[i]);
        }
        return r;
    }
    case SK::Goto: return "GOTO " + names_.label(s.target);
    case SK::Gosub: return "GOSUB " + names_.label(s.target);
    case SK::Return: return s.hasTarget ? "RETURN " + names_.label(s.target) : "RETURN";
    case SK::Label: return names_.label(s.target) + ":";
    case SK::Comment: return "' " + s.text;
    }
    return "";
}

} // namespace qb
