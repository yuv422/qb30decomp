#include "postprocess.h"

#include "simplify.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace qb {

namespace {

int tySize(Ty t) {
    switch (t) {
    case Ty::Int: return 2;
    case Ty::Dbl: return 8;
    case Ty::Sng: case Ty::Str: return 4;
    default: return 0;
    }
}

int32_t floorDiv(int32_t a, int32_t b) {
    int32_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

struct StaticEvidence {
    Ty type = Ty::Unknown;
    std::vector<Linear> offsets;   // byte offsets
    std::vector<int> debugDims;    // dimensions from /D bounds checking layout
    bool fromRef = false;
};

// Dimension sizes (in elements) for a static array from its accesses.
std::vector<int> inferDims(const std::vector<Linear>& offs, int size, int totalElems) {
    std::vector<int> mults;
    for (const Linear& l : offs) {
        if (l.terms.size() < 2) continue;
        bool hasOne = false;
        int best = 0;
        for (const auto& t : l.terms) {
            int c = std::abs(t.second) / size;
            if (t.second % size) continue;
            if (c == 1) hasOne = true;
            else if (c > 1 && (best == 0 || c < best)) best = c;
        }
        if (hasOne && best) mults.push_back(best);
    }
    std::vector<int> dims;
    if (mults.empty()) {
        // weaker evidence: A(2, j) gives a single term j * d1 plus a constant below d1
        std::set<int> weak;
        for (const Linear& l : offs) {
            if (l.terms.size() != 1 || l.terms[0].second % size) continue;
            int c = std::abs(l.terms[0].second) / size;
            int k = std::abs(l.constant) / size;
            if (c > 1 && k > 0 && k < c && l.constant % size == 0) weak.insert(c);
        }
        if (weak.size() == 1 && *weak.begin() < totalElems) mults.push_back(*weak.begin());
    }
    if (mults.empty()) {
        dims.push_back(std::max(totalElems, 1));
        return dims;
    }
    int d1 = *std::min_element(mults.begin(), mults.end());
    dims.push_back(d1);
    // a third dimension: coefficients that are multiples of d1 again
    std::vector<int> mults2;
    for (const Linear& l : offs) {
        bool hasD1 = false;
        int best = 0;
        for (const auto& t : l.terms) {
            if (t.second % size) continue;
            int c = std::abs(t.second) / size;
            if (c % d1) continue;
            int c2 = c / d1;
            if (c2 == 1) hasD1 = true;
            else if (c2 > 1 && (best == 0 || c2 < best)) best = c2;
        }
        if (hasD1 && best) mults2.push_back(best);
    }
    int prod = d1;
    if (!mults2.empty()) {
        int d2 = *std::min_element(mults2.begin(), mults2.end());
        dims.push_back(d2);
        prod *= d2;
    }
    int rest = totalElems > 0 ? (totalElems + prod - 1) / prod : 1;
    dims.push_back(std::max(rest, 1));
    return dims;
}

// Splits a byte offset into indices for the given dimensions.
std::vector<ExprPtr> decompose(const Linear& byteOff, int size, const std::vector<int>& dims) {
    Linear l;
    bool exact = true;
    for (const auto& t : byteOff.terms) {
        if (t.second % size) exact = false;
        l.add(t.first, t.second / size);
    }
    if (byteOff.constant % size) exact = false;
    l.constant = byteOff.constant / size;
    if (!exact) {
        // not a multiple of the element size: keep the raw offset arithmetic
        ExprPtr e = mkBinary(Op::IDiv, fromLinear(byteOff), mkInt(size), Ty::Int);
        return {e};
    }
    std::vector<ExprPtr> idx;
    Linear cur = l;
    for (size_t d = 0; d + 1 < dims.size(); ++d) {
        int dn = dims[d];
        Linear here, rest;
        for (const auto& t : cur.terms) {
            if (t.second % dn == 0) rest.add(t.first, t.second / dn);
            else here.add(t.first, t.second);
        }
        int32_t c = cur.constant;
        int32_t q = floorDiv(c, dn);
        here.constant = c - q * dn;
        rest.constant = q;
        idx.push_back(fromLinear(here));
        cur = rest;
    }
    idx.push_back(fromLinear(cur));
    return idx;
}

bool isDescWord(uint16_t addr, uint16_t desc, int dims) {
    return addr >= uint16_t(desc + 8) && addr < uint16_t(desc + 8 + 2 * std::max(dims, 1));
}

// Indices of a dynamic array element from its byte offset. Multi dimensional
// offsets use the dimension sizes stored in the descriptor.
std::vector<ExprPtr> decomposeDyn(const ExprPtr& byteOff, int size, int dims,
                                  const std::function<bool(const ExprPtr&)>& isDim) {
    Linear l0 = toLinear(byteOff);
    Linear l;
    for (const auto& t : l0.terms) l.add(t.first, size ? t.second / size : t.second);
    l.constant = size ? l0.constant / size : l0.constant;
    std::vector<ExprPtr> idx;
    Linear cur = l;
    for (int d = 0; d < dims; ++d) {
        if (d == dims - 1) {
            idx.push_back(fromLinear(cur));
            break;
        }
        Linear here, rest;
        for (const auto& t : cur.terms) {
            const ExprPtr& a = t.first;
            if (isDim(a)) {
                rest.constant += t.second;
                continue;
            }
            if (a->kind == EK::Binary && a->op == Op::Mul) {
                const ExprPtr& x = a->args[0];
                const ExprPtr& y = a->args[1];
                if (isDim(y)) {
                    Linear inner = toLinear(x);
                    for (const auto& it : inner.terms) rest.add(it.first, it.second * t.second);
                    rest.constant += inner.constant * t.second;
                    continue;
                }
                if (isDim(x)) {
                    Linear inner = toLinear(y);
                    for (const auto& it : inner.terms) rest.add(it.first, it.second * t.second);
                    rest.constant += inner.constant * t.second;
                    continue;
                }
            }
            here.add(t.first, t.second);
        }
        here.constant = cur.constant;
        idx.push_back(fromLinear(here));
        cur = rest;
    }
    return idx;
}

} // namespace

void forEachExprSlot(std::vector<std::vector<LiftedBlock>>& results, GlobalInfo& info,
                     const std::function<void(ExprPtr&, uint32_t)>& f) {
    for (uint32_t pi = 0; pi < results.size(); ++pi) {
        for (LiftedBlock& lb : results[pi]) {
            for (Stmt& s : lb.stmts) {
                if (s.lhs) f(s.lhs, pi);
                if (s.rhs) f(s.rhs, pi);
                for (Part& p : s.parts)
                    if (p.expr) f(p.expr, pi);
            }
            if (lb.cond) f(lb.cond, pi);
            if (lb.selector) f(lb.selector, pi);
        }
    }
    for (auto& kv : info.fns)
        if (kv.second.body) f(kv.second.body, UINT32_MAX);
}

std::map<uint16_t, ArrayInfo> computeArrays(const LoadedProgram& prog, std::vector<std::vector<LiftedBlock>>& results,
                                            GlobalInfo& info, std::map<uint16_t, std::set<Ty>>& varTypes,
                                            std::map<uint16_t, std::pair<uint16_t, int>>& aliases) {
    struct { std::map<uint16_t, ArrayInfo> arrays; } pinfo;
    // 1. evidence
    std::map<uint16_t, StaticEvidence> stat;
    forEachExprSlot(results, info, [&](ExprPtr& e, uint32_t) {
        walkExpr(e, [&](const ExprPtr& n) {
            if (n->kind == EK::ArrElem && !(n->flags & (AF_Dynamic | AF_Param))) {
                StaticEvidence& ev = stat[n->addr];
                if (n->type != Ty::Unknown) ev.type = n->type;
                if (n->flags & AF_ByteOffset) {
                    const ExprPtr& off = n->args[0];
                    if (off->kind == EK::Func && off->sval.rfind("@index", 0) == 0) {
                        // "/D" bounds checked access: dims are in the name "@index:d1,d2"
                        ev.debugDims.clear();
                        size_t c = off->sval.find(':');
                        if (c != std::string::npos) {
                            std::string ds = off->sval.substr(c + 1);
                            size_t p = 0;
                            while (p < ds.size()) {
                                ev.debugDims.push_back(std::atoi(ds.c_str() + p));
                                size_t q = ds.find(',', p);
                                if (q == std::string::npos) break;
                                p = q + 1;
                            }
                        }
                    } else {
                        ev.offsets.push_back(toLinear(off));
                    }
                }
            } else if (n->kind == EK::Raw && (n->sval == "@array" || n->sval == "@arrayarg") && !(n->flags & AF_Dynamic)) {
                StaticEvidence& ev = stat[n->addr];
                ev.fromRef = true;
                if (n->type != Ty::Unknown) ev.type = n->type;
            } else if (n->kind == EK::Var) {
                if (n->type != Ty::Unknown) varTypes[n->addr].insert(n->type);
                else varTypes[n->addr];
            }
        });
    });
    for (const auto& kv : info.staticArrays) {
        stat[kv.first].type = kv.second;
        stat[kv.first].fromRef = true;
    }
    for (const auto& kv : info.arrayBytes) stat[kv.first].fromRef = true;
    // pass 1 var types (from all uses)
    for (const auto& kv : info.varTypes)
        for (Ty t : kv.second) varTypes[kv.first].insert(t);

    // Accesses like A(2, j) use the address of A(2, 0) as displacement. Such
    // "bases" that fall inside an earlier array of the same type (with no
    // explicit reference of their own) belong to that array.
    {
        std::vector<uint16_t> bs;
        for (const auto& kv : stat) bs.push_back(kv.first);
        std::sort(bs.begin(), bs.end());
        std::vector<uint16_t> explicitBases;
        for (uint16_t b : bs)
            if (stat[b].fromRef) explicitBases.push_back(b);
        auto extentOf = [&](uint16_t b, Ty t) -> uint32_t {
            int size = tySize(t);
            auto ab = info.arrayBytes.find(b);
            if (ab != info.arrayBytes.end()) return ab->second;
            uint32_t end = prog.dsEnd > b ? prog.dsEnd : uint32_t(b) + uint32_t(size);
            for (uint16_t e : explicitBases)
                if (e > b) { end = std::min<uint32_t>(end, e); break; }
            for (const auto& vt : varTypes) {
                if (vt.first <= b || vt.first >= end) continue;
                bool sameType = vt.second.count(t) && vt.second.size() == 1;
                if (!sameType || (vt.first - b) % size) { end = vt.first; break; }
            }
            for (const auto& dk : info.dynArrays)
                if (dk.first > b && dk.first < end) end = dk.first;
            for (uint32_t a = b; a < end; ++a)
                if (prog.dsInit[a]) { end = a; break; }
            return end > b ? end - b : uint32_t(size);
        };
        for (size_t i = 0; i < bs.size(); ++i) {
            uint16_t a = bs[i];
            if (!stat.count(a) || stat[a].type == Ty::Unknown) continue;
            uint32_t ext = extentOf(a, stat[a].type);
            for (size_t j = i + 1; j < bs.size(); ++j) {
                uint16_t b = bs[j];
                if (b >= a + ext) break;
                auto it = stat.find(b);
                if (it == stat.end() || it->second.fromRef || it->second.type != stat[a].type) continue;
                if ((b - a) % tySize(stat[a].type)) continue;
                int delta = b - a;
                for (Linear l : it->second.offsets) {
                    l.constant += delta;
                    stat[a].offsets.push_back(l);
                }
                aliases[b] = {a, delta};
                stat.erase(it);
            }
        }
    }

    // 2. static array extents and dimensions
    std::vector<uint16_t> bases;
    for (const auto& kv : stat) bases.push_back(kv.first);
    std::sort(bases.begin(), bases.end());
    for (size_t bi = 0; bi < bases.size(); ++bi) {
        uint16_t b = bases[bi];
        StaticEvidence& ev = stat[b];
        ArrayInfo ai;
        ai.base = b;
        ai.type = ev.type;
        if (ai.type == Ty::Unknown) {
            // e.g. only used by GET/PUT: take the type of elements accessed directly
            auto vt = varTypes.find(b);
            if (vt != varTypes.end() && !vt->second.empty()) ai.type = *vt->second.begin();
            else ai.type = Ty::Int;
        }
        int size = tySize(ai.type);
        uint32_t bytes = 0;
        auto ab = info.arrayBytes.find(b);
        if (ab != info.arrayBytes.end()) bytes = ab->second;
        if (!bytes) {
            uint32_t end = prog.dsEnd > b ? prog.dsEnd : uint32_t(b) + uint32_t(size);
            if (bi + 1 < bases.size()) end = std::min<uint32_t>(end, bases[bi + 1]);
            for (const auto& vt : varTypes) {
                if (vt.first <= b || vt.first >= end) continue;
                bool sameType = vt.second.count(ai.type) && vt.second.size() == 1;
                if (!sameType || (vt.first - b) % size) {
                    end = vt.first;
                    break;
                }
            }
            for (const auto& dk : info.dynArrays)
                if (dk.first > b && dk.first < end) end = dk.first;
            for (uint32_t a = b; a < end; ++a)
                if (prog.dsInit[a]) {
                    end = a;
                    break;
                }
            bytes = end > b ? end - b : uint32_t(size);
        }
        ai.bytes = bytes;
        int total = int(bytes / uint32_t(size));
        if (!ev.debugDims.empty()) ai.dims = ev.debugDims;
        else ai.dims = inferDims(ev.offsets, size, total);
        pinfo.arrays[b] = ai;
    }
    for (const auto& kv : info.dynArrays) {
        ArrayInfo ai;
        ai.base = kv.first;
        ai.type = kv.second;
        ai.dynamic = true;
        ai.dynDims = info.dynDims.count(kv.first) ? info.dynDims[kv.first] : 1;
        pinfo.arrays[kv.first] = ai;
    }
    return pinfo.arrays;
}

ProgramInfo postprocess(Program& program, std::vector<std::vector<LiftedBlock>>& results, GlobalInfo& info,
                        std::vector<std::string>& warnings) {
    const LoadedProgram& prog = program.prog();
    ProgramInfo pinfo;

    // procs of DEF FN bodies (for reference bookkeeping)
    std::map<uint16_t, uint32_t> procOfFn;
    for (uint32_t i = 0; i < program.procs().size(); ++i) procOfFn[program.procs()[i].entry] = i;

    std::map<uint16_t, std::set<Ty>> varTypes;
    pinfo.arrays = computeArrays(prog, results, info, varTypes, pinfo.arrayAliases);
    auto staticArrayAt = [&](uint16_t addr, Ty t) -> const ArrayInfo* {
        auto it = pinfo.arrays.upper_bound(addr);
        if (it == pinfo.arrays.begin()) return nullptr;
        --it;
        const ArrayInfo& ai = it->second;
        if (ai.dynamic) return nullptr;
        if (addr < ai.base || addr >= ai.base + ai.bytes) return nullptr;
        if (t != Ty::Unknown && t != ai.type) return nullptr;
        if ((addr - ai.base) % tySize(ai.type)) return nullptr;
        return &ai;
    };

    // 3. rewrite element accesses and resolve variable types
    auto resolveVarType = [&](uint16_t a) {
        auto it = varTypes.find(a);
        if (it != varTypes.end())
            for (Ty t : it->second)
                if (t != Ty::Unknown) return t;
        return Ty::Sng;
    };
    forEachExprSlot(results, info, [&](ExprPtr& e, uint32_t pi) {
        e = rewriteExpr(e, [&](const ExprPtr& n) -> ExprPtr {
            if (n->kind == EK::ArrElem && (n->flags & AF_ByteOffset)) {
                if (!(n->flags & (AF_Dynamic | AF_Param))) {
                    uint16_t base = n->addr;
                    int delta = 0;
                    auto al = pinfo.arrayAliases.find(base);
                    if (al != pinfo.arrayAliases.end()) {
                        base = al->second.first;
                        delta = al->second.second;
                    }
                    auto it = pinfo.arrays.find(base);
                    if (it == pinfo.arrays.end()) return n;
                    const ArrayInfo& ai = it->second;
                    const ExprPtr& off = n->args[0];
                    std::vector<ExprPtr> idx;
                    if (off->kind == EK::Func && off->sval.rfind("@index", 0) == 0) {
                        idx = off->args;
                    } else {
                        Linear l = toLinear(off);
                        l.constant += delta;
                        idx = decompose(l, tySize(ai.type), ai.dims);
                    }
                    for (auto& x : idx) x = simplifyExpr(x);
                    return mkArrElem(base, ai.type, idx, 0);
                }
                if (n->flags & AF_Param) {
                    int size = tySize(n->type);
                    std::vector<ExprPtr> idx;
                    const ExprPtr& off = n->args[0];
                    if (off->kind == EK::Func && off->sval.rfind("@index", 0) == 0) {
                        idx = off->args;
                    } else {
                        // dimension sizes come from the descriptor on the stack ("@pdim")
                        int dims = 1;
                        auto sit = info.subs.find(program.procs()[pi].entry);
                        if (sit != info.subs.end() && n->ival >= 0 && n->ival < int(sit->second.params.size()))
                            dims = std::max(1, sit->second.params[size_t(n->ival)].dims);
                        int k = n->ival;
                        idx = decomposeDyn(off, size ? size : 2, dims, [k](const ExprPtr& a) {
                            return a->kind == EK::Raw && a->sval == "@pdim" && a->ival == k;
                        });
                    }
                    for (auto& x : idx) x = simplifyExpr(x);
                    return mkArrElem(0, n->type, idx, AF_Param | AF_Dynamic, n->ival);
                }
                // dynamic
                Ty t = n->type;
                auto it = pinfo.arrays.find(n->addr);
                int dims = 1;
                if (it != pinfo.arrays.end()) {
                    if (t == Ty::Unknown) t = it->second.type;
                    dims = it->second.dynDims;
                }
                const ExprPtr& off = n->args[0];
                std::vector<ExprPtr> idx;
                if (off->kind == EK::Func && off->sval.rfind("@index", 0) == 0) idx = off->args;
                else {
                    uint16_t desc = n->addr;
                    idx = decomposeDyn(off, tySize(t), dims, [desc, dims](const ExprPtr& a) {
                        return a->kind == EK::Var && isDescWord(a->addr, desc, dims);
                    });
                }
                for (auto& x : idx) x = simplifyExpr(x);
                return mkArrElem(n->addr, t, idx, AF_Dynamic);
            }
            if (n->kind == EK::Var) {
                Ty t = n->type == Ty::Unknown ? resolveVarType(n->addr) : n->type;
                if (const ArrayInfo* ai = staticArrayAt(n->addr, t)) {
                    int size = tySize(ai->type);
                    Linear l;
                    l.constant = int32_t(n->addr - ai->base);
                    std::vector<ExprPtr> idx = decompose(l, size, ai->dims);
                    return mkArrElem(ai->base, ai->type, idx, 0);
                }
                if (t != n->type) return mkVar(n->addr, t);
            }
            if (n->kind == EK::Raw && (n->sval == "@array" || n->sval == "@arrayarg") && n->type == Ty::Unknown) {
                auto it = pinfo.arrays.find(n->addr);
                if (it != pinfo.arrays.end()) {
                    auto c = std::make_shared<Expr>(*n);
                    c->type = it->second.type;
                    return c;
                }
            }
            return n;
        });
    });

    // 4. reference counts of scalar variables
    forEachExprSlot(results, info, [&](ExprPtr& e, uint32_t pi) {
        walkExpr(e, [&](const ExprPtr& n) {
            if (n->kind == EK::Var) {
                pinfo.varRefs[n->addr]++;
                pinfo.varTypes[n->addr] = n->type;
                if (pi != UINT32_MAX) pinfo.varProcs[n->addr].insert(pi);
            } else if ((n->kind == EK::ArrElem && !(n->flags & AF_Param)) || (n->kind == EK::Raw && (n->sval == "@array" || n->sval == "@arrayarg"))) {
                if (pi != UINT32_MAX) pinfo.arrayProcs[n->addr].insert(pi);
            }
        });
    });
    for (const auto& kv : info.fns) {
        auto pit = procOfFn.find(kv.first);
        if (pit == procOfFn.end()) continue;
        for (uint16_t p : kv.second.params) pinfo.varProcs[p].insert(pit->second);
        if (kv.second.body)
            walkExpr(kv.second.body, [&](const ExprPtr& n) {
                if (n->kind == EK::Var) pinfo.varProcs[n->addr].insert(pit->second);
            });
    }

    // 5. inline compiler temporaries passed to SUBs: "T = expr: CALL s(T)"
    for (auto& blocks : results) {
        for (LiftedBlock& lb : blocks) {
            auto& ss = lb.stmts;
            for (size_t k = 0; k < ss.size(); ++k) {
                if (ss[k].kind != SK::Text || ss[k].parts.empty() || ss[k].parts[0].text.rfind("CALL ", 0) != 0) continue;
                size_t j = k;
                while (j > 0 && ss[j - 1].kind == SK::Assign && ss[j - 1].lhs && ss[j - 1].lhs->kind == EK::Var) {
                    Stmt& a = ss[j - 1];
                    uint16_t t = a.lhs->addr;
                    bool used = false;
                    for (Part& p : ss[k].parts) {
                        if (p.expr && p.expr->kind == EK::Var && p.expr->addr == t && pinfo.varRefs[t] == 2 &&
                            a.rhs && a.rhs->kind != EK::Var && a.rhs->kind != EK::ArrElem && a.rhs->kind != EK::Param) {
                            p.expr = a.rhs;
                            used = true;
                            break;
                        }
                    }
                    if (!used) break;
                    pinfo.varRefs[t] = 0;
                    ss.erase(ss.begin() + long(j - 1));
                    --j;
                    --k;
                }
            }
        }
    }
    (void)warnings;
    return pinfo;
}

} // namespace qb
