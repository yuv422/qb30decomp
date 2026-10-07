// Internal definitions shared by the lifter source files.
#pragma once

#include "lifter.h"
#include "mbf.h"
#include "simplify.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace qb {

enum Reg { AX = 0, CX, DX, BX, SP, BP, SI, DI, NREG };

struct Val {
    ExprPtr e;   // current value
    ExprPtr fb;  // fallback: the computed value when e is an alias variable
};

struct Flags {
    enum Kind { None, Cmp, RtCmp, Test } kind = None;
    ExprPtr a, b;
    bool swapped = false;  // a and b are the CMP operands in reverse order
};

struct State {
    bool valid = false;
    Val r[NREG];
    Val fac;
    Ty facTy = Ty::Unknown;
    std::map<int, Val> slots;
    std::map<int, Ty> slotTy;
    std::map<int, Val> frame;
    std::vector<Val> stack;
    Flags flags;
};

// Pending multi-opcode statements (PRINT, LINE, COLOR ...).
struct Pending {
    // PRINT / WRITE / LPRINT / PRINT # ...
    bool print = false;
    std::string printHead;
    bool printWrite = false;
    bool printUsing = false;
    std::vector<std::pair<ExprPtr, char>> printItems; // separator ',' ';' or 0 (end of line)
    uint16_t printAddr = 0;

    // argument lists for COLOR / LOCATE / SCREEN
    std::vector<ExprPtr> args;
    bool argsActive = false;

    // graphics coordinates
    bool step = false;
    struct Pt { ExprPtr x, y; bool step = false; bool valid = false; };
    Pt p1, p2;
    ExprPtr circleStart, circleEnd, circleAspect;
    ExprPtr viewX1, viewY1, viewX2, viewY2;

    // INPUT
    bool input = false;
    std::string inputHead;
    int inputCount = 0;
    std::vector<Ty> inputTypes;
    std::vector<ExprPtr> inputVars;
    ExprPtr inputFile;
    std::string inputLinePrompt; // LINE INPUT form of the prompt
    uint16_t inputAddr = 0;

    // OPEN
    ExprPtr openMode;      // numeric mode
    ExprPtr openModeStr;   // "O", "I" ...
    ExprPtr openAccess;

    // FIELD
    bool field = false;
    ExprPtr fieldFile;
    std::vector<std::pair<ExprPtr, ExprPtr>> fieldVars;
    uint16_t fieldAddr = 0;

    // LOCK record numbers that did not fit an integer
    std::vector<ExprPtr> lockRecs;

    // dynamic array element selected by DYN_TARGET
    ExprPtr dynTarget;
};
struct Lifter::Impl {
    Program& program;
    const LoadedProgram& prog;
    GlobalInfo& info;
    std::vector<std::vector<LiftedBlock>>& results;
    std::vector<std::string>& warnings;
    int pass = 1;

    // current procedure / block
    uint32_t procIndex = 0;
    const Proc* proc = nullptr;
    const Block* block = nullptr;
    bool emit = false;
    State st;
    Pending pend;
    std::vector<Stmt>* out = nullptr;
    uint16_t curAddr = 0;
    bool popRet = false;            // a POP removed the GOSUB return address
    uint16_t slotBase = 0;
    int facWrites = 0;              // number of FAC writes (single line DEF FN result detection)
    std::set<int> everSlot;         // temp slots written anywhere in the current procedure

    // fixpoint data
    std::vector<State> entryStates, exitStates;

    // pass 1 bookkeeping for DEF FN parameters
    std::vector<uint16_t> recentStores;  // variables stored since the last statement boundary
    int pendingFnResult = -1;            // fn whose result variable is read next (multi-line)
    // pass 2: pending arguments of DEF FN calls (param var -> value)
    std::map<uint16_t, std::vector<ExprPtr>> fnArgs; // stores into DEF FN parameters not yet used by a call
    // result variables of multi-line DEF FN calls just made
    std::map<uint16_t, ExprPtr> fnResultOverride;
    // deferred stores of join values into predecessors
    struct PhiStore { uint32_t pred; ExprPtr lhs; int loc; };
    std::vector<PhiStore> phiStores;

    Impl(Program& p, GlobalInfo& i, std::vector<std::vector<LiftedBlock>>& r, std::vector<std::string>& w)
        : program(p), prog(p.prog()), info(i), results(r), warnings(w) {
        slotBase = prog.slotBase;
    }

    // ---- diagnostics
    void warn(const std::string& msg) {
        if (!emit) return;
        char buf[16];
        std::snprintf(buf, sizeof buf, "%04X: ", curAddr);
        warnings.push_back(buf + msg);
    }

    // ---- statements
    void addStmt(Stmt s) {
        if (!emit) return;
        if (s.addr == 0) s.addr = curAddr;
        out->push_back(std::move(s));
    }
    void addText(std::vector<Part> parts) {
        flushPendingStatements();
        addStmt(mkTextStmt(curAddr, std::move(parts)));
        recentStores.clear();
    }
    void addText(const std::string& text) { addText(std::vector<Part>{P(text)}); }

    // ---- values
    static ExprPtr unknownVal(const char* why) { return mkUnknown(why); }

    void setReg(int r, ExprPtr e) {
        st.r[r].e = std::move(e);
        st.r[r].fb = nullptr;
    }
    ExprPtr reg(int r) {
        const ExprPtr& e = st.r[r].e;
        if (!e) return unknownVal("reg");
        return e;
    }

    // Is the value at DS:addr a literal constant?
    bool isLiteralAddr(uint16_t a, Ty t) const {
        int n = t == Ty::Dbl ? 8 : t == Ty::Int ? 2 : 4;
        if (!prog.isInitialised(a, n)) return false;
        if (t == Ty::Str) {
            uint16_t len = prog.dsWord(a), ptr = prog.dsWord(uint16_t(a + 2));
            if (len > 0 && !prog.isInitialised(ptr, len)) return false;
        }
        return true;
    }
    bool isSlotAddr(uint16_t a) const {
        return a >= slotBase && a < uint16_t(slotBase + 128 * 8) && !prog.dsInit[a];
    }

    ExprPtr literalAt(uint16_t a, Ty t) const {
        switch (t) {
        case Ty::Sng: return mkSng(mbfSingleToDouble(&prog.ds[a]));
        case Ty::Dbl: return mkDbl(mbfDoubleToDouble(&prog.ds[a]));
        case Ty::Int: return mkInt(int16_t(prog.dsWord(a)));
        case Ty::Str: {
            uint16_t len = prog.dsWord(a), ptr = prog.dsWord(uint16_t(a + 2));
            std::string s;
            for (uint16_t i = 0; i < len; ++i) s += char(prog.ds[uint16_t(ptr + i)]);
            return mkStr(s);
        }
        default: return mkUnknown("literal");
        }
    }

    ExprPtr facValue(Ty t) {
        ExprPtr e = st.fac.e ? st.fac.e : unknownVal("fac");
        if (st.facTy == t || st.facTy == Ty::Unknown || t == Ty::Unknown) return e;
        return mkConv(e, t);
    }
    void setFac(ExprPtr e, Ty t) {
        ++facWrites;
        st.fac.e = std::move(e);
        st.fac.fb = nullptr;
        st.facTy = t;
    }

    // Converts a pointer value held in a register to the value it points to.
    ExprPtr deref(const ExprPtr& p, Ty t) {
        if (!p) return unknownVal("ptr");
        switch (p->kind) {
        case EK::PtrTo: {
            ExprPtr v = p->args[0];
            if (t != Ty::Unknown && v->type != t && v->type != Ty::Unknown && t != Ty::Str) return mkConv(v, t);
            return v;
        }
        case EK::IntLit: {
            uint16_t a = uint16_t(p->ival);
            return locationValue(a, t);
        }
        case EK::Param:
            if (p->type == Ty::Unknown && p->flags == 0) {
                noteParamType(p->ival, t);
                return mkParam(p->ival, t);
            }
            break;
        case EK::Binary:
            if (p->op == Op::Add || p->op == Op::Sub) {
                // dynamic string array: [descriptor] + offset (the constant part of
                // the offset may have been folded to the top of the sum)
                Linear l = toLinear(p);
                for (size_t k = 0; k < l.terms.size(); ++k) {
                    const ExprPtr& d = l.terms[k].first;
                    if (l.terms[k].second == 1 && d->kind == EK::Var && d->type == Ty::Int &&
                        info.dynArrays.count(d->addr)) {
                        Linear rest = l;
                        rest.terms.erase(rest.terms.begin() + long(k));
                        return mkArrElem(d->addr, t, {fromLinear(rest)}, AF_ByteOffset | AF_Dynamic);
                    }
                    // string array parameter: [descriptor on the stack] + offset
                    if (l.terms[k].second == 1 && d->kind == EK::Param && d->flags == 0 && isArrayParam(d->ival)) {
                        Linear rest = l;
                        rest.terms.erase(rest.terms.begin() + long(k));
                        return mkArrElem(0, t, {fromLinear(rest)}, AF_ByteOffset | AF_Dynamic | AF_Param, d->ival);
                    }
                }
                int32_t base = 0;
                ExprPtr off;
                if (p->op == Op::Add && isIntLit(p->args[1], &base)) off = p->args[0];
                else if (p->op == Op::Add && isIntLit(p->args[0], &base)) off = p->args[1];
                else if (l.constant != 0 && !l.terms.empty()) {
                    // addresses above 7FFFh are negative literals: x - 2CB4h is x + D34Ch
                    Linear rest = l;
                    rest.constant = 0;
                    base = l.constant;
                    off = fromLinear(rest);
                }
                if (off) {
                    // dynamic string array: [descriptor] + offset
                    if (off->kind == EK::Var && info.dynArrays.count(off->addr) && off->type == Ty::Int) {
                        return mkArrElem(off->addr, t, {mkInt(base)}, AF_ByteOffset | AF_Dynamic);
                    }
                    return mkArrElem(uint16_t(base), t, {off}, AF_ByteOffset);
                }
                // dynamic string array with variable offset: [desc] + off
                for (int k = 0; k < 2; ++k) {
                    const ExprPtr& d = p->args[k];
                    if (d->kind == EK::Var && d->type == Ty::Int && info.dynArrays.count(d->addr))
                        return mkArrElem(d->addr, t, {p->args[1 - k]}, AF_ByteOffset | AF_Dynamic);
                }
            }
            break;
        case EK::Var:
            // [descriptor] alone: element 0 of a dynamic string array
            if (p->type == Ty::Int && info.dynArrays.count(p->addr))
                return mkArrElem(p->addr, t, {mkInt(0)}, AF_ByteOffset | AF_Dynamic);
            break;
        default:
            break;
        }
        // A pointer value that isn't understood.
        return mkUnknown("deref", t);
    }

    ExprPtr locationValue(uint16_t a, Ty t) {
        auto ov = fnResultOverride.find(a);
        if (ov != fnResultOverride.end()) return ov->second;
        if (a == 0x1A && (t == Ty::Sng || t == Ty::Unknown)) return facValue(Ty::Sng);
        if (a == 0x16 && (t == Ty::Dbl || t == Ty::Unknown)) return facValue(Ty::Dbl);
        if (isSlotAddr(a) && (a - slotBase) % 8 == 0) {
            // a temp slot, if one was saved there (otherwise it is an ordinary address)
            int n = (a - slotBase) / 8;
            auto it = st.slots.find(n);
            if (it != st.slots.end() && it->second.e) {
                ExprPtr v = it->second.e;
                Ty st2 = st.slotTy[n];
                if (t != Ty::Unknown && st2 != t) return mkConv(v, t);
                return v;
            }
            if (everSlot.count(n)) return mkUnknown("slot", t);
        }
        if (t != Ty::Unknown && isLiteralAddr(a, t)) return literalAt(a, t);
        noteVarRead(a, t);
        return mkVar(a, t);
    }

    // ---- aliases and invalidation
    // Extent of a static array (bytes) as estimated after the first pass.
    uint32_t arrayExtent(uint16_t base) const {
        auto it = info.arrayExtent.find(base);
        return it == info.arrayExtent.end() ? 0 : it->second;
    }
    bool isArrayParam(int k) const {
        if (!proc || proc->kind != ProcKind::Sub) return false;
        auto it = info.subs.find(proc->entry);
        return it != info.subs.end() && k >= 0 && k < int(it->second.params.size()) && it->second.params[size_t(k)].isArray;
    }
    // Bytes a static array element with this base may touch. Constant subscripts
    // are folded into the base, so it can point into the middle of the array.
    std::pair<uint32_t, uint32_t> arrayRange(uint16_t base) const {
        auto it = info.arrayExtent.upper_bound(base);
        if (it != info.arrayExtent.begin()) {
            --it;
            if (base < it->first + it->second) return {it->first, it->first + it->second};
        }
        return {base, base + 256u}; // not known yet
    }
    // A variable only used by the current procedure (known after the first pass).
    bool isLocalVar(uint16_t a) const {
        if (pass == 1) return false;
        auto r = info.varReadProcs.find(a);
        auto w = info.varWriteProcs.find(a);
        auto only = [&](const std::set<uint32_t>& s) { return s.empty() || (s.size() == 1 && *s.begin() == procIndex); };
        return (r == info.varReadProcs.end() || only(r->second)) && (w == info.varWriteProcs.end() || only(w->second));
    }
    // Can the result of DEF FN `fn` change when [addr, addr+size) is written?
    bool fnReadsRange(uint16_t fn, uint16_t addr, int size) const {
        if (pass == 1) return true;
        auto r = info.fnBodyReads.find(fn);
        if (r == info.fnBodyReads.end()) return true;
        auto f = info.fns.find(fn);
        for (uint16_t v : r->second) {
            if (f != info.fns.end() && std::find(f->second.params.begin(), f->second.params.end(), v) != f->second.params.end())
                continue;
            if (v < addr + size && addr < v + 8) return true;
        }
        return false;
    }
    // Does e read memory that a write to the DS range [addr, addr+size) may change?
    bool readsRange(const ExprPtr& e, uint16_t addr, int size) const {
        bool hit = false;
        walkExpr(e, [&](const ExprPtr& n) {
            if (hit) return;
            switch (n->kind) {
            case EK::Var: {
                int sz = n->type == Ty::Dbl ? 8 : n->type == Ty::Int ? 2 : 4;
                if (n->addr < addr + size && addr < n->addr + sz) hit = true;
                break;
            }
            case EK::ArrElem:
                if (!(n->flags & (AF_Dynamic | AF_Param))) {
                    auto r = arrayRange(n->addr);
                    if (uint32_t(addr + size) > r.first && uint32_t(addr) < r.second) hit = true;
                }
                break;
            case EK::Param:
                if (n->type != Ty::Unknown) hit = true; // a parameter may refer to any variable
                break;
            case EK::FnCall:
                hit = fnReadsRange(n->addr, addr, size);
                break;
            default:
                break;
            }
        });
        return hit;
    }
    // Does e read an element of the array at `base` (static data address or descriptor)?
    bool readsArray(const ExprPtr& e, uint16_t base, bool dynamic) const {
        bool hit = false;
        auto r = arrayRange(base);
        auto overlaps = [&](uint16_t b2) {
            auto r2 = arrayRange(b2);
            return r2.first < r.second && r.first < r2.second;
        };
        walkExpr(e, [&](const ExprPtr& n) {
            if (hit) return;
            if (n->kind == EK::ArrElem && (n->flags & AF_Param)) hit = true;
            else if (n->kind == EK::ArrElem && dynamic && (n->flags & AF_Dynamic) && n->addr == base) hit = true;
            else if (n->kind == EK::ArrElem && !dynamic && !(n->flags & AF_Dynamic) && overlaps(n->addr)) hit = true;
            else if (n->kind == EK::Var && !dynamic && n->addr >= r.first && n->addr < r.second) hit = true;
            else if (n->kind == EK::Param && n->type != Ty::Unknown) hit = true;
            else if (n->kind == EK::FnCall) hit = true;
        });
        return hit;
    }
    void invalidate(const std::function<bool(const ExprPtr&)>& reads, const Val* keep = nullptr) {
        auto fix = [&](Val& v) {
            if (&v == keep) return;
            if (v.fb && reads(v.fb)) v.fb = nullptr;
            if (v.e && reads(v.e)) {
                v.e = v.fb ? v.fb : mkUnknown("stale");
                v.fb = nullptr;
            }
        };
        for (auto& v : st.r) fix(v);
        fix(st.fac);
        for (auto& kv : st.slots) fix(kv.second);
        for (auto& kv : st.frame) fix(kv.second);
        for (auto& v : st.stack) fix(v);
    }
    void invalidateLoc(const ExprPtr& lhs, const Val* keep = nullptr) {
        if (!lhs) return;
        if (lhs->kind == EK::Var) {
            int sz = lhs->type == Ty::Dbl ? 8 : lhs->type == Ty::Int ? 2 : 4;
            uint16_t a = lhs->addr;
            invalidate([&](const ExprPtr& e) { return readsRange(e, a, sz); }, keep);
        } else if (lhs->kind == EK::ArrElem && (lhs->flags & AF_Param)) {
            // an array parameter can only alias other array elements (or scalar parameters)
            invalidate([&](const ExprPtr& e) {
                bool hit = false;
                walkExpr(e, [&](const ExprPtr& n) {
                    if (n->kind == EK::ArrElem || n->kind == EK::FnCall || (n->kind == EK::Param && n->type != Ty::Unknown))
                        hit = true;
                });
                return hit;
            }, keep);
        } else if (lhs->kind == EK::ArrElem) {
            uint16_t b = lhs->addr;
            bool dyn = (lhs->flags & AF_Dynamic) != 0;
            invalidate([&](const ExprPtr& e) { return readsArray(e, b, dyn); }, keep);
        } else if (lhs->kind == EK::Param) {
            // the parameter may refer to any variable of the caller, but not to
            // variables that are local to this SUB
            invalidate([&](const ExprPtr& e) {
                bool hit = false;
                walkExpr(e, [&](const ExprPtr& n) {
                    if (n->kind == EK::Var && !isLocalVar(n->addr)) hit = true;
                    else if (n->kind == EK::ArrElem || n->kind == EK::FnCall || (n->kind == EK::Param && n->type != Ty::Unknown))
                        hit = true;
                });
                return hit;
            }, keep);
        }
    }
    void invalidateAllMemory(const Val* keep = nullptr) {
        invalidate([](const ExprPtr& e) {
            bool hit = false;
            walkExpr(e, [&](const ExprPtr& n) {
                if (n->kind == EK::Var || n->kind == EK::ArrElem || n->kind == EK::FnCall ||
                    (n->kind == EK::Param && n->type != Ty::Unknown))
                    hit = true;
            });
            return hit;
        }, keep);
    }

    // Emits "lhs = value" and records that the source now aliases lhs.
    void storeTo(const ExprPtr& lhs, ExprPtr value, Val* src) {
        if (!lhs) return;
        if (lhs->kind == EK::Unknown) {
            warn("store to unknown location");
        }
        // stores into the FAC / slots are internal
        ExprPtr v = resolvePhi(value, lhs);
        if (!v) {
            if (src) {
                Val nv;
                nv.e = lhs;
                *src = nv;
            }
            invalidateLoc(lhs, src);
            return;
        }
        if (lhs->kind == EK::Var && pass == 2 && isFnParam(lhs->addr)) {
            fnArgs[lhs->addr].push_back(v);
        } else if (lhs->kind == EK::Var && pass == 2 && isFnResultVarInBody(lhs->addr)) {
            Stmt s = mkAssign(curAddr, mkRaw(fnNameFor(lhs->addr), lhs->type), v);
            flushPendingStatements();
            addStmt(std::move(s));
        } else {
            flushPendingStatements();
            addStmt(mkAssign(curAddr, lhs, v));
        }
        if (lhs->kind == EK::Var) {
            noteVarWrite(lhs->addr, lhs->type);
            recentStores.push_back(lhs->addr);
        }
        invalidateLoc(lhs, src);
        if (src) {
            ExprPtr old = src->e;
            src->e = lhs;
            src->fb = (old && old->kind != EK::Unknown && old->kind != EK::Phi) ? old : nullptr;
            // the fallback must not read the location just written
            if (src->fb) {
                Val tmp = *src;
                tmp.e = nullptr;
                const ExprPtr fbv = tmp.fb;
                if (lhs->kind == EK::Var) {
                    int sz = lhs->type == Ty::Dbl ? 8 : lhs->type == Ty::Int ? 2 : 4;
                    if (readsRange(fbv, lhs->addr, sz)) src->fb = nullptr;
                } else {
                    src->fb = nullptr;
                }
            }
        }
    }

    // A value that differs between predecessors of a join: if it is stored as the
    // first statement of a block, the store is moved into the predecessors.
    ExprPtr resolvePhi(const ExprPtr& value, const ExprPtr& lhs) {
        if (!value || value->kind != EK::Phi) return value;
        if (!emit) return nullptr;
        if (!out->empty()) {
            warn("join value stored after other statements");
        }
        phiStores.push_back({value->addr, lhs, value->ival});
        return nullptr;
    }

    // ---- variable access bookkeeping (pass 1)
    void noteParamType(int k, Ty t) {
        if (pass != 1 || !proc || proc->kind != ProcKind::Sub || t == Ty::Unknown || k < 0) return;
        auto& ps = info.subs[proc->entry].params;
        if (ps.size() <= size_t(k)) ps.resize(size_t(k) + 1);
        if (ps[size_t(k)].type == Ty::Unknown && !ps[size_t(k)].isArray) ps[size_t(k)].type = t;
    }
    bool lvalueMode = false;  // deref() builds a store target: not a read
    ExprPtr lvalue(const ExprPtr& p, Ty t) {
        lvalueMode = true;
        ExprPtr r = deref(p, t);
        lvalueMode = false;
        return r;
    }
    void noteVarRead(uint16_t a, Ty t = Ty::Unknown) {
        if (pass != 1 || lvalueMode) return;
        info.varReadProcs[a].insert(procIndex);
        if (t != Ty::Unknown) info.varTypes[a].insert(t);
        if (proc && (proc->kind == ProcKind::DefFn || proc->kind == ProcKind::DefFnSingle))
            info.fnBodyReads[proc->entry].insert(a);
        if (pendingFnResult >= 0) {
            if (!info.fnResultReads.count(uint16_t(pendingFnResult))) {
                info.fnResultReads[uint16_t(pendingFnResult)] = a;
                info.fnResultTypes[uint16_t(pendingFnResult)] = t;
            }
            pendingFnResult = -1;
        }
    }
    void noteVarWrite(uint16_t a, Ty t = Ty::Unknown) {
        if (pass != 1) return;
        info.varWriteProcs[a].insert(procIndex);
        if (t != Ty::Unknown) info.varTypes[a].insert(t);
        if (proc && (proc->kind == ProcKind::DefFn || proc->kind == ProcKind::DefFnSingle))
            info.fnBodyWrites[proc->entry].insert(a);
    }
    bool isFnParam(uint16_t a) const {
        for (const auto& kv : info.fns)
            for (uint16_t p : kv.second.params)
                if (p == a && (proc == nullptr || proc->entry != kv.first)) return true;
        return false;
    }
    bool isFnResultVarInBody(uint16_t a) const {
        if (!proc || proc->kind != ProcKind::DefFn) return false;
        auto it = info.fns.find(proc->entry);
        return it != info.fns.end() && it->second.hasResultVar && it->second.resultVar == a;
    }
    std::string fnNameFor(uint16_t resultVar) const {
        for (const auto& kv : info.fns)
            if (kv.second.hasResultVar && kv.second.resultVar == resultVar) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "FNf%04X%s", kv.first, tySuffix(kv.second.type));
                return buf;
            }
        return "FN?";
    }

    // ---- pending multi-op statements
    void flushPendingStatements() {
        if (pend.field) {
            pend.field = false;
            std::vector<Part> parts{P("FIELD #"), P(pend.fieldFile, Ty::Int)};
            for (auto& fv : pend.fieldVars) {
                parts.push_back(P(", "));
                parts.push_back(P(fv.second, Ty::Int));
                parts.push_back(P(" AS "));
                parts.push_back(P(fv.first, Ty::Str));
            }
            pend.fieldVars.clear();
            if (emit) out->push_back(mkTextStmt(pend.fieldAddr, std::move(parts)));
        }
    }

    // implemented in lifter_x86.cpp / lifter_rt.cpp
    void execBlock(const Block& b);
    void execInsn(const LInsn& li);
    void execX86(const Insn& in);
    void execRt(const Insn& in);
    void execIdiom(const LInsn& li);
    ExprPtr condFromFlags(ZydisMnemonic cc);
    ExprPtr operandValue(const Insn& in, int i);
    ExprPtr memLocation(const ZydisDecodedOperand& op, Ty t);
    void writeOperand(const Insn& in, int i, ExprPtr v, Val* src);
    void callNear(const Insn& in, uint16_t target);
    void callFar(const Insn& in);
    void callSub(uint16_t target, const std::string& name, bool external);
    void printItem(ExprPtr e, char sep);
    void printEnd();
    void inputStore(ExprPtr var);
    std::string litText(const ExprPtr& e);
    ExprPtr intArg(int r);
    ExprPtr ptrArg(int r, Ty t);
    void gfxPoint(Pending::Pt& p, ExprPtr x, ExprPtr y, bool floatX, bool floatY);
    std::vector<Part> pointParts(const Pending::Pt& p);
    ExprPtr dynElem(Ty t);
    Ty dynType(const ExprPtr& desc);
    ExprPtr facOrAxResult(Ty t);

    // ---- driver
    State unknownState() {
        State s;
        s.valid = true;
        for (int i = 0; i < NREG; ++i) s.r[i].e = mkUnknown("entry");
        s.fac.e = mkUnknown("entry");
        return s;
    }
    State join(const State& a, const State& b, uint32_t blockId);
    bool sameState(const State& a, const State& b);
    void liftProc(uint32_t pi);
};


} // namespace qb
