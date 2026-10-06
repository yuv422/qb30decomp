#include "lifter.h"

#include "lifter_impl.h"
#include "mbf.h"
#include "opcodes.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace qb {

// ---------------------------------------------------------------------------
// Lifter::Impl

// ---------------------------------------------------------------------------
// State join

namespace {
bool valEq(const Val& a, const Val& b) { return exprEqual(a.e, b.e) && exprEqual(a.fb, b.fb); }
} // namespace

State Lifter::Impl::join(const State& a, const State& b, uint32_t blockId) {
    if (!a.valid) return b;
    if (!b.valid) return a;
    State s = a;
    auto merge = [&](Val& dst, const Val& other, int loc) {
        if (valEq(dst, other)) return;
        if (dst.e && other.e && exprEqual(dst.e, other.e)) {
            dst.fb = nullptr;
            return;
        }
        dst.e = mkPhi(blockId, loc);
        dst.fb = nullptr;
    };
    for (int i = 0; i < NREG; ++i) merge(s.r[i], b.r[i], i);
    merge(s.fac, b.fac, 100);
    if (a.facTy != b.facTy) s.facTy = Ty::Unknown;
    for (auto it = s.slots.begin(); it != s.slots.end();) {
        auto o = b.slots.find(it->first);
        if (o == b.slots.end() || !valEq(it->second, o->second)) it = s.slots.erase(it);
        else ++it;
    }
    for (auto it = s.frame.begin(); it != s.frame.end();) {
        auto o = b.frame.find(it->first);
        if (o == b.frame.end() || !valEq(it->second, o->second)) it = s.frame.erase(it);
        else ++it;
    }
    if (a.stack.size() != b.stack.size()) {
        s.stack.clear();
    } else {
        for (size_t i = 0; i < s.stack.size(); ++i) merge(s.stack[i], b.stack[i], 2000 + int(i));
    }
    if (!(a.flags.kind == b.flags.kind && exprEqual(a.flags.a, b.flags.a) && exprEqual(a.flags.b, b.flags.b)))
        s.flags = Flags();
    return s;
}

bool Lifter::Impl::sameState(const State& a, const State& b) {
    if (a.valid != b.valid) return false;
    for (int i = 0; i < NREG; ++i)
        if (!valEq(a.r[i], b.r[i])) return false;
    if (!valEq(a.fac, b.fac) || a.facTy != b.facTy) return false;
    if (a.slots.size() != b.slots.size() || a.frame.size() != b.frame.size() || a.stack.size() != b.stack.size())
        return false;
    for (auto& kv : a.slots) {
        auto o = b.slots.find(kv.first);
        if (o == b.slots.end() || !valEq(kv.second, o->second)) return false;
    }
    for (auto& kv : a.frame) {
        auto o = b.frame.find(kv.first);
        if (o == b.frame.end() || !valEq(kv.second, o->second)) return false;
    }
    for (size_t i = 0; i < a.stack.size(); ++i)
        if (!valEq(a.stack[i], b.stack[i])) return false;
    return true;
}

// Returns the value of location `loc` in a state (see join()).
static ExprPtr stateLoc(const State& s, int loc) {
    if (loc >= 0 && loc < NREG) return s.r[loc].e;
    if (loc == 100) return s.fac.e;
    if (loc >= 2000) {
        size_t i = size_t(loc - 2000);
        return i < s.stack.size() ? s.stack[i].e : nullptr;
    }
    return nullptr;
}

void Lifter::Impl::liftProc(uint32_t pi) {
    procIndex = pi;
    proc = &program.procs()[pi];
    const Proc& p = *proc;
    size_t n = p.blocks.size();
    entryStates.assign(n, State());
    exitStates.assign(n, State());
    results[pi].assign(n, LiftedBlock());
    phiStores.clear();

    // roots: entry, GOSUB targets, trap handlers, unreachable blocks
    std::vector<bool> isRoot(n, false);
    for (size_t i = 0; i < n; ++i) {
        const Block& b = p.blocks[i];
        if (b.preds.empty() || b.start == p.entry || p.gosubTargets.count(b.start) ||
            program.dis().trapTargets().count(b.start))
            isRoot[i] = true;
    }

    // reverse post order
    std::vector<uint32_t> order;
    {
        std::vector<int> mark(n, 0);
        std::vector<uint32_t> post;
        std::function<void(uint32_t)> dfs = [&](uint32_t v) {
            mark[v] = 1;
            for (uint32_t s : p.blocks[v].succs)
                if (!mark[s]) dfs(s);
            post.push_back(v);
        };
        for (size_t i = 0; i < n; ++i)
            if (isRoot[i] && !mark[i]) dfs(uint32_t(i));
        for (size_t i = 0; i < n; ++i)
            if (!mark[i]) dfs(uint32_t(i));
        order.assign(post.rbegin(), post.rend());
    }

    std::vector<bool> done(n, false);
    emit = false;
    for (int iter = 0; iter < 30; ++iter) {
        bool changed = false;
        for (uint32_t bi : order) {
            State in;
            if (isRoot[bi]) {
                in = unknownState();
            } else {
                for (uint32_t pr : p.blocks[bi].preds)
                    if (exitStates[pr].valid) in = join(in, exitStates[pr], bi);
                if (!in.valid) continue;
            }
            if (done[bi] && sameState(in, entryStates[bi])) continue;
            entryStates[bi] = in;
            st = in;
            execBlock(p.blocks[bi]);
            exitStates[bi] = st;
            done[bi] = true;
            changed = true;
        }
        if (!changed) break;
    }

    // final pass: emit statements
    emit = true;
    for (uint32_t bi : order) {
        st = entryStates[bi].valid ? entryStates[bi] : unknownState();
        execBlock(p.blocks[bi]);
    }
    for (const PhiStore& ps : phiStores) {
        const Block& b = p.blocks[ps.pred];
        for (uint32_t pr : b.preds) {
            ExprPtr v = stateLoc(exitStates[pr], ps.loc);
            if (!v || v->kind == EK::Phi || v->kind == EK::Unknown) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "%04X: unresolved join value", b.start);
                warnings.push_back(buf);
                v = mkUnknown("join");
            }
            // values are tracked as int registers or FAC; convert to the target type
            results[pi][pr].stmts.push_back(mkAssign(b.start, ps.lhs, v));
        }
    }
    proc = nullptr;
}

// ---------------------------------------------------------------------------

Lifter::Lifter(Program& program, GlobalInfo& info) : program_(program), info_(info) {}

void Lifter::run(int pass) {
    results_.assign(program_.procs().size(), {});
    warnings_.clear();
    Impl impl(program_, info_, results_, warnings_);
    impl.pass = pass;
    // DATA keys
    for (const DataLine& d : program_.prog().dataLines) info_.dataKeys.insert(d.key);
    for (uint32_t i = 0; i < program_.procs().size(); ++i) impl.liftProc(i);
}

} // namespace qb
