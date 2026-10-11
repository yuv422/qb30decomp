#include "emitter.h"

#include "printer.h"
#include "simplify.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>

namespace qb {

namespace {

std::string hex4(uint16_t v) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%04X", v);
    return buf;
}

// The longest line QB 3.0 accepts. Longer statements are continued on the next
// line with "_" (which QB allows wherever a space may be).
constexpr int kMaxLine = 252;

struct OutLine {
    int indent = 0;
    std::string text;
    int label = -1;   // label definition for this code address (emitted only when referenced)
    std::vector<uint16_t> lineNos; // BASIC line numbers defined at this line
};

struct Ctx {
    int hi = 0;           // end of the current region (block index, exclusive)
    int follow = -1;      // block reached when control falls off the end of the region
    int loopExit = -1;    // innermost loop exit block
    int loopHead = -1;    // head of the loop whose body is being structured
    bool loopIsFor = false;
    int skipTerm = -1;    // block whose terminator belongs to an enclosing construct
    int dropLast = -1;    // block whose last statement belongs to an enclosing FOR (increment)
};

struct ProcView {
    const Proc* proc = nullptr;
    uint32_t pi = 0;
    std::vector<LiftedBlock>* lbs = nullptr;
    int n = 0;
    std::vector<bool> live;
    std::vector<std::vector<int>> succ;   // resolved successors (same order as Block::succs)
    std::vector<std::vector<int>> preds;  // resolved predecessors (live blocks)
    int procEnd = -1;                     // SUB_END / DEF_FN_END block
    std::vector<bool> epilogue;           // blocks whose Ret is the end of the SUB / FN
};

bool blockHasRt(const Block& b, int intNo, int op) {
    for (const LInsn& li : b.code)
        if (li.insn && li.insn->isRt && li.insn->rt.is(intNo, op)) return true;
    return false;
}

class Emitter {
public:
    Emitter(Program& program, std::vector<std::vector<LiftedBlock>>& results, GlobalInfo& info, ProgramInfo& pinfo,
            std::vector<std::string>& warnings)
        : program_(program), results_(results), info_(info), pinfo_(pinfo), warnings_(warnings), printer_(names_) {
        setupNames();
        setupLineNumbers();
    }

    std::string run(const DecompileOptions& opt, const std::string& inputName);

private:
    void setupNames();
    void setupLineNumbers();
    ProcView makeView(uint32_t pi);
    bool isForwarder(const ProcView& v, int b) const;
    int resolve(const ProcView& v, int b) const;
    int nextLive(const ProcView& v, int i) const {
        for (int j = i + 1; j < v.n; ++j)
            if (v.live[size_t(j)]) return j;
        return v.n;
    }
    int firstLive(const ProcView& v, int i) const {
        for (int j = i; j < v.n; ++j)
            if (v.live[size_t(j)]) return j;
        return v.n;
    }
    int prevLive(const ProcView& v, int i) const {
        for (int j = i - 1; j >= 0; --j)
            if (v.live[size_t(j)]) return j;
        return -1;
    }
    const LiftedBlock& lb(const ProcView& v, int b) const { return (*v.lbs)[size_t(b)]; }
    LiftedBlock& lbm(ProcView& v, int b) { return (*v.lbs)[size_t(b)]; }
    const Block& blk(const ProcView& v, int b) const { return v.proc->blocks[size_t(b)]; }
    TermKind term(const ProcView& v, int b) const { return blk(v, b).term; }

    void line(int ind, const std::string& text) {
        out_.push_back({ind, text, -1, pendingLines_});
        pendingLines_.clear();
    }
    void placeDims(const std::vector<std::pair<uint16_t, std::string>>& dims, std::vector<OutLine>& fnLines,
                   std::vector<OutLine>& mainLines);
    void labelLine(uint16_t addr) {
        // line numbers of code before the label stay in front of it
        if (!pendingLines_.empty()) out_.push_back({0, "", -2, pendingLines_});
        pendingLines_.clear();
        OutLine l{0, "", int(addr), {}};
        auto it = lineAt_.find(addr);
        if (it != lineAt_.end()) l.lineNos = it->second;
        out_.push_back(l);
    }
    // line numbers that start inside block b, by statement (last entry: after the statements)
    std::vector<std::vector<uint16_t>> blockLines(const ProcView& v, int b) const;

    void region(ProcView& v, int lo, const Ctx& ctx, int ind);
    void emitBody(ProcView& v, int b, const Ctx& ctx, int ind, size_t dropTail = 0);
    void emitTerm(ProcView& v, int b, const Ctx& ctx, int ind);
    bool tryIf(ProcView& v, int i, const Ctx& ctx, int ind, int& next);
    bool trySelect(ProcView& v, int i, const Ctx& ctx, int ind, int& next);
    bool tryLoop(ProcView& v, int i, const Ctx& ctx, int ind, int& next);
    bool tryFor(ProcView& v, int i, const Ctx& ctx, int ind, int& next);
    std::string jumpText(const ProcView& v, int target, const Ctx& ctx);
    void jumpTo(ProcView& v, int from, int target, const Ctx& ctx, int ind);
    // block index -> label address
    uint16_t addrOf(const ProcView& v, int b) const { return blk(v, b).start; }
    // condition for taking `target` out of block b (Cond terminator)
    ExprPtr condTo(const ProcView& v, int b, int target) const {
        const ExprPtr& c = lb(v, b).cond;
        if (v.succ[size_t(b)][0] == target) return c;
        return invertCond(c);
    }

    void emitProc(uint32_t pi, int ind);
    std::string subHeader(const Proc& p);
    std::vector<std::string> sharedList(uint32_t pi);

    Program& program_;
    std::vector<std::vector<LiftedBlock>>& results_;
    GlobalInfo& info_;
    ProgramInfo& pinfo_;
    std::vector<std::string>& warnings_;
    NameResolver names_;
    Printer printer_;
    std::vector<OutLine> out_;
    std::set<uint16_t> refLabels_;
    std::set<uint16_t> deadLabels_; // starts of unreachable code
    std::map<uint16_t, uint16_t> labelCanon_; // address -> address of the live block that holds the label
    std::map<uint16_t, std::vector<uint16_t>> lineAt_; // code address -> line numbers starting there
    std::vector<uint16_t> pendingLines_;               // line numbers for the next output line
    const ProcView* cur_ = nullptr;
};

void Emitter::setupNames() {
    names_.rowOrder = program_.prog().optR();
    names_.var = [](uint16_t a, Ty t) { return "vari" + hex4(a) + tySuffix(t); };
    names_.array = [this](uint16_t a, Ty t) {
        auto it = pinfo_.arrays.find(a);
        if (it != pinfo_.arrays.end() && t == Ty::Unknown) t = it->second.type;
        return "vari" + hex4(a) + tySuffix(t);
    };
    names_.param = [](int i, Ty t) { return "arg" + std::to_string(i + 1) + tySuffix(t); };
    names_.fn = [this](uint16_t a) {
        auto it = info_.fns.find(a);
        Ty t = it != info_.fns.end() ? it->second.type : Ty::Sng;
        return "FNf" + hex4(a) + tySuffix(t);
    };
    names_.fnParamType = [this](uint16_t a, size_t i) {
        auto it = info_.fns.find(a);
        if (it == info_.fns.end() || i >= it->second.paramTypes.size()) return Ty::Unknown;
        return it->second.paramTypes[i];
    };
    names_.label = [this](uint16_t a) {
        auto it = labelCanon_.find(a);
        if (it != labelCanon_.end()) a = it->second;
        refLabels_.insert(a);
        auto ln = lineAt_.find(a);
        if (ln != lineAt_.end()) return std::to_string(ln->second.front());
        return "loc" + hex4(a);
    };
}

// The line table has an entry for every statement (/X, /E: the current ERL
// value, 0 before the first numbered line) or for every numbered line (/D). A
// line number starts at the lowest address that carries it.
void Emitter::setupLineNumbers() {
    std::map<uint16_t, std::pair<uint16_t, size_t>> first; // line -> (address, table index)
    const auto& table = program_.prog().lineTable;
    for (size_t i = 0; i < table.size(); ++i) {
        uint16_t addr = table[i].first, ln = table[i].second;
        if (ln == 0) continue;
        auto it = first.find(ln);
        if (it == first.end() || addr < it->second.first) first[ln] = {addr, i};
    }
    std::map<uint16_t, std::vector<std::pair<size_t, uint16_t>>> byAddr;
    for (const auto& kv : first) byAddr[kv.second.first].push_back({kv.second.second, kv.first});
    for (auto& kv : byAddr) {
        std::sort(kv.second.begin(), kv.second.end());
        for (const auto& e : kv.second) lineAt_[kv.first].push_back(e.second);
    }
}

bool Emitter::isForwarder(const ProcView& v, int b) const {
    const Block& bk = blk(v, b);
    const LiftedBlock& l = lb(v, b);
    if (!l.stmts.empty() || l.noJump || l.returnWithTarget) return false;
    if (bk.term != TermKind::Jump && bk.term != TermKind::Fall) return false;
    if (bk.succs.empty()) return false;
    if (v.proc->gosubTargets.count(bk.start) || program_.dis().trapTargets().count(bk.start)) return false;
    if (bk.start == v.proc->entry) return false;
    // "GOTO itself" (an endless loop) leads nowhere else
    if (int(bk.succs[0]) == b) return false;
    // A loop's closing jump that is also the exit of a loop test right before it
    // ("jle body / jmp head" ending a FOR inside a WHILE) is the outer loop's latch.
    if (bk.term == TermKind::Jump && int(bk.succs[0]) <= b && b > 0 && blk(v, b - 1).term == TermKind::Cond &&
        blk(v, b - 1).succs.size() > 1 && int(blk(v, b - 1).succs[1]) == b &&
        int(blk(v, b - 1).succs[0]) < b - 1)
        return false;
    return true;
}

int Emitter::resolve(const ProcView& v, int b) const {
    int guard = 0;
    while (b >= 0 && b < v.n && isForwarder(v, b) && guard++ < 10000) b = int(blk(v, b).succs[0]);
    return b;
}

ProcView Emitter::makeView(uint32_t pi) {
    ProcView v;
    v.proc = &program_.procs()[pi];
    v.pi = pi;
    v.lbs = &results_[pi];
    v.n = int(v.proc->blocks.size());
    v.live.assign(size_t(v.n), true);
    v.epilogue.assign(size_t(v.n), false);
    for (int b = 0; b < v.n; ++b) v.live[size_t(b)] = !isForwarder(v, b);
    v.succ.resize(size_t(v.n));
    v.preds.resize(size_t(v.n));
    for (int b = 0; b < v.n; ++b) {
        for (uint32_t s : blk(v, b).succs) v.succ[size_t(b)].push_back(resolve(v, int(s)));
        if (v.live[size_t(b)])
            for (int s : v.succ[size_t(b)])
                if (s >= 0) v.preds[size_t(s)].push_back(b);
    }
    for (int b = 0; b < v.n; ++b) {
        const Block& bk = blk(v, b);
        if (v.proc->kind == ProcKind::Sub && blockHasRt(bk, 0x3f, 0x54)) {
            v.procEnd = b;
            v.epilogue[size_t(b)] = true;
        }
        if (v.proc->kind == ProcKind::DefFn && blockHasRt(bk, 0x3f, 0x3a)) {
            v.procEnd = b;
            v.epilogue[size_t(b)] = true;
        }
        if (v.proc->kind == ProcKind::DefFnSingle && bk.term == TermKind::Ret) v.epilogue[size_t(b)] = true;
    }
    // labels on forwarders move to the block they lead to
    for (int b = 0; b < v.n; ++b) {
        int r = resolve(v, b);
        if (r != b && r >= 0) labelCanon_[blk(v, b).start] = blk(v, r).start;
    }
    return v;
}

std::string Emitter::jumpText(const ProcView& v, int target, const Ctx& ctx) {
    if (target == ctx.loopExit && target >= 0) return ctx.loopIsFor ? "EXIT FOR" : "EXIT DO";
    if (target == v.procEnd && target >= 0) return v.proc->kind == ProcKind::Sub ? "EXIT SUB" : "EXIT DEF";
    if (target < 0 || target >= v.n) return "GOTO ???";
    return "GOTO " + names_.label(addrOf(v, target));
}

void Emitter::jumpTo(ProcView& v, int from, int target, const Ctx& ctx, int ind) {
    if (target < 0) return;
    int n = nextLive(v, from);
    int fallsTo = n < ctx.hi ? n : ctx.follow;
    if (target == fallsTo) return;
    // a jump to the end of the SUB / FN from its last statements
    if (target == v.procEnd && n == v.procEnd) return;
    line(ind, jumpText(v, target, ctx));
}

std::vector<std::vector<uint16_t>> Emitter::blockLines(const ProcView& v, int b) const {
    const Block& bk = blk(v, b);
    const LiftedBlock& l = lb(v, b);
    std::vector<std::vector<uint16_t>> res(l.stmts.size() + 1);
    for (auto it = lineAt_.upper_bound(bk.start); it != lineAt_.end() && it->first < bk.end; ++it) {
        // a statement's address is the last instruction of its code
        size_t k = 0;
        while (k < l.stmts.size() && l.stmts[k].addr < it->first) ++k;
        res[k].insert(res[k].end(), it->second.begin(), it->second.end());
    }
    return res;
}

void Emitter::emitBody(ProcView& v, int b, const Ctx& ctx, int ind, size_t dropTail) {
    labelLine(addrOf(v, b));
    const LiftedBlock& l = lb(v, b);
    std::vector<std::vector<uint16_t>> lines = blockLines(v, b);
    size_t n = l.stmts.size();
    if (b == ctx.dropLast && n > 0) --n;
    n = n >= dropTail ? n - dropTail : 0;
    for (size_t k = 0; k < l.stmts.size(); ++k) {
        pendingLines_.insert(pendingLines_.end(), lines[k].begin(), lines[k].end());
        if (k >= n) continue;
        std::string t = printer_.stmt(l.stmts[k]);
        if (t.empty()) continue;
        line(ind, t);
    }
    // line numbers of the terminator (IF, loop test ...) or of dropped statements
    pendingLines_.insert(pendingLines_.end(), lines.back().begin(), lines.back().end());
}

void Emitter::emitTerm(ProcView& v, int b, const Ctx& ctx, int ind) {
    const Block& bk = blk(v, b);
    const LiftedBlock& l = lb(v, b);
    const auto& sc = v.succ[size_t(b)];
    switch (bk.term) {
    case TermKind::Fall:
        if (!sc.empty()) jumpTo(v, b, sc.back(), ctx, ind);
        break;
    case TermKind::Jump:
        if (l.noJump) break;
        if (l.returnWithTarget && !sc.empty()) {
            line(ind, "RETURN " + names_.label(addrOf(v, sc[0])));
            break;
        }
        if (!sc.empty()) jumpTo(v, b, sc[0], ctx, ind);
        break;
    case TermKind::Cond: {
        if (sc.size() < 2 || !l.cond) {
            if (!sc.empty()) jumpTo(v, b, sc.back(), ctx, ind);
            break;
        }
        int T = sc[0], F = sc[1];
        int n = nextLive(v, b);
        int fallsTo = n < ctx.hi ? n : ctx.follow;
        if (T == F) {
            jumpTo(v, b, T, ctx, ind);
        } else if (T == fallsTo) {
            line(ind, "IF " + printer_.cond(invertCond(l.cond)) + " THEN " + jumpText(v, F, ctx));
        } else if (F == fallsTo) {
            line(ind, "IF " + printer_.cond(l.cond) + " THEN " + jumpText(v, T, ctx));
        } else {
            line(ind, "IF " + printer_.cond(l.cond) + " THEN " + jumpText(v, T, ctx));
            jumpTo(v, b, F, ctx, ind);
        }
        break;
    }
    case TermKind::OnGoto: {
        std::string sel = printer_.expr(l.selector, Ty::Int);
        std::vector<std::string> labels;
        for (size_t k = 0; k + 1 < sc.size(); ++k) labels.push_back(names_.label(addrOf(v, sc[k])));
        std::string s = "ON " + sel + " GOTO ";
        for (size_t k = 0; k < labels.size(); ++k) s += (k ? ", " : "") + labels[k];
        line(ind, s);
        if (!sc.empty()) jumpTo(v, b, sc.back(), ctx, ind);
        break;
    }
    case TermKind::Ret:
        if (!v.epilogue[size_t(b)]) line(ind, "RETURN");
        break;
    case TermKind::Stop:
        break;
    }
}

bool Emitter::tryIf(ProcView& v, int i, const Ctx& ctx, int ind, int& next) {
    if (term(v, i) != TermKind::Cond) return false;
    const auto& sc = v.succ[size_t(i)];
    if (sc.size() < 2 || !lb(v, i).cond) return false;
    int T = sc[0], F = sc[1];
    int n = nextLive(v, i);
    if (T == F) return false;
    int other;
    ExprPtr cond;
    if (T == n) {
        cond = lb(v, i).cond;
        other = F;
    } else if (F == n) {
        cond = invertCond(lb(v, i).cond);
        other = T;
    } else {
        return false;
    }
    if (other <= n || other > ctx.hi || (other == ctx.hi && ctx.follow != other)) return false;

    // the end of the whole IF (after an ELSE part), if any
    auto elseEnd = [&](int thenStart, int thenEnd) -> int {
        int last = prevLive(v, thenEnd);
        if (last < thenStart) return -1;
        if (term(v, last) != TermKind::Jump || lb(v, last).noJump || lb(v, last).returnWithTarget) return -1;
        int e = v.succ[size_t(last)].empty() ? -1 : v.succ[size_t(last)][0];
        if (e > thenEnd && (e < ctx.hi || (e == ctx.hi && ctx.follow == e))) return e;
        return -1;
    };

    int E = elseEnd(n, other);
    line(ind, "IF " + printer_.cond(cond) + " THEN");
    int thenStart = n, thenEnd = other;
    for (;;) {
        Ctx c = ctx;
        c.hi = thenEnd;
        c.follow = E >= 0 ? E : thenEnd;
        region(v, thenStart, c, ind + 1);
        if (E < 0) break;
        // ELSEIF: the else part is just another IF that ends at E
        int f = firstLive(v, thenEnd);
        bool chained = false;
        if (f < E && term(v, f) == TermKind::Cond && lb(v, f).stmts.empty() && lb(v, f).cond &&
            v.preds[size_t(f)].size() == 1 && f != v.procEnd) {
            const auto& fs = v.succ[size_t(f)];
            int n2 = nextLive(v, f);
            int other2 = -1;
            ExprPtr cond2;
            if (fs.size() >= 2 && fs[0] != fs[1]) {
                if (fs[0] == n2) { cond2 = lb(v, f).cond; other2 = fs[1]; }
                else if (fs[1] == n2) { cond2 = invertCond(lb(v, f).cond); other2 = fs[0]; }
            }
            if (other2 > n2 && other2 <= E && (other2 == E || v.preds[size_t(other2)].size() == 1)) {
                int e2 = other2 == E ? E : elseEnd(n2, other2);
                if (e2 == E) {
                    labelLine(addrOf(v, f));
                    line(ind, "ELSEIF " + printer_.cond(cond2) + " THEN");
                    thenStart = n2;
                    thenEnd = other2;
                    chained = true;
                    if (other2 == E) {
                        Ctx c2 = ctx;
                        c2.hi = E;
                        c2.follow = E;
                        region(v, thenStart, c2, ind + 1);
                        break;
                    }
                }
            }
        }
        if (chained) continue;
        line(ind, "ELSE");
        Ctx c2 = ctx;
        c2.hi = E;
        c2.follow = E;
        region(v, thenEnd, c2, ind + 1);
        break;
    }
    line(ind, "END IF");
    next = E >= 0 ? E : other;
    return true;
}

// SELECT CASE. The selector is copied to a hidden temporary T at the end of
// block i, then every clause is a run of tests followed by its body:
//
//     cmp T,v1 / jcc next / jmp body       one test per item; the last item
//     cmp T,v2 / jcc body / jmp nextClause jumps to the next clause instead
//     body ... / jmp end
//
// A range "a TO b" is "T >= a" (else the next item) followed by "T <= b".
// Without CASE ELSE the compiler adds "ERROR 39" (CASE ELSE expected) as the
// else part.
bool Emitter::trySelect(ProcView& v, int i, const Ctx& ctx, int ind, int& next) {
    if (term(v, i) != TermKind::Cond || !lb(v, i).cond || lb(v, i).stmts.empty()) return false;
    const Stmt& assign = lb(v, i).stmts.back();
    if (assign.kind != SK::Assign || !assign.lhs || assign.lhs->kind != EK::Var || !assign.rhs) return false;
    const uint16_t tv = assign.lhs->addr;
    const Ty tt = assign.lhs->type;
    auto isT = [&](const ExprPtr& e) {
        ExprPtr s = stripConv(e);
        return s && s->kind == EK::Var && s->addr == tv;
    };
    int tRefs = 0;
    auto countT = [&](const ExprPtr& e) {
        walkExpr(e, [&](const ExprPtr& x) {
            if (x->kind == EK::Var && x->addr == tv) ++tRefs;
        });
    };
    // "T op value" with T on the left
    auto parseTest = [&](const ExprPtr& c, Op& op, ExprPtr& val) {
        if (!c || c->kind != EK::Binary || !isCompare(c->op)) return false;
        bool l = isT(c->args[0]), r = isT(c->args[1]);
        if (l == r) return false;
        op = l ? c->op : swapCompare(c->op);
        val = l ? c->args[1] : c->args[0];
        bool refs = false;
        walkExpr(val, [&](const ExprPtr& x) {
            if (x->kind == EK::Var && x->addr == tv) refs = true;
        });
        return !refs;
    };
    auto isTest = [&](int b) {
        if (b < 0 || b >= v.n || !v.live[size_t(b)] || term(v, b) != TermKind::Cond || !lb(v, b).cond) return false;
        if (b != i && !lb(v, b).stmts.empty()) return false;
        const auto& s = v.succ[size_t(b)];
        if (s.size() < 2 || s[0] == s[1] || blk(v, b).succs.empty()) return false;
        Op op;
        ExprPtr val;
        return parseTest(lb(v, b).cond, op, val);
    };
    auto testTo = [&](int b, int target, Op& op, ExprPtr& val) { return parseTest(condTo(v, b, target), op, val); };
    auto valText = [&](const ExprPtr& e) { return printer_.expr(e, tt); };
    if (!isTest(i)) return false;

    // the block a test's "jmp" goes to (one forwarder followed, raw index)
    auto rawJmp = [&](int c) {
        int j = int(blk(v, c).succs.size() > 1 ? blk(v, c).succs[1] : blk(v, c).succs[0]);
        if (j >= 0 && j < v.n && !v.live[size_t(j)] && !blk(v, j).succs.empty()) j = int(blk(v, j).succs[0]);
        return j;
    };
    // Items of a clause with tests C that go to M (resolved) on a match and to F
    // (resolved) when nothing matches.
    auto decode = [&](const std::vector<int>& C, int M, int F, std::string& items) {
        items.clear();
        auto edges = [&](int c, int a, int b) {
            const auto& s = v.succ[size_t(c)];
            return (s[0] == a && s[1] == b) || (s[0] == b && s[1] == a);
        };
        for (size_t j = 0; j < C.size();) {
            int c = C[j];
            int nxt = j + 1 < C.size() ? C[j + 1] : F;
            Op op, op2;
            ExprPtr val, val2;
            if (!items.empty()) items += ", ";
            if (edges(c, M, nxt)) {
                if (!testTo(c, M, op, val)) return false;
                items += op == Op::Eq ? valText(val) : std::string("IS ") + opText(op) + " " + valText(val);
                ++j;
                continue;
            }
            // range: T >= a goes on to T <= b; failing either goes to the next item
            if (j + 1 >= C.size()) return false;
            int c2 = C[j + 1];
            int fail = j + 2 < C.size() ? C[j + 2] : F;
            if (!edges(c, c2, fail) || !edges(c2, M, fail)) return false;
            if (!testTo(c, c2, op, val) || op != Op::Ge || !testTo(c2, M, op2, val2) || op2 != Op::Le) return false;
            items += valText(val) + " TO " + valText(val2);
            j += 2;
        }
        return true;
    };

    struct Clause {
        std::string items;
        int body = -1;     // first block (may be a forwarder); -1: empty
        int bodyEnd = -1;  // next clause / else part
    };
    std::vector<Clause> clauses;
    std::vector<int> tests;
    int end = -1;             // raw block the bodies jump to
    bool endFromBody = false; // an empty body could be a GOTO; a real body confirms the end
    auto setEnd = [&](int e, bool fromBody) {
        if (end >= 0 && end != e) return false;
        end = e;
        endFromBody = endFromBody || fromBody;
        return true;
    };
    int c0 = i;
    for (int guard = 0; isTest(c0) && guard < 1000 && (end < 0 || c0 < end); ++guard) {
        // the run of tests: each test falls ("jcc" over the "jmp") into the next one
        std::vector<int> run{c0};
        for (;;) {
            int t = int(blk(v, run.back()).succs[0]);
            if (t <= run.back() || !isTest(t)) break;
            run.push_back(t);
        }
        Clause cl;
        size_t used = 0;
        // the body follows the tests
        int body = int(blk(v, run.back()).succs[0]);
        int bodyRes = resolve(v, body);
        int fail = v.succ[size_t(run.back())][1];
        if (body > run.back() && bodyRes < fail && fail > run.back() && decode(run, bodyRes, fail, cl.items)) {
            cl.body = body;
            cl.bodyEnd = fail;
            used = run.size();
            int last = prevLive(v, fail);
            if (last >= bodyRes && term(v, last) == TermKind::Jump && !lb(v, last).noJump &&
                !lb(v, last).returnWithTarget && !blk(v, last).succs.empty() && int(blk(v, last).succs[0]) > fail &&
                !setEnd(int(blk(v, last).succs[0]), true))
                return false;
        } else {
            // empty body: the matching tests jump straight to the end
            for (size_t k = 0; k < run.size() && !used; ++k) {
                int c = run[k];
                int f = resolve(v, int(blk(v, c).succs[0]));
                int m = v.succ[size_t(c)][0] == f ? v.succ[size_t(c)][1] : v.succ[size_t(c)][0];
                std::vector<int> part(run.begin(), run.begin() + long(k) + 1);
                if (m <= run.back() || !decode(part, m, f, cl.items)) continue;
                if (!setEnd(rawJmp(c), false)) return false;
                cl.bodyEnd = f;
                used = k + 1;
            }
            if (!used) return false;
        }
        for (size_t k = 0; k < used; ++k) {
            countT(lb(v, run[k]).cond);
            tests.push_back(run[k]);
        }
        clauses.push_back(cl);
        c0 = cl.bodyEnd;
    }
    if (clauses.empty() || end < 0 || c0 > end) return false;
    const int endRes = resolve(v, end);
    if (end > ctx.hi || (firstLive(v, end) >= ctx.hi && endRes != ctx.follow && firstLive(v, end) != ctx.hi))
        return false;
    if (!endFromBody && (c0 >= end || term(v, prevLive(v, end)) != TermKind::Fall)) return false;
    // T is only used by the tests, which are only reached from the SELECT
    if (pinfo_.varRefs[tv] != tRefs + 1) return false;
    for (int t : tests) {
        if (t == i) continue;
        for (int p : v.preds[size_t(t)])
            if (std::find(tests.begin(), tests.end(), p) == tests.end()) return false;
    }
    // else part: "ERROR 39" alone means there is no CASE ELSE
    int elseStart = firstLive(v, c0) < end ? c0 : -1;
    if (elseStart >= 0) {
        int e = firstLive(v, elseStart);
        const LiftedBlock& el = lb(v, e);
        if (firstLive(v, nextLive(v, e)) >= end && el.stmts.size() == 1 && term(v, e) == TermKind::Fall &&
            printer_.stmt(el.stmts[0]) == "ERROR 39" && v.preds[size_t(e)].size() == 1)
            elseStart = -1;
    }

    emitBody(v, i, ctx, ind, 1);
    line(ind, "SELECT CASE " + printer_.expr(assign.rhs, tt));
    for (const Clause& cl : clauses) {
        line(ind + 1, "CASE " + cl.items);
        Ctx c = ctx;
        c.hi = cl.bodyEnd;
        c.follow = endRes;
        c.skipTerm = -1;
        c.dropLast = -1;
        if (cl.body >= 0 && resolve(v, cl.body) < cl.bodyEnd) region(v, cl.body, c, ind + 2);
    }
    if (elseStart >= 0) {
        line(ind + 1, "CASE ELSE");
        Ctx c = ctx;
        c.hi = end;
        c.follow = endRes;
        c.skipTerm = -1;
        c.dropLast = -1;
        region(v, elseStart, c, ind + 2);
    }
    line(ind, "END SELECT");
    next = end;
    return true;
}

bool Emitter::tryLoop(ProcView& v, int i, const Ctx& ctx, int ind, int& next) {
    int latch = -1;
    for (int p : v.preds[size_t(i)])
        if (p >= i && p < ctx.hi && v.live[size_t(p)]) latch = std::max(latch, p);
    if (latch < 0) return false;
    int exitIdx = nextLive(v, latch);
    const auto& ls = v.succ[size_t(latch)];
    Ctx body = ctx;
    body.hi = latch + 1;
    body.follow = i;
    body.loopExit = exitIdx;
    body.loopIsFor = false;
    body.skipTerm = latch;
    body.loopHead = i;
    TermKind lt = term(v, latch);
    // "LOOP WHILE" can't carry a label: jumps into the test need an IF ... EXIT DO
    bool latchEnteredFromOutside = false;
    for (int p : v.preds[size_t(latch)])
        if (p < i || p > latch) latchEnteredFromOutside = true;
    if (lt == TermKind::Cond && latchEnteredFromOutside && lb(v, latch).stmts.empty()) {
        body.skipTerm = -1;
        line(ind, "DO");
        region(v, i, body, ind + 1);
        line(ind, "LOOP");
        next = exitIdx;
        return true;
    }
    if (lt == TermKind::Cond && ls.size() >= 2 && lb(v, latch).cond &&
        ((ls[0] == i && ls[1] == exitIdx) || (ls[1] == i && ls[0] == exitIdx))) {
        ExprPtr cont = condTo(v, latch, i);
        line(ind, "DO");
        region(v, i, body, ind + 1);
        line(ind, "LOOP WHILE " + printer_.cond(cont));
        next = exitIdx;
        return true;
    }
    if (lt == TermKind::Jump && !ls.empty() && ls[0] == i && !lb(v, latch).noJump && !lb(v, latch).returnWithTarget) {
        // pre-tested loop: the head only tests the condition
        const auto& hs = v.succ[size_t(i)];
        int bodyStart = nextLive(v, i);
        if (i != latch && term(v, i) == TermKind::Cond && lb(v, i).stmts.empty() && lb(v, i).cond && hs.size() >= 2 &&
            ((hs[0] == bodyStart && hs[1] == exitIdx) || (hs[1] == bodyStart && hs[0] == exitIdx)) && bodyStart <= latch) {
            ExprPtr c = condTo(v, i, bodyStart);
            labelLine(addrOf(v, i));
            line(ind, "DO WHILE " + printer_.cond(c));
            region(v, bodyStart, body, ind + 1);
            line(ind, "LOOP");
            next = exitIdx;
            return true;
        }
        line(ind, "DO");
        region(v, i, body, ind + 1);
        line(ind, "LOOP");
        next = exitIdx;
        return true;
    }
    return false;
}

namespace {

bool isVarRef(const ExprPtr& e, const ExprPtr& v) { return e && v && exprEqual(e, v); }

// rhs == v + step: returns step
ExprPtr incrementOf(const ExprPtr& v, const ExprPtr& rhs) {
    if (!rhs || rhs->kind != EK::Binary) return nullptr;
    if (rhs->op == Op::Add) {
        if (isVarRef(rhs->args[0], v)) return rhs->args[1];
        if (isVarRef(rhs->args[1], v)) return rhs->args[0];
    }
    if (rhs->op == Op::Sub && isVarRef(rhs->args[0], v)) {
        const ExprPtr& s = rhs->args[1];
        if (s->kind == EK::IntLit) return mkInt(-s->ival);
        if (s->kind == EK::SngLit) return mkSng(-s->dval);
        if (s->kind == EK::DblLit) return mkDbl(-s->dval);
        return mkUnary(Op::Neg, s, s->type);
    }
    return nullptr;
}

// cond is "v <= end" (dir 1) or "v >= end" (dir -1): returns end
ExprPtr limitOf(const ExprPtr& v, const ExprPtr& cond, int& dir) {
    if (!cond || cond->kind != EK::Binary || !isCompare(cond->op)) return nullptr;
    ExprPtr a = stripConv(cond->args[0]), b = stripConv(cond->args[1]);
    Op op = cond->op;
    if (isVarRef(b, v) && !isVarRef(a, v)) {
        std::swap(a, b);
        op = swapCompare(op);
    }
    if (!isVarRef(a, v)) return nullptr;
    if (op == Op::Le) dir = 1;
    else if (op == Op::Ge) dir = -1;
    else return nullptr;
    return b;
}

int literalSign(const ExprPtr& e) {
    ExprPtr s = stripConv(e);
    if (!s) return 0;
    if (s->kind == EK::IntLit) return s->ival > 0 ? 1 : s->ival < 0 ? -1 : 0;
    if (s->kind == EK::SngLit || s->kind == EK::DblLit) return s->dval > 0 ? 1 : s->dval < 0 ? -1 : 0;
    if (s->kind == EK::Unary && s->op == Op::Neg) return -literalSign(s->args[0]);
    return 0;
}

bool isOne(const ExprPtr& e) {
    ExprPtr s = stripConv(e);
    if (!s) return false;
    if (s->kind == EK::IntLit) return s->ival == 1;
    if (s->kind == EK::SngLit || s->kind == EK::DblLit) return s->dval == 1.0;
    return false;
}

} // namespace

bool Emitter::tryFor(ProcView& v, int i, const Ctx& ctx, int ind, int& next) {
    if (term(v, i) != TermKind::Jump || lb(v, i).noJump || lb(v, i).returnWithTarget) return false;
    const auto& is = v.succ[size_t(i)];
    if (is.empty()) return false;
    int C = is[0];
    int B0 = nextLive(v, i);
    if (C <= B0 || C >= ctx.hi || term(v, C) != TermKind::Cond || !lb(v, C).stmts.empty()) return false;
    int L = prevLive(v, C);
    if (L < B0) return false;
    if (!((term(v, L) == TermKind::Fall || term(v, L) == TermKind::Jump) && !v.succ[size_t(L)].empty() &&
          v.succ[size_t(L)].back() == C))
        return false;
    for (int p : v.preds[size_t(C)])
        if (p != i && p != L) return false;
    const auto& initStmts = lb(v, i).stmts;
    const auto& incStmts = lb(v, L).stmts;
    if (initStmts.empty() || incStmts.empty()) return false;
    const Stmt& init = initStmts.back();
    const Stmt& inc = incStmts.back();
    if (init.kind != SK::Assign || inc.kind != SK::Assign || !init.lhs || init.lhs->kind != EK::Var) return false;
    ExprPtr var = init.lhs;
    if (!isVarRef(inc.lhs, var)) return false;
    ExprPtr step = incrementOf(var, inc.rhs);
    if (!step) return false;

    // loop test: either one compare, or a test of the sign of a variable STEP
    const auto& cs = v.succ[size_t(C)];
    if (cs.size() < 2) return false;
    ExprPtr limit;
    int exitIdx = -1;
    int lastCond = C;
    int dir = 0;
    bool varStep = false;
    if (cs[0] == B0 || cs[1] == B0) {
        exitIdx = cs[0] == B0 ? cs[1] : cs[0];
        limit = limitOf(var, condTo(v, C, B0), dir);
        if (!limit) return false;
        int sg = literalSign(step);
        if (sg == 0 || sg != dir) return false;
    } else {
        // C: IF step < 0 THEN (v >= end) ELSE (v <= end)
        int a = cs[0], b = cs[1];
        for (int x : {a, b}) {
            if (x < 0 || term(v, x) != TermKind::Cond || !lb(v, x).stmts.empty()) return false;
            const auto& xs = v.succ[size_t(x)];
            if (xs.size() < 2 || (xs[0] != B0 && xs[1] != B0)) return false;
            int ex = xs[0] == B0 ? xs[1] : xs[0];
            if (exitIdx >= 0 && ex != exitIdx) return false;
            exitIdx = ex;
            int d = 0;
            ExprPtr lim = limitOf(var, condTo(v, x, B0), d);
            if (!lim) return false;
            if (limit && !exprEqual(stripConv(limit), stripConv(lim))) return false;
            limit = lim;
            if (v.preds[size_t(x)].size() != 1) return false;
            lastCond = std::max(lastCond, x);
        }
        varStep = true;
    }
    (void)varStep;

    // hidden temporaries for TO and STEP values assigned in the init block
    size_t drop = 1;
    auto inlineTemp = [&](ExprPtr& e, int refs) {
        ExprPtr s = stripConv(e);
        if (!s || s->kind != EK::Var) return;
        if (pinfo_.varRefs[s->addr] != refs) return;
        // the temp must be assigned in the init block just before the counter
        for (size_t k = initStmts.size() - drop; k-- > 0;) {
            const Stmt& a = initStmts[k];
            if (a.kind == SK::Assign && a.lhs && a.lhs->kind == EK::Var && a.lhs->addr == s->addr) {
                if (k + drop + 1 != initStmts.size()) return; // must be adjacent
                e = a.rhs;
                ++drop;
                return;
            }
            break;
        }
    };
    for (int round = 0; round < 2; ++round) {
        inlineTemp(step, varStep ? 3 : 2);
        inlineTemp(limit, 2);
    }

    emitBody(v, i, ctx, ind, drop);
    std::string head = "FOR " + printer_.expr(var) + " = " + printer_.expr(init.rhs, var->type) + " TO " +
                       printer_.expr(limit, var->type);
    if (!isOne(step)) head += " STEP " + printer_.expr(step, var->type);
    line(ind, head);
    Ctx body = ctx;
    body.hi = C;
    body.follow = C;
    body.loopExit = exitIdx;
    body.loopIsFor = true;
    body.dropLast = L;
    region(v, B0, body, ind + 1);
    line(ind, "NEXT " + printer_.expr(var));
    int after = nextLive(v, lastCond);
    if (exitIdx != after) {
        Ctx c = ctx;
        line(ind, jumpText(v, exitIdx, c));
    }
    next = after;
    return true;
}

void Emitter::region(ProcView& v, int lo, const Ctx& ctx, int ind) {
    int i = firstLive(v, lo);
    while (i < ctx.hi && i < v.n) {
        int next;
        if (tryFor(v, i, ctx, ind, next)) {
            i = next;
            continue;
        }
        if (i != ctx.skipTerm && i != ctx.loopHead && tryLoop(v, i, ctx, ind, next)) {
            i = next;
            continue;
        }
        if (i != ctx.skipTerm && trySelect(v, i, ctx, ind, next)) {
            i = next;
            continue;
        }
        emitBody(v, i, ctx, ind);
        if (i == ctx.skipTerm) {
            i = nextLive(v, i);
            continue;
        }
        if (tryIf(v, i, ctx, ind, next)) {
            i = next;
            continue;
        }
        emitTerm(v, i, ctx, ind);
        i = nextLive(v, i);
    }
}

std::vector<std::string> Emitter::sharedList(uint32_t pi) {
    std::vector<std::string> shared;
    for (const auto& kv : pinfo_.varProcs) {
        if (!kv.second.count(pi)) continue;
        if (kv.second.size() < 2) continue;
        shared.push_back(names_.var(kv.first, pinfo_.varTypes[kv.first]));
    }
    for (const auto& kv : pinfo_.arrayProcs) {
        if (!kv.second.count(pi)) continue;
        if (!pinfo_.arrays.count(kv.first)) continue;
        shared.push_back(names_.array(kv.first, Ty::Unknown) + "()");
    }
    return shared;
}

std::string Emitter::subHeader(const Proc& p) {
    std::string s = "SUB " + p.name;
    auto it = info_.subs.find(p.entry);
    int nparams = 0;
    std::vector<SubParam> params;
    if (it != info_.subs.end()) params = it->second.params;
    nparams = int(params.size());
    if (nparams == 0 && p.retfBytes > 0) {
        nparams = p.retfBytes / 2;
        params.resize(size_t(nparams));
    }
    if (nparams > 0) {
        s += " (";
        for (int k = 0; k < nparams; ++k) {
            if (k) s += ", ";
            const SubParam& sp = params[size_t(k)];
            Ty t = sp.type == Ty::Unknown ? Ty::Sng : sp.type;
            s += names_.param(k, t);
            if (sp.isArray) s += "(" + std::to_string(std::max(sp.dims, 1)) + ")";
        }
        s += ")";
    }
    return s + " STATIC";
}

void Emitter::emitProc(uint32_t pi, int ind) {
    ProcView v = makeView(pi);
    cur_ = &v;
    const Proc& p = *v.proc;
    if (p.kind == ProcKind::DefFnSingle) {
        auto it = info_.fns.find(p.entry);
        if (it == info_.fns.end()) return;
        const FnInfo& fi = it->second;
        std::string s = "DEF " + names_.fn(p.entry);
        if (!fi.params.empty()) {
            s += " (";
            for (size_t k = 0; k < fi.params.size(); ++k) {
                if (k) s += ", ";
                s += names_.var(fi.params[k], fi.paramTypes[k]);
            }
            s += ")";
        }
        s += " = " + printer_.expr(fi.body, fi.type);
        line(ind, s);
        return;
    }
    Ctx ctx;
    ctx.hi = v.n;
    ctx.follow = v.n;
    int bodyInd = ind;
    if (p.kind == ProcKind::Sub) {
        line(ind, subHeader(p));
        std::vector<std::string> sh = sharedList(pi);
        for (size_t k = 0; k < sh.size(); k += 8) {
            std::string s = "SHARED ";
            for (size_t j = k; j < sh.size() && j < k + 8; ++j) {
                if (j > k) s += ", ";
                s += sh[j];
            }
            line(ind + 1, s);
        }
        bodyInd = ind + 1;
    } else if (p.kind == ProcKind::DefFn) {
        auto it = info_.fns.find(p.entry);
        std::string s = "DEF " + names_.fn(p.entry);
        if (it != info_.fns.end() && !it->second.params.empty()) {
            s += " (";
            for (size_t k = 0; k < it->second.params.size(); ++k) {
                if (k) s += ", ";
                s += names_.var(it->second.params[k], it->second.paramTypes[k]);
            }
            s += ")";
        }
        line(ind, s);
        bodyInd = ind + 1;
    }
    region(v, 0, ctx, bodyInd);
    if (p.kind == ProcKind::Sub) line(ind, "END SUB");
    if (p.kind == ProcKind::DefFn) line(ind, "END DEF");
    cur_ = nullptr;
}

namespace {

// Breaks a line longer than QB accepts. A statement continues on the next line
// after " _" (wherever there could be a space, but not in a string); the
// continuation is indented a little more. DATA can't be continued (the rest of
// the line is data), so a long DATA line becomes several DATA statements.
// Returns false when there is nowhere to break.
bool breakLine(const std::string& line, std::vector<std::string>& out) {
    if (int(line.size()) <= kMaxLine) {
        out.push_back(line);
        return true;
    }
    size_t indent = line.find_first_not_of(' ');
    if (indent == std::string::npos) indent = 0;
    // the statement starts after an optional line number
    size_t stmt = indent;
    while (stmt < line.size() && std::isdigit(static_cast<unsigned char>(line[stmt]))) ++stmt;
    if (stmt > indent && stmt < line.size() && line[stmt] == ' ') ++stmt;
    else stmt = indent;
    if (line.compare(stmt, 1, "'") == 0 || line.compare(stmt, 3, "REM") == 0) {
        out.push_back(line);
        return false;
    }

    if (line.compare(stmt, 5, "DATA ") == 0) {
        // the last comma outside quotes that keeps the first part short enough
        size_t cut = std::string::npos;
        bool quoted = false;
        for (size_t i = stmt + 5; i < line.size() && int(i) < kMaxLine; ++i) {
            if (line[i] == '"') quoted = !quoted;
            else if (line[i] == ',' && !quoted) cut = i;
        }
        if (cut == std::string::npos) {
            out.push_back(line);
            return false;
        }
        size_t rest = line.find_first_not_of(' ', cut + 1);
        out.push_back(line.substr(0, cut));
        return breakLine(std::string(indent, ' ') + "DATA " + (rest == std::string::npos ? "" : line.substr(rest)), out);
    }

    // Break at a space outside strings, preferably after a list separator or
    // next to an operator; the first part (with " _") must fit.
    auto isOp = [](char c) { return std::strchr("+-*/\\^=<>", c) != nullptr; };
    auto wordAt = [&](size_t p, const char* w) {
        size_t n = std::strlen(w);
        return line.compare(p, n, w) == 0 && (p + n == line.size() || line[p + n] == ' ');
    };
    auto wordBefore = [&](size_t p, const char* w) {
        size_t n = std::strlen(w);
        return p >= n && line.compare(p - n, n, w) == 0 && (p == n || line[p - n - 1] == ' ');
    };
    static const char* kWords[] = {"AND", "OR", "XOR", "EQV", "IMP", "MOD", "THEN", "ELSE", "TO", "STEP"};
    size_t good = std::string::npos, any = std::string::npos;
    bool quoted = false;
    size_t firstSpace = line.find(' ', stmt);
    for (size_t i = stmt; i < line.size() && int(i) + 2 <= kMaxLine; ++i) {
        if (line[i] == '"') quoted = !quoted;
        if (quoted || line[i] != ' ' || i <= firstSpace || i + 1 >= line.size() || line[i + 1] == ' ') continue;
        any = i;
        char prev = line[i - 1], next = line[i + 1];
        bool pref = prev == ',' || prev == ';' || isOp(prev) || isOp(next);
        for (const char* w : kWords)
            if (wordAt(i + 1, w) || wordBefore(i, w)) pref = true;
        if (pref) good = i;
    }
    size_t cut = good != std::string::npos ? good : any;
    if (cut == std::string::npos) {
        out.push_back(line);
        return false;
    }
    out.push_back(line.substr(0, cut) + " _");
    return breakLine(std::string(indent + 4, ' ') + line.substr(cut + 1), out);
}

// The highest variable address ("variXXXX") a line mentions, -1 if none.
int maxVarRef(const std::string& s) {
    int best = -1;
    for (size_t p = s.find("vari"); p != std::string::npos; p = s.find("vari", p + 1)) {
        if (p > 0 && (std::isalnum(static_cast<unsigned char>(s[p - 1])) || s[p - 1] == '_')) continue;
        if (p + 8 > s.size()) break;
        bool hex = true;
        for (size_t k = p + 4; k < p + 8; ++k)
            if (!std::isdigit(static_cast<unsigned char>(s[k])) && (s[k] < 'A' || s[k] > 'F')) hex = false;
        if (!hex || (p + 8 < s.size() && std::isalnum(static_cast<unsigned char>(s[p + 8])))) continue;
        best = std::max(best, int(std::stoul(s.substr(p + 4, 4), nullptr, 16)));
    }
    return best;
}

} // namespace

// Puts each DIM in front of the first line that uses the array or something
// allocated after it. Arrays first used in a SUB are declared at the end of the
// main program (a DIM in a SUB would make them local), ones used in a DEF FN
// before the DEF.
void Emitter::placeDims(const std::vector<std::pair<uint16_t, std::string>>& dims, std::vector<OutLine>& fnLines,
                        std::vector<OutLine>& mainLines) {
    if (dims.empty()) return;
    std::map<size_t, std::vector<OutLine>> atFn, atMain;
    // insertion point at the end of the main program: before trailing labels
    size_t mainEnd = mainLines.size();
    while (mainEnd > 0 && mainLines[mainEnd - 1].label != -1) --mainEnd;
    for (const auto& d : dims) {
        uint16_t base = d.first;
        bool placed = false;
        for (size_t i = 0; i < fnLines.size() && !placed; ++i) {
            if (fnLines[i].label != -1 || maxVarRef(fnLines[i].text) < base) continue;
            size_t j = i;
            while (j > 0 && !(fnLines[j].indent == 0 && fnLines[j].text.rfind("DEF ", 0) == 0)) --j;
            atFn[j].push_back({0, d.second, -1});
            placed = true;
        }
        for (size_t i = 0; i < mainLines.size() && !placed; ++i) {
            if (mainLines[i].label != -1 || maxVarRef(mainLines[i].text) < base) continue;
            // keep the labels and line numbers of the line in front of it
            size_t j = i;
            while (j > 0 && mainLines[j - 1].label != -1) --j;
            atMain[j].push_back({mainLines[i].indent, d.second, -1});
            placed = true;
        }
        if (!placed) atMain[mainEnd].push_back({0, d.second, -1});
    }
    auto apply = [](std::vector<OutLine>& ls, const std::map<size_t, std::vector<OutLine>>& at) {
        std::vector<OutLine> r;
        for (size_t i = 0; i <= ls.size(); ++i) {
            auto it = at.find(i);
            if (it != at.end()) r.insert(r.end(), it->second.begin(), it->second.end());
            if (i < ls.size()) r.push_back(ls[i]);
        }
        ls = std::move(r);
    };
    apply(fnLines, atFn);
    apply(mainLines, atMain);
}

std::string Emitter::run(const DecompileOptions& opt, const std::string& inputName) {
    const LoadedProgram& prog = program_.prog();
    std::vector<OutLine> header;
    // compiler switches the program needs
    bool onError = false, resumeNext = false;
    for (const auto& kv : program_.dis().insns()) {
        const Insn& in = kv.second;
        if (!in.isRt) continue;
        if (in.rt.is(0x3f, 0x02) || in.rt.is(0x3f, 0x09)) onError = true;
        if (in.rt.is(0x3e, 0x12) || in.rt.is(0x3e, 0x13)) resumeNext = true;
    }
    // the compiler switches recorded in the header (and those the code needs)
    std::string opts;
    if (prog.debugBuild()) opts += " /D";
    if (prog.optE() || onError || resumeNext) opts += " /E";
    if (prog.opcodePadding()) opts += " /Q"; // optimise for speed (undocumented switch)
    if (prog.optR()) opts += " /R";
    if (prog.optS()) opts += " /S";
    if (prog.optV()) opts += " /V";
    else if (prog.eventTrapping()) opts += " /W";
    if (prog.optX() || resumeNext) opts += " /X";
    if (opt.comments) {
        header.push_back({0, "' Decompiled from " + inputName + " by qb30decomp", -1});
        if (!prog.sourceName.empty()) header.push_back({0, "' Original source: " + prog.sourceName, -1});
    }
    if (!opts.empty()) header.push_back({0, "' QBOPTS:" + opts, -1});
    // Static arrays. QB allocates an array at its DIM and a variable where it is
    // first used, so an array outside COMMON is declared just before the first
    // line that uses something allocated after it: the program keeps its memory
    // layout. With $DYNAMIC all static arrays come first (before it takes effect).
    bool anyDynamic = false;
    for (const auto& kv : pinfo_.arrays)
        if (kv.second.dynamic) anyDynamic = true;
    std::vector<std::pair<uint16_t, std::string>> lateDims;
    for (const auto& kv : pinfo_.arrays) {
        const ArrayInfo& ai = kv.second;
        if (ai.dynamic) continue;
        std::string s = "DIM " + names_.array(ai.base, ai.type) + "(";
        std::vector<int> dims = ai.dims;
        if (prog.optR()) std::reverse(dims.begin(), dims.end());
        for (size_t d = 0; d < dims.size(); ++d) {
            if (d) s += ", ";
            s += std::to_string(dims[d] - 1);
        }
        s += ")";
        bool inCommon = prog.commonSize && ai.base >= prog.commonStart && ai.base < prog.commonEnd();
        if (anyDynamic || inCommon) {
            header.push_back({0, s, -1});
            continue;
        }
        lateDims.push_back({ai.base, s});
        // Storage after the array that the program never uses (variables only
        // declared, ...) is reserved, so that arrays stay apart as they were.
        uint32_t end = uint32_t(ai.base) + ai.bytes, next = prog.dsEnd;
        auto below = [&](uint32_t a) {
            if (a >= end && a < next) next = a;
        };
        for (const auto& v : pinfo_.varTypes) below(v.first);
        for (const auto& a : pinfo_.arrays) below(a.first);
        for (const auto& f : info_.fns) {
            for (uint16_t p : f.second.params) below(p);
            if (f.second.hasResultVar) below(f.second.resultVar);
        }
        for (uint32_t a = end; a < next; ++a)
            if (prog.dsInit[a]) {
                next = a;
                break;
            }
        if (next < prog.dsEnd && next > end && (next - end) % 2 == 0)
            lateDims.push_back({ai.base, "DIM vari" + hex4(uint16_t(end)) + "%(" + std::to_string((next - end) / 2 - 1) + ")"});
    }
    // COMMON: the block CHAIN keeps must have the same layout, so everything in it
    // is listed in address order and gaps get placeholder variables
    if (prog.commonSize) {
        const uint32_t cs = prog.commonStart, ce = prog.commonEnd();
        struct Item {
            uint32_t addr, size;
            std::string name;
        };
        std::vector<Item> items;
        for (const auto& kv : pinfo_.arrays) {
            const ArrayInfo& ai = kv.second;
            if (!ai.dynamic && ai.base >= cs && ai.base < ce)
                items.push_back({ai.base, ai.bytes, names_.array(ai.base, ai.type) + "()"});
        }
        for (const auto& kv : pinfo_.varTypes) {
            if (kv.first < cs || kv.first >= ce) continue;
            Ty t = kv.second == Ty::Unknown ? Ty::Sng : kv.second;
            uint32_t size = t == Ty::Int ? 2 : t == Ty::Dbl ? 8 : 4;
            items.push_back({kv.first, size, names_.var(kv.first, t)});
        }
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.addr < b.addr; });
        std::vector<std::string> list;
        auto plainFill = [&](uint32_t a, uint32_t e) {
            while (e - a >= 2) {
                uint32_t g = e - a;
                uint16_t x = uint16_t(a);
                if (g >= 16) {
                    uint32_t n = g / 2;
                    header.push_back({0, "DIM vari" + hex4(x) + "%(" + std::to_string(n - 1) + ")", -1});
                    list.push_back("vari" + hex4(x) + "%()");
                    a += 2 * n;
                } else if (g >= 8) {
                    list.push_back(names_.var(x, Ty::Dbl));
                    a += 8;
                } else if (g >= 4) {
                    list.push_back(names_.var(x, Ty::Sng));
                    a += 4;
                } else {
                    list.push_back(names_.var(x, Ty::Int));
                    a += 2;
                }
            }
            if (a != e) warnings_.push_back("COMMON block has a gap of odd size at " + hex4(uint16_t(a)));
        };
        // a gap is declared like the programs sharing the block declare it, if known
        std::vector<CommonItem> hints = info_.commonHints;
        std::sort(hints.begin(), hints.end(), [](const CommonItem& x, const CommonItem& y) {
            if (x.addr != y.addr) return x.addr < y.addr;
            if (x.array != y.array) return x.array;
            return x.size > y.size;
        });
        auto fill = [&](uint32_t a, uint32_t e) {
            for (const CommonItem& h : hints) {
                if (h.addr < a || h.addr + h.size > e || h.size == 0 || h.type == Ty::Unknown) continue;
                if (h.array) {
                    uint32_t n = h.type == Ty::Int ? 2 : h.type == Ty::Dbl ? 8 : 4;
                    for (int d : h.dims) n *= uint32_t(d);
                    if (h.dims.empty() || n != h.size) continue;
                }
                if (h.addr > a) plainFill(a, h.addr);
                if (h.array) {
                    std::string d = "DIM " + names_.array(h.addr, h.type) + "(";
                    std::vector<int> hd = h.dims;
                    if (prog.optR()) std::reverse(hd.begin(), hd.end());
                    for (size_t k = 0; k < hd.size(); ++k) d += (k ? ", " : "") + std::to_string(hd[k] - 1);
                    header.push_back({0, d + ")", -1});
                    list.push_back(names_.array(h.addr, h.type) + "()");
                } else {
                    list.push_back(names_.var(h.addr, h.type));
                }
                a = h.addr + h.size;
            }
            if (a < e) plainFill(a, e);
        };
        uint32_t p = cs;
        for (const Item& it : items) {
            if (it.addr < p) continue;
            if (it.addr > p) fill(p, it.addr);
            list.push_back(it.name);
            p = it.addr + it.size;
        }
        if (p < ce) fill(p, ce);
        for (const auto& kv : pinfo_.arrays)
            if (kv.second.dynamic && kv.first >= cs && kv.first < ce)
                warnings_.push_back("$DYNAMIC array in COMMON is not supported");
        std::string s;
        for (const std::string& n : list) {
            if (!s.empty() && int(s.size() + n.size()) + 2 > kMaxLine - 6) {
                header.push_back({0, s, -1});
                s.clear();
            }
            s += s.empty() ? "COMMON " + n : ", " + n;
        }
        if (!s.empty()) header.push_back({0, s, -1});
    }
    if (anyDynamic) header.push_back({0, "REM $DYNAMIC", -1});

    for (uint32_t pi = 0; pi < program_.procs().size(); ++pi) makeView(pi);
    // The compiler drops statements after GOTO/RETURN/END up to the next label or
    // line number, so unreachable code keeps a label to survive recompilation.
    for (const Proc& p : program_.procs()) deadLabels_.insert(p.deadRoots.begin(), p.deadRoots.end());
    // DEF FN definitions come first so that they precede their use
    std::vector<OutLine> fnLines;
    for (uint32_t pi = 0; pi < program_.procs().size(); ++pi) {
        ProcKind k = program_.procs()[pi].kind;
        if (k != ProcKind::DefFn && k != ProcKind::DefFnSingle) continue;
        out_.clear();
        emitProc(pi, 0);
        fnLines.insert(fnLines.end(), out_.begin(), out_.end());
    }
    out_.clear();
    emitProc(0, 0);
    std::vector<OutLine> mainLines = out_;
    std::vector<OutLine> subLines;
    for (uint32_t pi = 0; pi < program_.procs().size(); ++pi) {
        if (program_.procs()[pi].kind != ProcKind::Sub) continue;
        out_.clear();
        subLines.push_back({0, "", -1});
        emitProc(pi, 0);
        subLines.insert(subLines.end(), out_.begin(), out_.end());
    }

    std::string text;
    // A line number goes in front of the next statement. Ones that can't (several
    // at the same place, before a label or a blank line) get a line of their own.
    std::set<uint16_t> doneLines;
    std::vector<uint16_t> pend;
    auto flushBare = [&]() {
        for (uint16_t n : pend) text += std::to_string(n) + "\n";
        pend.clear();
    };
    auto emitLines = [&](const std::vector<OutLine>& ls) {
        for (const OutLine& l : ls) {
            for (uint16_t n : l.lineNos)
                if (doneLines.insert(n).second) pend.push_back(n);
            if (l.label == -2) continue;
            if (l.label >= 0) {
                if (l.lineNos.empty() && (refLabels_.count(uint16_t(l.label)) || deadLabels_.count(uint16_t(l.label)))) {
                    flushBare();
                    text += "loc" + hex4(uint16_t(l.label)) + ":\n";
                }
                continue;
            }
            if (l.text.empty()) {
                flushBare();
                text += "\n";
                continue;
            }
            std::string prefix;
            if (!pend.empty()) {
                prefix = std::to_string(pend.back()) + " ";
                pend.pop_back();
                flushBare();
            }
            std::string s(size_t(l.indent) * 2, ' ');
            s += prefix + l.text;
            std::vector<std::string> parts;
            if (!breakLine(s, parts))
                warnings_.push_back("line longer than " + std::to_string(kMaxLine) + " characters: " + s.substr(0, 60) + "...");
            for (const std::string& p : parts) text += p + "\n";
        }
        flushBare();
    };
    placeDims(lateDims, fnLines, mainLines);
    // QB keeps the DEF SEG segment across CHAIN. A program built differently from
    // the one that chains to it gets its data at another address, so BLOAD,
    // BSAVE, PEEK and POKE would go to the old program's memory: start with the
    // program's own data segment
    {
        bool segUse = false;
        for (const auto& kv : program_.dis().insns()) {
            const Insn& in = kv.second;
            if (in.isRt && (in.rt.is(0x3e, 0x1a) || in.rt.is(0x3e, 0x1b) || in.rt.is(0x3d, 0x62) ||
                            in.rt.is(0x3e, 0xa5) || in.rt.is(0x3f, 0x00)))
                segUse = true;
            if (!in.isRt && in.zi.mnemonic == ZYDIS_MNEMONIC_CALL && program_.externalName(in) == "ABSOLUTE")
                segUse = true;
        }
        size_t first = 0;
        while (first < mainLines.size() && mainLines[first].label != -1) ++first;
        bool hasReset = first < mainLines.size() && mainLines[first].text == "DEF SEG";
        if (segUse && !hasReset) {
            std::vector<OutLine> reset;
            if (opt.comments) reset.push_back({0, "' QB keeps DEF SEG across CHAIN: use this program's data", -1});
            reset.push_back({0, "DEF SEG", -1});
            mainLines.insert(mainLines.begin(), reset.begin(), reset.end());
        }
    }
    // unreachable code at the very end is just the program end: no label needed
    while (!mainLines.empty() && mainLines.back().label >= 0 && mainLines.back().lineNos.empty() &&
           !refLabels_.count(uint16_t(mainLines.back().label)))
        mainLines.pop_back();
    emitLines(header);
    emitLines(fnLines);
    emitLines(mainLines);
    // a label at the very end of the main program needs a statement
    if (!mainLines.empty() && mainLines.back().label >= 0 && refLabels_.count(uint16_t(mainLines.back().label)))
        text += "END\n";
    emitLines(subLines);
    return text;
}

} // namespace

std::string emitProgram(Program& program, std::vector<std::vector<LiftedBlock>>& results, GlobalInfo& info,
                        ProgramInfo& pinfo, const DecompileOptions& opt, const std::string& inputName,
                        std::vector<std::string>& warnings) {
    Emitter e(program, results, info, pinfo, warnings);
    return e.run(opt, inputName);
}

} // namespace qb
