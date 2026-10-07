#include "cfg.h"

#include "opcodes.h"

#include <algorithm>
#include <map>
#include <cstdio>
#include <cstring>

namespace qb {

namespace {

bool isReg16(const ZydisDecodedOperand& op) {
    return op.type == ZYDIS_OPERAND_TYPE_REGISTER && op.size == 16;
}

bool isRegOp(const Insn& in, int i, ZydisRegister r) {
    return in.zi.operand_count_visible > i && in.ops[i].type == ZYDIS_OPERAND_TYPE_REGISTER && in.ops[i].reg.value == r;
}

ZydisRegister decReg(const Insn& in) {
    if (in.isRt || in.zi.mnemonic != ZYDIS_MNEMONIC_DEC || in.length != 1) return ZYDIS_REGISTER_NONE;
    if (!isReg16(in.ops[0])) return ZYDIS_REGISTER_NONE;
    return in.ops[0].reg.value;
}

// "and r,r" / "or r,r": returns r
ZydisRegister selfTestReg(const Insn& in) {
    if (in.isRt) return ZYDIS_REGISTER_NONE;
    if (in.zi.mnemonic != ZYDIS_MNEMONIC_AND && in.zi.mnemonic != ZYDIS_MNEMONIC_OR) return ZYDIS_REGISTER_NONE;
    if (!isReg16(in.ops[0]) || !isReg16(in.ops[1]) || in.ops[0].reg.value != in.ops[1].reg.value)
        return ZYDIS_REGISTER_NONE;
    return in.ops[0].reg.value;
}

bool isTerminalRt(const RtCall& rt) {
    if (opIsTerminal(rt.intNo, rt.op)) return true;
    if (rt.is(0x3f, 0x3a)) return true; // DEF FN end returns to the caller
    return false;
}

} // namespace

const Block* Proc::blockFor(uint16_t addr) const {
    auto it = blockAt.find(addr);
    return it == blockAt.end() ? nullptr : &blocks[it->second];
}

Program::Program(const LoadedProgram& prog, const Disassembly& dis) : prog_(prog), dis_(dis) {
    classify();

    // SUBs and functions first, so that main gets whatever is left.
    std::set<uint16_t> claimed;
    std::vector<uint16_t> entries;
    for (uint16_t a : subs_) entries.push_back(a);
    for (uint16_t a : fns_) entries.push_back(a);
    std::sort(entries.begin(), entries.end());
    for (uint16_t a : entries) {
        Proc p;
        p.entry = a;
        char buf[32];
        if (subs_.count(a)) {
            p.kind = ProcKind::Sub;
            std::snprintf(buf, sizeof buf, "sub%04X", a);
        } else {
            const Insn* first = dis_.at(a);
            p.kind = (first && first->isRt && first->rt.is(0x3f, 0x39)) ? ProcKind::DefFn : ProcKind::DefFnSingle;
            std::snprintf(buf, sizeof buf, "FNf%04X", a);
        }
        p.name = buf;
        buildProc(p, {a}, nullptr);
        procs_.push_back(std::move(p));
    }
    for (const Proc& p : procs_)
        for (const Block& b : p.blocks)
            for (const LInsn& li : b.code)
                for (uint16_t x = li.addr; x < li.end; ++x) claimed.insert(x);

    Proc mainp;
    mainp.kind = ProcKind::Main;
    mainp.entry = prog_.codeStart;
    mainp.name = "main";
    std::set<uint16_t> roots = {prog_.codeStart};
    for (uint16_t t : dis_.trapTargets()) roots.insert(t);
    // Unclaimed instructions (dead code) also belong to main (see buildProc).
    buildProc(mainp, roots, &claimed);
    procs_.insert(procs_.begin(), std::move(mainp));
}

const Proc* Program::procAt(uint16_t entry) const {
    for (const Proc& p : procs_)
        if (p.entry == entry) return &p;
    return nullptr;
}

std::string Program::externalName(const Insn& call) const {
    if (call.isRt || call.zi.opcode != 0x9A) return "";
    const ZydisDecodedOperand& op = call.ops[0];
    if (op.type != ZYDIS_OPERAND_TYPE_POINTER) return "";
    // The routine lives in the load module at seg:off (segment relative to the module start).
    size_t off = size_t(op.ptr.segment) * 16 + op.ptr.offset;
    static const uint8_t kAbsolute[] = {0x5F, 0x5E, 0x5B, 0x56, 0x57, 0xFF, 0x36, 0x00, 0x00, 0xFF, 0x37, 0xCB};
    if (off + sizeof kAbsolute <= prog_.code.size() && std::memcmp(&prog_.code[off], kAbsolute, sizeof kAbsolute) == 0)
        return "ABSOLUTE";
    char buf[32];
    std::snprintf(buf, sizeof buf, "ext%04X%04X", op.ptr.segment, op.ptr.offset);
    return buf;
}

void Program::classify() {
    for (uint16_t a : dis_.farCallTargets()) subs_.insert(a);
    for (uint16_t a : dis_.callTargets()) {
        const Insn* first = dis_.at(a);
        if (!first) continue;
        if (first->isRt && first->rt.is(0x3f, 0x53)) {
            subs_.insert(a);
            continue;
        }
        if (first->isRt && first->rt.is(0x3f, 0x39)) {
            fns_.insert(a);
            continue;
        }
    }
    // Single line DEF FN: code that is jumped around, only computes a value
    // (possibly calling other functions) and returns with RET, and whose
    // callers use the result left in AX or the FAC.
    std::map<uint16_t, std::vector<const Insn*>> callSites;
    for (const auto& kv : dis_.insns()) {
        uint16_t t;
        if (kv.second.isCall() && kv.second.branchTarget(t)) callSites[t].push_back(&kv.second);
    }
    auto resultUsed = [&](const Insn* call) {
        const Insn* in = dis_.at(call->next());
        // (the compiler may set up other operands first)
        bool axDead = false;
        for (int k = 0; in && k < 12; ++k) {
            if (in->isRt) {
                const RtCall& rt = in->rt;
                // opcodes that take the FAC as an operand
                if (rt.intNo == 0x3f &&
                    (rt.op == 0x71 || rt.op == 0x72 || rt.op == 0x74 || rt.op == 0x77 || rt.op == 0x78 ||
                     rt.op == 0x7a || rt.op == 0x7d || rt.op == 0x7e || rt.op == 0x1b || rt.op == 0x1c ||
                     rt.op == 0x1f || rt.op == 0x20 || rt.op == 0x22 || rt.op == 0x2d || rt.op == 0x2e ||
                     rt.op == 0x31 || rt.op == 0x32 || rt.op == 0xa9 || rt.op == 0xaa || rt.op == 0xad ||
                     rt.op == 0xae || rt.op == 0xb1 || rt.op == 0xb2 ||
                     (rt.op >= 0x81 && rt.op <= 0x86) || (rt.op >= 0x89 && rt.op <= 0x8e) ||
                     (rt.op >= 0x91 && rt.op <= 0x96) || (rt.op >= 0x99 && rt.op <= 0x9e) ||
                     (rt.op >= 0xa1 && rt.op <= 0xa6) || (rt.op >= 0x25 && rt.op <= 0x2a)))
                    return true;
                return false;
            }
            // reads of AX, or BX pointing at the FAC
            bool axWritten = false;
            for (int i = 0; i < in->zi.operand_count_visible; ++i) {
                const ZydisDecodedOperand& op = in->ops[i];
                if (op.type == ZYDIS_OPERAND_TYPE_REGISTER && op.reg.value == ZYDIS_REGISTER_AX &&
                    (op.actions & ZYDIS_OPERAND_ACTION_MASK_READ) && !axDead)
                    return true;
                if (op.type == ZYDIS_OPERAND_TYPE_REGISTER && op.reg.value == ZYDIS_REGISTER_AX &&
                    (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE))
                    axWritten = true;
                if (op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && (op.imm.value.u == 0x1a || op.imm.value.u == 0x16) &&
                    in->zi.mnemonic == ZYDIS_MNEMONIC_MOV)
                    return true;
            }
            if (axWritten) axDead = true;
            if (in->isJmp() || in->isJcc() || in->isCall() || in->isRet()) return false;
            in = dis_.at(in->next());
        }
        return false;
    };
    std::set<uint16_t> candidates, shaped;
    std::map<uint16_t, std::vector<uint16_t>> innerCalls;
    for (uint16_t a : dis_.callTargets()) {
        if (subs_.count(a) || fns_.count(a)) continue;
        // Instruction before must be a JMP over the function.
        const Insn* prev = nullptr;
        for (const auto& kv : dis_.insns()) {
            if (kv.second.next() == a) prev = &kv.second;
        }
        if (!prev || !prev->isJmp()) continue;
        uint16_t over;
        if (!prev->branchTarget(over) || over <= a) continue;
        bool pure = true, sawRet = false, computes = false;
        uint16_t pc = a;
        while (pc < over) {
            const Insn* in = dis_.at(pc);
            if (!in) { pure = false; break; }
            if (in->isRet()) {
                sawRet = true;
                if (in->next() != over) pure = false;
                break;
            }
            if (in->isRt) {
                const RtCall& rt = in->rt;
                // statements (PRINT, END, ...) and control flow are not allowed
                if (rt.intNo == 0x3e) { pure = false; break; }
                if (rt.intNo == 0x3f) {
                    int op = rt.op;
                    bool exprOp = (op >= 0x15 && op <= 0x32) || op == 0x3b || op == 0x55 || op == 0x56 || op == 0x57 ||
                                  (op >= 0x6f && op <= 0xb2) || op == 0xba || op == 0xbb || op == 0xc4 ||
                                  op == 0xc9 || op == 0xca || op == 0xcb || op == 0x3c || op == 0x41 || op == 0x61;
                    if (!exprOp) { pure = false; break; }
                }
                computes = true;
            } else {
                uint16_t t;
                if (in->isJmp() || in->isRetf()) { pure = false; break; }
                if (in->isCall() && !(in->branchTarget(t) && t != a)) { pure = false; break; }
                if (in->isCall()) innerCalls[a].push_back(t);
                computes = true;
            }
            pc = in->next();
        }
        if (!pure || !sawRet || !computes) continue;
        shaped.insert(a);
        bool used = false;
        for (const Insn* c : callSites[a])
            if (resultUsed(c)) used = true;
        if (used) candidates.insert(a);
    }
    // A single line DEF FN can't GOSUB: what it calls is a function as well
    std::vector<uint16_t> work(candidates.begin(), candidates.end());
    while (!work.empty()) {
        uint16_t a = work.back();
        work.pop_back();
        for (uint16_t t : innerCalls[a])
            if (shaped.count(t) && candidates.insert(t).second) work.push_back(t);
    }
    for (uint16_t a : candidates) fns_.insert(a);
}

std::vector<LInsn> Program::linearize(uint16_t start, const std::set<uint16_t>& owned) const {
    (void)start;
    std::vector<LInsn> out;
    std::vector<const Insn*> ins;
    for (uint16_t a : owned) {
        const Insn* in = dis_.at(a);
        if (in) ins.push_back(in);
    }
    auto consecutive = [&](size_t i, size_t n) {
        if (i + n > ins.size()) return false;
        for (size_t k = 1; k < n; ++k)
            if (ins[i + k - 1]->next() != ins[i + k]->addr) return false;
        return true;
    };
    for (size_t i = 0; i < ins.size(); ++i) {
        const Insn& a = *ins[i];
        LInsn li;
        li.insn = &a;
        li.addr = a.addr;
        li.end = a.next();
        uint16_t t;
        // SetBool: jcc over a one byte "dec r"
        if (a.isJcc() && consecutive(i, 2) && a.branchTarget(t) && t == a.next() + 1) {
            ZydisRegister r = decReg(*ins[i + 1]);
            if (r != ZYDIS_REGISTER_NONE) {
                li.idiom = IdiomKind::SetBool;
                li.reg = r;
                li.cc = a.zi.mnemonic;
                li.end = ins[i + 1]->next();
                out.push_back(li);
                i += 1;
                continue;
            }
        }
        ZydisRegister tr = selfTestReg(a);
        if (tr != ZYDIS_REGISTER_NONE && consecutive(i, 3)) {
            const Insn& j = *ins[i + 1];
            const Insn& k = *ins[i + 2];
            if (!j.isRt && j.zi.mnemonic == ZYDIS_MNEMONIC_JNL && j.branchTarget(t) && t == j.next() + 2 &&
                !k.isRt && k.zi.mnemonic == ZYDIS_MNEMONIC_NEG && isRegOp(k, 0, tr)) {
                li.idiom = IdiomKind::AbsInt;
                li.reg = tr;
                li.end = k.next();
                out.push_back(li);
                i += 2;
                continue;
            }
        }
        if (tr != ZYDIS_REGISTER_NONE && consecutive(i, 5)) {
            const Insn& j = *ins[i + 1];
            const Insn& k = *ins[i + 2];
            const Insn& m = *ins[i + 3];
            const Insn& n = *ins[i + 4];
            uint16_t t1, t2;
            if (!j.isRt && j.zi.mnemonic == ZYDIS_MNEMONIC_JZ && j.branchTarget(t1) && !k.isRt &&
                k.zi.mnemonic == ZYDIS_MNEMONIC_MOV && isRegOp(k, 0, tr) &&
                k.ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && k.ops[1].imm.value.u == 1 && !m.isRt &&
                m.zi.mnemonic == ZYDIS_MNEMONIC_JNL && m.branchTarget(t2) && t1 == t2 && !n.isRt &&
                n.zi.mnemonic == ZYDIS_MNEMONIC_NEG && isRegOp(n, 0, tr) && t1 == n.next()) {
                li.idiom = IdiomKind::SgnInt;
                li.reg = tr;
                li.end = n.next();
                out.push_back(li);
                i += 4;
                continue;
            }
        }
        // WAIT port, and [, xor]
        if (!a.isRt && a.zi.mnemonic == ZYDIS_MNEMONIC_IN && isRegOp(a, 0, ZYDIS_REGISTER_AL)) {
            size_t n = 0;
            bool hasXor = false;
            if (consecutive(i, 3)) {
                const Insn& b = *ins[i + 1];
                const Insn& c = *ins[i + 2];
                if (!b.isRt && b.zi.mnemonic == ZYDIS_MNEMONIC_AND && isRegOp(b, 0, ZYDIS_REGISTER_AL) &&
                    isRegOp(b, 1, ZYDIS_REGISTER_CL) && !c.isRt && c.zi.mnemonic == ZYDIS_MNEMONIC_JZ &&
                    c.branchTarget(t) && t == a.addr)
                    n = 3;
            }
            if (!n && consecutive(i, 4)) {
                const Insn& b = *ins[i + 1];
                const Insn& c = *ins[i + 2];
                const Insn& d = *ins[i + 3];
                if (!b.isRt && b.zi.mnemonic == ZYDIS_MNEMONIC_XOR && isRegOp(b, 0, ZYDIS_REGISTER_AL) &&
                    isRegOp(b, 1, ZYDIS_REGISTER_BL) && !c.isRt && c.zi.mnemonic == ZYDIS_MNEMONIC_AND &&
                    isRegOp(c, 0, ZYDIS_REGISTER_AL) && isRegOp(c, 1, ZYDIS_REGISTER_CL) && !d.isRt &&
                    d.zi.mnemonic == ZYDIS_MNEMONIC_JZ && d.branchTarget(t) && t == a.addr) {
                    n = 4;
                    hasXor = true;
                }
            }
            if (n) {
                li.idiom = IdiomKind::Wait;
                li.waitXor = hasXor;
                li.end = ins[i + n - 1]->next();
                out.push_back(li);
                i += n - 1;
                continue;
            }
        }
        out.push_back(li);
    }
    return out;
}

void Program::buildProc(Proc& p, std::set<uint16_t> roots, const std::set<uint16_t>* claimed) {
    // 1. Reachability within the procedure.
    std::set<uint16_t> owned;
    std::vector<uint16_t> work(roots.begin(), roots.end());
    std::set<uint16_t> gosubs;
    std::set<uint16_t> leaders(roots.begin(), roots.end());
    auto push = [&](uint16_t a) { work.push_back(a); };
    for (;;) {
        if (work.empty()) {
            if (!claimed) break;
            // Main program: pick up code that no procedure reached (dead code).
            bool found = false;
            for (const auto& kv : dis_.insns()) {
                if (!owned.count(kv.first) && !claimed->count(kv.first)) {
                    roots.insert(kv.first);
                    leaders.insert(kv.first);
                    push(kv.first);
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
        uint16_t pc = work.back();
        work.pop_back();
        while (true) {
            if (owned.count(pc)) break;
            const Insn* in = dis_.at(pc);
            if (!in) break;
            owned.insert(pc);
            if (in->isRt) {
                const RtCall& rt = in->rt;
                if (rt.intNo == 0x3f && (rt.op == 0x5d || rt.op == 0x5e || rt.op == 0x58)) {
                    for (uint16_t t : rt.targets) {
                        leaders.insert(t);
                        if (rt.op != 0x5e) gosubs.insert(t);
                        push(t);
                    }
                }
                if (isTerminalRt(rt)) break;
                pc = in->next();
                continue;
            }
            uint16_t t;
            if (in->isJmp()) {
                if (in->branchTarget(t)) { push(t); }
                break;
            }
            if (in->isJcc()) {
                if (in->branchTarget(t)) { push(t); }
            } else if (in->isCall()) {
                if (in->branchTarget(t) && !fns_.count(t) && !subs_.count(t)) {
                    leaders.insert(t);
                    gosubs.insert(t);
                    push(t);
                }
            } else if (in->isRet() || in->isRetf() || in->zi.mnemonic == ZYDIS_MNEMONIC_IRET) {
                if (in->isRetf() && in->zi.operand_count_visible > 0 &&
                    in->ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                    p.retfBytes = int(in->ops[0].imm.value.u);
                else if (in->isRetf())
                    p.retfBytes = 0;
                break;
            }
            pc = in->next();
        }
    }
    p.gosubTargets = gosubs;

    // 2. Linear list of instructions/idioms.
    std::vector<LInsn> code = linearize(p.entry, owned);
    if (code.empty()) return;
    // Remove leaders that fell inside fused idioms (their internal jump targets).
    std::set<uint16_t> starts;
    for (const LInsn& li : code) starts.insert(li.addr);

    // 3. Leaders from the fused code.
    for (size_t i = 0; i < code.size(); ++i) {
        const LInsn& li = code[i];
        bool endsBlock = false;
        if (li.idiom == IdiomKind::None) {
            const Insn& in = *li.insn;
            uint16_t t;
            if (in.isRt) {
                if (isTerminalRt(in.rt) || in.rt.is(0x3f, 0x5e)) endsBlock = true;
            } else if (in.isJmp() || in.isJcc()) {
                if (in.branchTarget(t)) leaders.insert(t);
                endsBlock = true;
            } else if (in.isRet() || in.isRetf() || in.zi.mnemonic == ZYDIS_MNEMONIC_IRET) {
                endsBlock = true;
            }
        }
        if (endsBlock && i + 1 < code.size()) leaders.insert(code[i + 1].addr);
        // a gap in the code (unreachable bytes) also starts a new block
        if (i + 1 < code.size() && code[i + 1].addr != li.end) leaders.insert(code[i + 1].addr);
    }
    leaders.insert(code.front().addr);
    // DATA statements (NOP placeholders) start blocks so that RESTORE labels can be placed
    std::set<uint16_t> dataKeys;
    for (const DataLine& d : prog_.dataLines) dataKeys.insert(d.key);
    for (const LInsn& li : code)
        if (li.insn && !li.insn->isRt && li.insn->zi.mnemonic == ZYDIS_MNEMONIC_NOP && dataKeys.count(li.addr))
            leaders.insert(li.addr);

    // 4. Blocks.
    for (size_t i = 0; i < code.size();) {
        Block b;
        b.id = uint32_t(p.blocks.size());
        b.start = code[i].addr;
        do {
            b.code.push_back(code[i]);
            ++i;
        } while (i < code.size() && !(leaders.count(code[i].addr) && starts.count(code[i].addr)));
        b.end = b.code.back().end;
        p.blockAt[b.start] = b.id;
        p.blocks.push_back(std::move(b));
    }
    for (const Block& b : p.blocks) {
        p.lowAddr = std::min(p.lowAddr, b.start);
        p.highAddr = std::max<uint16_t>(p.highAddr, b.end);
    }

    // 5. Terminators and edges.
    auto idAt = [&](uint16_t a) -> int {
        auto it = p.blockAt.find(a);
        return it == p.blockAt.end() ? -1 : int(it->second);
    };
    for (size_t bi = 0; bi < p.blocks.size(); ++bi) {
        Block& b = p.blocks[bi];
        const LInsn& last = b.code.back();
        int fall = idAt(b.end);
        b.term = TermKind::Fall;
        if (last.idiom == IdiomKind::None) {
            const Insn& in = *last.insn;
            uint16_t t;
            if (in.isRt) {
                if (in.rt.is(0x3f, 0x5e)) {
                    b.term = TermKind::OnGoto;
                    for (uint16_t tt : in.rt.targets) {
                        int id = idAt(tt);
                        if (id >= 0) b.succs.push_back(uint32_t(id));
                    }
                } else if (isTerminalRt(in.rt)) {
                    bool ret = in.rt.is(0x3f, 0x60) || in.rt.is(0x3f, 0x3a);
                    b.term = ret ? TermKind::Ret : TermKind::Stop;
                    fall = -1;
                }
            } else if (in.isJmp()) {
                b.term = TermKind::Jump;
                fall = -1;
                if (in.branchTarget(t) && idAt(t) >= 0) b.succs.push_back(uint32_t(idAt(t)));
            } else if (in.isJcc()) {
                b.term = TermKind::Cond;
                if (in.branchTarget(t) && idAt(t) >= 0) b.succs.push_back(uint32_t(idAt(t)));
            } else if (in.isRet() || in.isRetf() || in.zi.mnemonic == ZYDIS_MNEMONIC_IRET) {
                b.term = TermKind::Ret;
                fall = -1;
            }
        }
        if (fall >= 0) b.succs.push_back(uint32_t(fall));
        else if (b.term == TermKind::Fall) b.term = TermKind::Stop;
    }
    for (Block& b : p.blocks)
        for (uint32_t s : b.succs) p.blocks[s].preds.push_back(b.id);
    // A block needs a label when it is entered other than by falling through.
    for (Block& b : p.blocks) {
        if (roots.count(b.start) || gosubs.count(b.start) || dis_.trapTargets().count(b.start)) b.isLabel = true;
        if (b.term == TermKind::Jump || b.term == TermKind::Cond || b.term == TermKind::OnGoto) {
            for (size_t k = 0; k < b.succs.size(); ++k) {
                bool fallEdge = (b.term == TermKind::Cond || b.term == TermKind::OnGoto) && k + 1 == b.succs.size();
                if (!fallEdge) p.blocks[b.succs[k]].isLabel = true;
            }
        }
    }
}

} // namespace qb
