#include "decompiler.h"

#include "cfg.h"
#include "disasm.h"
#include "emitter.h"
#include "lifter.h"
#include "postprocess.h"
#include "printer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace qb {

namespace {

// Finds dynamic array descriptors ("MOV BX,desc" before DIM/REDIM) ahead of
// lifting, so that element accesses can be typed.
void prescanArrays(const Disassembly& dis, GlobalInfo& info) {
    int lastBx = -1;
    for (const auto& kv : dis.insns()) {
        const Insn& in = kv.second;
        if (in.isRt) {
            const RtCall& rt = in.rt;
            if (rt.intNo == 0x3f && ((rt.op >= 0x43 && rt.op <= 0x46) || (rt.op >= 0x4f && rt.op <= 0x52)) && lastBx >= 0) {
                static const Ty ts[] = {Ty::Sng, Ty::Dbl, Ty::Int, Ty::Str};
                int k = rt.op >= 0x4f ? rt.op - 0x4f : rt.op - 0x43;
                info.dynArrays[uint16_t(lastBx)] = ts[k];
                info.dynDims[uint16_t(lastBx)] = rt.byteArg();
            }
            if (rt.intNo != 0x3f || rt.op != 0x55) lastBx = -1;
            continue;
        }
        if (in.zi.mnemonic == ZYDIS_MNEMONIC_MOV && in.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
            in.ops[0].reg.value == ZYDIS_REGISTER_BX && in.ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
            lastBx = int(in.ops[1].imm.value.u & 0xffff);
    }
}

// Parameter list of a SUB that is never called in this module (it may be
// called from another module linked into the program): stack words, with the
// array parameters seen in its body (pass 1) taking 4 + dims words each.
void inferBodyArrayParams(const Program& program, GlobalInfo& info) {
    for (const auto& kv : info.bodyArrays) {
        const Proc* p = program.procAt(kv.first);
        if (!p || p->retfBytes <= 0 || info.subCallArgs.count(kv.first)) continue;
        int words = p->retfBytes / 2;
        int end = 6 + 2 * words;
        SubInfo& si = info.subs[kv.first];
        std::vector<SubParam> old = si.params; // indexed by stack word (last parameter = last word)
        std::vector<SubParam> rev;              // from the last parameter (bp+6) up
        bool ok = true;
        for (int off = 6; off < end;) {
            auto a = kv.second.find(off);
            if (a != kv.second.end()) {
                int size = (4 + a->second.dims) * 2;
                if (off + size > end) { ok = false; break; }
                SubParam sp;
                sp.isArray = true;
                sp.dims = a->second.dims;
                sp.type = a->second.type;
                rev.push_back(sp);
                off += size;
            } else {
                size_t k = size_t(words - 1 - (off - 6) / 2);
                rev.push_back(k < old.size() ? old[k] : SubParam());
                off += 2;
            }
        }
        if (!ok) continue;
        si.params.assign(rev.rbegin(), rev.rend());
    }
}

// A SUB that passes its own parameter on to another SUB: both have the same type.
void propagatePassThroughTypes(GlobalInfo& info) {
    auto param = [&](uint16_t sub, int k) -> SubParam* {
        auto it = info.subs.find(sub);
        if (it == info.subs.end() || k < 0 || k >= int(it->second.params.size())) return nullptr;
        SubParam* p = &it->second.params[size_t(k)];
        return p->isArray ? nullptr : p;
    };
    // a variable argument whose type only became known later in pass 1
    for (const auto& kv : info.subCallArgs)
        for (const auto& args : kv.second)
            for (size_t k = 0; k < args.size(); ++k) {
                SubParam* p = param(kv.first, int(k));
                const ExprPtr& a = args[k];
                if (!p || p->type != Ty::Unknown || !a || a->kind != EK::Var || a->type != Ty::Unknown) continue;
                auto vt = info.varTypes.find(a->addr);
                if (vt == info.varTypes.end()) continue;
                std::set<Ty> known;
                for (Ty t : vt->second)
                    if (t != Ty::Unknown) known.insert(t);
                if (known.size() == 1) p->type = *known.begin();
            }
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& pt : info.paramPassThrough) {
            SubParam* a = param(pt.caller, pt.callerParam);
            SubParam* b = param(pt.callee, pt.calleeParam);
            if (!a || !b || (a->type == Ty::Unknown) == (b->type == Ty::Unknown)) continue;
            if (a->type == Ty::Unknown) a->type = b->type;
            else b->type = a->type;
            changed = true;
        }
    }
}

// DEF FN parameters: variables that are stored right before every call of the
// function, read by its body and not read anywhere else.
void analyzeFns(const Program& program, GlobalInfo& info) {
    for (const Proc& p : program.procs()) {
        if (p.kind != ProcKind::DefFn && p.kind != ProcKind::DefFnSingle) continue;
        FnInfo& fi = info.fns[p.entry];
        uint32_t fnProc = 0;
        for (uint32_t i = 0; i < program.procs().size(); ++i)
            if (&program.procs()[i] == &p) fnProc = i;
        auto calls = info.fnCallStores.find(p.entry);
        std::vector<uint16_t> params;
        if (calls != info.fnCallStores.end() && !calls->second.empty()) {
            // A parameter is stored before calls and only read inside the function.
            // (Arguments of nested calls may be stored before other calls, so a
            // parameter doesn't have to show up right before every call.)
            auto isParam = [&](uint16_t v) {
                bool readInBody = info.fnBodyReads[p.entry].count(v) != 0;
                bool readElsewhere = false;
                for (uint32_t pr : info.varReadProcs[v])
                    if (pr != fnProc) readElsewhere = true;
                return readInBody && !readElsewhere;
            };
            // order: the call site with the most parameters, in store order
            std::vector<uint16_t> best;
            for (const auto& c : calls->second) {
                std::vector<uint16_t> cur;
                for (uint16_t v : c)
                    if (isParam(v) && std::find(cur.begin(), cur.end(), v) == cur.end()) cur.push_back(v);
                if (cur.size() > best.size()) best = cur;
            }
            for (const auto& c : calls->second)
                for (uint16_t v : c)
                    if (isParam(v) && std::find(best.begin(), best.end(), v) == best.end()) best.push_back(v);
            params = best;
        }
        fi.params = params;
        if (std::getenv("QBDEBUG")) {
            std::fprintf(stderr, "fn %04x calls=%zu params=%zu\n", p.entry,
                         calls == info.fnCallStores.end() ? size_t(0) : calls->second.size(), params.size());
            if (calls != info.fnCallStores.end())
                for (auto& c : calls->second) {
                    for (auto x : c) std::fprintf(stderr, " %04x", x);
                    std::fprintf(stderr, "\n");
                }
            for (auto x : info.fnBodyReads[p.entry]) {
                std::fprintf(stderr, " read %04x procs:", x);
                for (auto q : info.varReadProcs[x]) std::fprintf(stderr, " %u", q);
                std::fprintf(stderr, "\n");
            }
        }
        fi.paramTypes.clear();
        for (uint16_t v : params) {
            Ty t = Ty::Sng;
            auto it = info.varTypes.find(v);
            if (it != info.varTypes.end() && !it->second.empty()) t = *it->second.begin();
            fi.paramTypes.push_back(t);
        }
        if (p.kind == ProcKind::DefFn) {
            auto r = info.fnResultReads.find(p.entry);
            if (r != info.fnResultReads.end()) {
                fi.hasResultVar = true;
                fi.resultVar = r->second;
                fi.type = info.fnResultTypes[p.entry];
                if (fi.type == Ty::Unknown) {
                    auto it = info.varTypes.find(r->second);
                    if (it != info.varTypes.end() && !it->second.empty()) fi.type = *it->second.begin();
                }
            }
        }
        if (fi.type == Ty::Unknown) fi.type = Ty::Sng;
    }
}

std::string hex4(uint16_t v) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%04X", v);
    return buf;
}

} // namespace

DecompileResult decompile(const LoadedProgram& prog, const DecompileOptions& opt) {
    DecompileResult res;
    Disassembly dis(prog);
    dis.run();
    Program program(prog, dis);
    GlobalInfo info;
    // what other programs know about the part of the COMMON block this one has
    for (const CommonItem& h : opt.commonHints)
        if (prog.commonSize && h.addr >= prog.commonStart && h.addr + h.size <= prog.commonEnd())
            info.commonHints.push_back(h);
    prescanArrays(dis, info);

    Lifter lifter(program, info);
    lifter.run(1);
    inferBodyArrayParams(program, info);
    propagatePassThroughTypes(info);
    analyzeFns(program, info);
    {
        std::map<uint16_t, std::set<Ty>> vt;
        std::map<uint16_t, std::pair<uint16_t, int>> al;
        for (const auto& kv : computeArrays(prog, lifter.results(), info, vt, al))
            if (!kv.second.dynamic) info.arrayExtent[kv.first] = kv.second.bytes;
    }
    lifter.run(2);
    res.warnings = lifter.warnings();

    if (opt.dumpLifted) {
        NameResolver names;
        names.var = [](uint16_t a, Ty t) { return "vari" + hex4(a) + tySuffix(t); };
        names.array = [](uint16_t a, Ty t) { return "vari" + hex4(a) + tySuffix(t); };
        names.param = [](int i, Ty t) { return "arg" + std::to_string(i) + tySuffix(t); };
        names.fn = [](uint16_t a) { return "FNf" + hex4(a); };
        names.label = [](uint16_t a) { return "loc" + hex4(a); };
        Printer pr(names);
        std::string out;
        const auto& results = lifter.results();
        for (size_t pi = 0; pi < program.procs().size(); ++pi) {
            const Proc& p = program.procs()[pi];
            out += "=== proc " + p.name + " entry " + hex4(p.entry) + "\n";
            for (size_t bi = 0; bi < p.blocks.size(); ++bi) {
                const Block& b = p.blocks[bi];
                const LiftedBlock& lb = results[pi][bi];
                out += "  block " + std::to_string(bi) + " [" + hex4(b.start) + "-" + hex4(b.end) + ")";
                if (b.isLabel) out += " label";
                out += " preds:";
                for (uint32_t x : b.preds) out += " " + std::to_string(x);
                out += "\n";
                for (const Stmt& s : lb.stmts) out += "    " + hex4(s.addr) + "  " + pr.stmt(s) + "\n";
                static const char* tk[] = {"fall", "jump", "cond", "ongoto", "ret", "stop"};
                out += std::string("    -> ") + tk[int(b.term)];
                if (lb.cond) out += " if " + pr.cond(lb.cond);
                if (lb.selector) out += " sel " + pr.expr(lb.selector);
                for (uint32_t s : b.succs) out += " " + std::to_string(s);
                out += "\n";
            }
        }
        res.source = out;
        return res;
    }
    ProgramInfo pinfo = postprocess(program, lifter.results(), info, res.warnings);
    if (opt.layoutOnly) {
        if (!prog.commonSize) return res;
        const uint32_t cs = prog.commonStart, ce = prog.commonEnd();
        for (const auto& kv : pinfo.arrays) {
            const ArrayInfo& ai = kv.second;
            if (ai.dynamic || ai.base < cs || ai.base >= ce) continue;
            CommonItem it;
            it.addr = ai.base;
            it.size = ai.bytes;
            it.type = ai.type;
            it.array = true;
            it.dims = ai.dims;
            res.common.push_back(it);
        }
        for (const auto& kv : pinfo.varTypes) {
            if (kv.first < cs || kv.first >= ce || kv.second == Ty::Unknown) continue;
            bool inArray = false;
            for (const CommonItem& a : res.common)
                if (a.array && kv.first >= a.addr && kv.first < a.addr + a.size) inArray = true;
            if (inArray) continue;
            CommonItem it;
            it.addr = kv.first;
            it.type = kv.second;
            it.size = kv.second == Ty::Int ? 2 : kv.second == Ty::Dbl ? 8 : 4;
            res.common.push_back(it);
        }
        return res;
    }
    res.source = emitProgram(program, lifter.results(), info, pinfo, opt, prog.moduleName, res.warnings);
    return res;
}

std::vector<CommonItem> commonLayout(const LoadedProgram& prog, const std::vector<CommonItem>& hints) {
    DecompileOptions opt;
    opt.layoutOnly = true;
    opt.commonHints = hints;
    return decompile(prog, opt).common;
}

} // namespace qb
