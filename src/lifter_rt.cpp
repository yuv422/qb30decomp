// Semantics of the BRUN30 runtime opcodes (INT 3Dh / 3Eh / 3Fh).
#include "lifter_impl.h"
#include "simplify.h"

#include <cstdio>

namespace qb {

namespace {

bool isMinus1(const ExprPtr& e) {
    int32_t v;
    return isIntLit(e, &v) && (v == -1 || v == 0xffff);
}

bool isOmitted7fff(const ExprPtr& e) {
    int32_t v;
    return isIntLit(e, &v) && v == 0x7fff;
}

Ty printType(int op) {
    switch ((op - 0x63) % 4) {
    case 0: return Ty::Sng;
    case 1: return Ty::Dbl;
    case 2: return Ty::Int;
    default: return Ty::Str;
    }
}

// Array reference used as a statement argument (GET/PUT graphics, ERASE, DIM ...).
ExprPtr arrayRef(uint16_t base, Ty t, bool dynamic) {
    ExprPtr r = mkRaw("@array", t);
    auto e = std::make_shared<Expr>(*r);
    e->addr = base;
    e->flags = dynamic ? AF_Dynamic : 0;
    return e;
}

} // namespace

ExprPtr Lifter::Impl::intArg(int r) {
    ExprPtr v = reg(r);
    if (v->kind == EK::PtrTo) return v->args[0];
    return v;
}

ExprPtr Lifter::Impl::ptrArg(int r, Ty t) { return deref(reg(r), t); }

std::string Lifter::Impl::litText(const ExprPtr& e) {
    if (e && e->kind == EK::StrLit) return e->sval;
    return "";
}

Ty Lifter::Impl::dynType(const ExprPtr& desc) {
    int32_t a;
    if (isIntLit(desc, &a)) {
        auto it = info.dynArrays.find(uint16_t(a));
        return it == info.dynArrays.end() ? Ty::Unknown : it->second;
    }
    if (desc && desc->kind == EK::Param && proc) {
        auto it = info.subs.find(proc->entry);
        if (it != info.subs.end() && desc->ival < int(it->second.params.size()))
            return it->second.params[size_t(desc->ival)].type;
    }
    return Ty::Unknown;
}

// Element of a dynamic array selected by BX (descriptor) and DX (byte offset).
ExprPtr Lifter::Impl::dynElem(Ty t) {
    ExprPtr desc = reg(BX);
    ExprPtr off = reg(DX);
    int32_t a;
    if (isIntLit(desc, &a)) return mkArrElem(uint16_t(a), t, {off}, AF_ByteOffset | AF_Dynamic);
    if (desc->kind == EK::Param) return mkArrElem(0, t, {off}, AF_ByteOffset | AF_Dynamic | AF_Param, desc->ival);
    warn("unknown array descriptor");
    return mkUnknown("array", t);
}

// Value for a dynamic array store: AX for integers, the FAC otherwise.
ExprPtr Lifter::Impl::facOrAxResult(Ty t) {
    if (t == Ty::Int) return reg(AX);
    if (t == Ty::Sng || t == Ty::Dbl) return facValue(t);
    // unknown element type: guess from the FAC state
    if (st.facTy == Ty::Sng || st.facTy == Ty::Dbl) return facValue(st.facTy);
    return reg(AX);
}

void Lifter::Impl::printItem(ExprPtr e, char sep) {
    if (!pend.print) {
        warn("PRINT item outside PRINT");
        pend.print = true;
        pend.printHead = "PRINT";
        pend.printAddr = curAddr;
    }
    // TAB()/SPC() are followed by an empty string that carries the separator
    if (!pend.printItems.empty() && pend.printItems.back().second == '?' && e->kind == EK::StrLit && e->sval.empty()) {
        pend.printItems.back().second = sep;
        return;
    }
    pend.printItems.emplace_back(std::move(e), sep);
}

void Lifter::Impl::printEnd() {
    if (!pend.print) return;
    pend.print = false;
    std::vector<Part> parts{P(pend.printHead)};
    auto& items = pend.printItems;
    // leading pseudo items: file number ('F') and USING format ('U')
    size_t first = 0;
    bool hasFile = false;
    while (first < items.size() && (items[first].second == 'F' || items[first].second == 'U')) {
        if (items[first].second == 'F') {
            parts.push_back(P(" #"));
            parts.push_back(P(items[first].first, Ty::Int));
            parts.push_back(P(","));
            hasFile = true;
        } else {
            parts.push_back(P(" USING "));
            parts.push_back(P(items[first].first, Ty::Str));
            parts.push_back(P(";"));
        }
        ++first;
    }
    items.erase(items.begin(), items.begin() + long(first));
    if (items.size() == 1 && items[0].second == 0 && items[0].first->kind == EK::StrLit &&
        items[0].first->sval.empty() && !hasFile && !pend.printUsing) {
        items.clear();
    }
    for (size_t i = 0; i < items.size(); ++i) {
        parts.push_back(P(i == 0 ? " " : ""));
        parts.push_back(PK(items[i].first));
        char sep = items[i].second;
        bool lastItem = i + 1 == items.size();
        if (pend.printWrite) {
            if (!lastItem) parts.push_back(P(", "));
        } else if (sep == ',') {
            parts.push_back(P(lastItem ? "," : ", "));
        } else if (sep == ';' || sep == '?') {
            parts.push_back(P(lastItem ? ";" : "; "));
        } else if (!lastItem) {
            parts.push_back(P("; "));
        }
    }
    items.clear();
    uint16_t a = curAddr;
    curAddr = pend.printAddr;
    addText(std::move(parts));
    curAddr = a;
}

void Lifter::Impl::inputStore(ExprPtr var) {
    if (!pend.input) {
        warn("INPUT store outside INPUT");
        return;
    }
    pend.inputVars.push_back(std::move(var));
    if (int(pend.inputVars.size()) >= pend.inputCount) {
        pend.input = false;
        std::vector<Part> parts{P(pend.inputHead)};
        if (pend.inputFile) {
            parts.push_back(P(pend.inputFile, Ty::Int));
            parts.push_back(P(", "));
        }
        for (size_t i = 0; i < pend.inputVars.size(); ++i) {
            if (i) parts.push_back(P(", "));
            parts.push_back(P(pend.inputVars[i]));
        }
        pend.inputVars.clear();
        uint16_t a = curAddr;
        curAddr = pend.inputAddr;
        addText(std::move(parts));
        curAddr = a;
    }
}

void Lifter::Impl::gfxPoint(Pending::Pt& p, ExprPtr x, ExprPtr y, bool floatX, bool floatY) {
    p.x = floatX ? deref(x, Ty::Sng) : x;
    p.y = floatY ? deref(y, Ty::Sng) : y;
    p.step = pend.step;
    p.valid = true;
    pend.step = false;
}

std::vector<Part> Lifter::Impl::pointParts(const Pending::Pt& p) {
    std::vector<Part> v;
    v.push_back(P(p.step ? "STEP(" : "("));
    v.push_back(P(p.x));
    v.push_back(P(", "));
    v.push_back(P(p.y));
    v.push_back(P(")"));
    return v;
}

static bool regIsFloatType(const ExprPtr& e) {
    int32_t v;
    return isIntLit(e, &v) && v != 0;
}

// Appends ", a, b" leaving out trailing omitted arguments (nullptr = omitted).
static void appendArgs(std::vector<Part>& parts, const std::vector<ExprPtr>& args, bool leadingComma) {
    size_t n = args.size();
    while (n > 0 && !args[n - 1]) --n;
    for (size_t i = 0; i < n; ++i) {
        if (i || leadingComma) parts.push_back(P(args[i] ? ", " : ","));
        if (args[i]) parts.push_back(P(args[i]));
    }
}

void Lifter::Impl::execRt(const Insn& in) {
    const RtCall& rt = in.rt;
    auto setBxStr = [&](ExprPtr e) { setReg(BX, mkPtrTo(std::move(e))); };
    auto strBX = [&]() { return ptrArg(BX, Ty::Str); };
    auto opt = [&](int r) -> ExprPtr { ExprPtr v = intArg(r); return isMinus1(v) ? nullptr : v; };
    auto fn1 = [&](const char* name, ExprPtr a, Ty t) { return mkFunc(name, {std::move(a)}, t); };
    auto stmt1 = [&](const std::string& kw, ExprPtr a, Ty want = Ty::Unknown) {
        addText({P(kw + " "), P(std::move(a), want)});
    };

    if (rt.intNo == 0x3d) {
        switch (rt.op) {
        case 0x00: warn("far call stub"); addText("REM far call stub"); return;
        case 0x01: setFac(fn1("FIX", ptrArg(BX, Ty::Sng), Ty::Sng), Ty::Sng); return;
        case 0x02: setFac(fn1("FIX", ptrArg(BX, Ty::Dbl), Ty::Dbl), Ty::Dbl); return;
        case 0x03: setFac(fn1("INT", ptrArg(BX, Ty::Sng), Ty::Sng), Ty::Sng); return;
        case 0x04: setFac(fn1("INT", ptrArg(BX, Ty::Dbl), Ty::Dbl), Ty::Dbl); return;
        case 0x05: setBxStr(fn1("CHR$", intArg(BX), Ty::Str)); return;
        case 0x06: setBxStr(mkFunc("INKEY$", {}, Ty::Str)); return;
        case 0x07: {
            ExprPtr n = intArg(BX), f = intArg(DX);
            if (isOmitted7fff(f)) setBxStr(mkFunc("INPUT$", {n}, Ty::Str));
            else setBxStr(mkFunc("INPUT$", {n, mkRaw("#"), f}, Ty::Str));
            return;
        }
        case 0x08: setReg(BX, mkFunc("INSTR", {intArg(BX), ptrArg(DX, Ty::Str), ptrArg(CX, Ty::Str)}, Ty::Int)); return;
        case 0x09: setReg(BX, mkFunc("INSTR", {strBX(), ptrArg(DX, Ty::Str)}, Ty::Int)); return;
        case 0x0A: {
            ExprPtr len = intArg(CX);
            if (isOmitted7fff(len)) setBxStr(mkFunc("MID$", {strBX(), intArg(DX)}, Ty::Str));
            else setBxStr(mkFunc("MID$", {strBX(), intArg(DX), len}, Ty::Str));
            return;
        }
        case 0x0B: setBxStr(mkFunc("LEFT$", {strBX(), intArg(DX)}, Ty::Str)); return;
        case 0x0C: setBxStr(mkFunc("RIGHT$", {strBX(), intArg(DX)}, Ty::Str)); return;
        case 0x0D: setBxStr(fn1("SPACE$", intArg(BX), Ty::Str)); return;
        case 0x0E: setBxStr(mkFunc("STRING$", {intArg(BX), intArg(DX)}, Ty::Str)); return;
        case 0x0F: setBxStr(mkFunc("STRING$", {intArg(BX), ptrArg(DX, Ty::Str)}, Ty::Str)); return;
        case 0x10: setBxStr(fn1("STR$", intArg(BX), Ty::Str)); return;
        case 0x11: setBxStr(fn1("STR$", ptrArg(BX, Ty::Sng), Ty::Str)); return;
        case 0x12: setBxStr(fn1("STR$", ptrArg(BX, Ty::Dbl), Ty::Str)); return;
        case 0x13: setFac(fn1("VAL", strBX(), Ty::Dbl), Ty::Dbl); return;
        case 0x14: setBxStr(fn1("HEX$", intArg(BX), Ty::Str)); return;
        case 0x15: setBxStr(fn1("HEX$", ptrArg(BX, Ty::Sng), Ty::Str)); return;
        case 0x16: setBxStr(fn1("OCT$", intArg(BX), Ty::Str)); return;
        case 0x17: setBxStr(fn1("OCT$", ptrArg(BX, Ty::Sng), Ty::Str)); return;
        case 0x18: setReg(BX, fn1("CVI", strBX(), Ty::Int)); return;
        case 0x19: setFac(fn1("CVS", strBX(), Ty::Sng), Ty::Sng); return;
        case 0x1A: setFac(fn1("CVD", strBX(), Ty::Dbl), Ty::Dbl); return;
        case 0x1B: setBxStr(fn1("MKI$", intArg(BX), Ty::Str)); return;
        case 0x1C: setBxStr(fn1("MKS$", ptrArg(BX, Ty::Sng), Ty::Str)); return;
        case 0x1D: setBxStr(fn1("MKD$", ptrArg(BX, Ty::Dbl), Ty::Str)); return;
        case 0x1E: setFac(mkFunc("ERL", {}, Ty::Sng), Ty::Sng); return;
        case 0x1F: setReg(BX, mkFunc("ERR", {}, Ty::Int)); return;
        case 0x20: setReg(BX, fn1("LPOS", intArg(BX), Ty::Int)); return;
        case 0x21: setReg(BX, fn1("POS", intArg(BX), Ty::Int)); return;
        case 0x22: setReg(BX, fn1("INT", intArg(BX), Ty::Int)); return;
        case 0x23: setBxStr(mkFunc("DATE$", {}, Ty::Str)); return;
        case 0x24: setBxStr(mkFunc("TIME$", {}, Ty::Str)); return;
        case 0x25: setReg(BX, mkFunc("CSRLIN", {}, Ty::Int)); return;
        case 0x26: setReg(BX, fn1("PEN", intArg(BX), Ty::Int)); return;
        case 0x27: setReg(BX, mkFunc("POINT", {intArg(BX), intArg(DX)}, Ty::Int)); return;
        case 0x28: setReg(BX, mkFunc("POINT", {ptrArg(BX, Ty::Sng), ptrArg(DX, Ty::Sng)}, Ty::Int)); return;
        case 0x29: setReg(BX, mkFunc("POINT", {intArg(BX), ptrArg(DX, Ty::Sng)}, Ty::Int)); return;
        case 0x2A: setFac(fn1("POINT", intArg(BX), Ty::Sng), Ty::Sng); return;
        case 0x2B: setFac(mkFunc("PMAP", {ptrArg(BX, Ty::Sng), intArg(DX)}, Ty::Sng), Ty::Sng); return;
        case 0x2C: {
            ExprPtr c = intArg(CX);
            int32_t v;
            if (isIntLit(c, &v) && v == 0) setReg(BX, mkFunc("SCREEN", {intArg(BX), intArg(DX)}, Ty::Int));
            else setReg(BX, mkFunc("SCREEN", {intArg(BX), intArg(DX), c}, Ty::Int));
            return;
        }
        case 0x2D: setReg(BX, fn1("STICK", intArg(BX), Ty::Int)); return;
        case 0x2E: setReg(BX, fn1("STRIG", intArg(BX), Ty::Int)); return;
        case 0x2F: setReg(BX, fn1("EOF", intArg(BX), Ty::Int)); return;
        case 0x30: setFac(fn1("LOC", intArg(BX), Ty::Dbl), Ty::Dbl); return;
        case 0x31: setFac(fn1("LOF", intArg(BX), Ty::Dbl), Ty::Dbl); return;
        case 0x32: setReg(BX, mkFunc("VARPTR", {mkRaw("#"), intArg(BX)}, Ty::Int)); return;
        case 0x33: setFac(fn1("RND", ptrArg(BX, Ty::Sng), Ty::Sng), Ty::Sng); return;
        case 0x34: setFac(mkFunc("RND", {}, Ty::Sng), Ty::Sng); return;
        case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: {
            static const char* names[] = {"ATN", "COS", "EXP", "LOG", "SIN", "SQR", "TAN"};
            setFac(fn1(names[rt.op - 0x35], ptrArg(BX, Ty::Sng), Ty::Sng), Ty::Sng);
            return;
        }
        case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x40: case 0x41: case 0x42: {
            static const char* names[] = {"ATN", "COS", "EXP", "LOG", "SIN", "SQR", "TAN"};
            setFac(fn1(names[rt.op - 0x3C], ptrArg(BX, Ty::Dbl), Ty::Dbl), Ty::Dbl);
            return;
        }
        case 0x43: setFac(mkFunc("TIMER", {}, Ty::Sng), Ty::Sng); return;
        case 0x44: setReg(BX, fn1("PLAY", intArg(BX), Ty::Int)); return;
        case 0x45: setBxStr(fn1("IOCTL$", intArg(BX), Ty::Str)); return;
        case 0x46: setBxStr(fn1("ENVIRON$", strBX(), Ty::Str)); return;
        case 0x47: setBxStr(fn1("ENVIRON$", intArg(BX), Ty::Str)); return;
        case 0x48: setReg(BX, mkFunc("ERDEV", {}, Ty::Int)); return;
        case 0x49: setBxStr(mkFunc("ERDEV$", {}, Ty::Str)); return;
        case 0x4A: setBxStr(mkFunc("COMMAND$", {}, Ty::Str)); return;
        case 0x62: setReg(BX, fn1("PEEK", ptrArg(BX, Ty::Sng), Ty::Int)); return;
        case 0x63: setFac(fn1("FRE", strBX(), Ty::Sng), Ty::Sng); return;
        case 0x64: setFac(fn1("FRE", intArg(BX), Ty::Sng), Ty::Sng); return;
        case 0x65: setReg(BX, fn1("SADD", strBX(), Ty::Int)); return;
        default: break;
        }
    } else if (rt.intNo == 0x3e) {
        switch (rt.op) {
        case 0x01: addText("END"); return;
        case 0x02: flushPendingStatements(); return; // end of program
        case 0x03: addText("STOP"); return;
        case 0x04: {
            std::vector<Part> p{P("WIDTH "), P(intArg(BX), Ty::Int)};
            if (!isMinus1(intArg(DX))) { p.push_back(P(", ")); p.push_back(P(intArg(DX), Ty::Int)); }
            addText(std::move(p));
            return;
        }
        case 0x05: stmt1("WIDTH LPRINT", intArg(BX), Ty::Int); return;
        case 0x06:
        case 0x07:
            flushPendingStatements();
            pend.print = true;
            pend.printWrite = true;
            pend.printUsing = false;
            pend.printAddr = curAddr;
            pend.printHead = "WRITE";
            pend.printItems.clear();
            if (rt.op == 0x07) pend.printItems.emplace_back(intArg(BX), 'F');
            return;
        case 0x08: addText("RANDOMIZE"); return;
        case 0x09: stmt1("RANDOMIZE", ptrArg(BX, Ty::Sng)); return;
        case 0x0A: return;
        case 0x0B: {
            ExprPtr a = intArg(BX), b = intArg(DX);
            int32_t v;
            std::vector<Part> p{P("CLEAR ,")};
            if (!(isIntLit(a, &v) && v == 0)) p.push_back(P(a, Ty::Int));
            p.push_back(P(", "));
            p.push_back(P(b, Ty::Int));
            addText(std::move(p));
            return;
        }
        case 0x0C: {
            // RUN is compiled as CLEAR followed by a jump to the start of the program
            const Insn* next = program.dis().at(in.next());
            uint16_t t;
            if (next && next->isJmp() && next->branchTarget(t) && t == prog.codeStart) {
                addText("RUN");
                if (emit) results[procIndex][block->id].noJump = true;
            } else {
                addText("CLEAR");
            }
            return;
        }
        case 0x0D: stmt1("RUN", strBX(), Ty::Str); return;
        case 0x0E: stmt1("CHAIN", strBX(), Ty::Str); return;
        case 0x0F: addText("TRON"); return;
        case 0x10: addText("TROFF"); return;
        case 0x11: stmt1("ERROR", intArg(BX), Ty::Int); return;
        case 0x12: addText("RESUME NEXT"); return;
        case 0x13: addText("RESUME"); return;
        case 0x14: addText("DEF SEG"); return;
        case 0x15: {
            ExprPtr seg = intArg(BX);
            int32_t sv;
            if (isIntLit(seg, &sv) && sv < 0) {
                char hb[16];
                std::snprintf(hb, sizeof hb, "&H%X", unsigned(sv) & 0xffff);
                seg = mkRaw(hb, Ty::Int);
            }
            addText({P("DEF SEG = "), P(seg, Ty::Int)});
            return;
        }
        case 0x16: addText("RESET"); return;
        case 0x17: addText({P("DATE$ = "), P(strBX())}); return;
        case 0x18: addText({P("TIME$ = "), P(strBX())}); return;
        case 0x19: stmt1("BLOAD", strBX()); return;
        case 0x1A: addText({P("BLOAD "), P(strBX()), P(", "), P(ptrArg(DX, Ty::Sng))}); return;
        case 0x1B:
            addText({P("BSAVE "), P(strBX()), P(", "), P(ptrArg(DX, Ty::Sng)), P(", "), P(intArg(CX), Ty::Int)});
            return;
        case 0x1C: stmt1("FILES", strBX()); return;
        case 0x1D: addText("FILES"); return;
        case 0x1E: {
            ExprPtr file = intArg(BX), name = ptrArg(DX, Ty::Str), len = intArg(CX);
            int32_t lv = 0;
            bool hasLen = !(isIntLit(len, &lv) && lv == 0);
            std::vector<Part> p;
            if (pend.openModeStr) {
                p = {P("OPEN "), P(pend.openModeStr), P(", #"), P(file, Ty::Int), P(", "), P(name)};
                if (hasLen) { p.push_back(P(", ")); p.push_back(P(len, Ty::Int)); }
            } else {
                p = {P("OPEN "), P(name)};
                int32_t m;
                if (pend.openMode && isIntLit(pend.openMode, &m)) {
                    static const char* modes[] = {" FOR INPUT", " FOR OUTPUT", " FOR RANDOM", " FOR APPEND"};
                    if (m >= 0 && m <= 3) p.push_back(P(modes[m]));
                }
                int32_t acc;
                if (pend.openAccess && isIntLit(pend.openAccess, &acc)) {
                    static const char* access[] = {"", " ACCESS READ", " ACCESS WRITE", " ACCESS READ WRITE"};
                    static const char* locks[] = {"", " LOCK READ", " LOCK WRITE", " LOCK READ WRITE", " SHARED"};
                    int a = (acc >> 8) & 0xff, l = acc & 0xff;
                    if (a <= 3) p.push_back(P(access[a]));
                    if (l <= 4) p.push_back(P(locks[l]));
                }
                p.push_back(P(" AS #"));
                p.push_back(P(file, Ty::Int));
                if (hasLen) { p.push_back(P(" LEN = ")); p.push_back(P(len, Ty::Int)); }
            }
            pend.openMode = pend.openModeStr = pend.openAccess = nullptr;
            addText(std::move(p));
            return;
        }
        case 0x1F: flushPendingStatements(); pend.openModeStr = strBX(); return;
        case 0x20: flushPendingStatements(); pend.openMode = intArg(BX); return;
        case 0x21: addText({P("CLOSE #"), P(intArg(BX), Ty::Int)}); return;
        case 0x22: addText("CLOSE"); return;
        case 0x23: addText({P("NAME "), P(strBX()), P(" AS "), P(ptrArg(DX, Ty::Str))}); return;
        case 0x24: stmt1("KILL", strBX()); return;
        case 0x25: addText({P("GET #"), P(intArg(BX), Ty::Int)}); return;
        case 0x26: addText({P("GET #"), P(intArg(BX), Ty::Int), P(", "), P(intArg(DX), Ty::Int)}); return;
        case 0x27: addText({P("PUT #"), P(intArg(BX), Ty::Int)}); return;
        case 0x28: addText({P("PUT #"), P(intArg(BX), Ty::Int), P(", "), P(intArg(DX), Ty::Int)}); return;
        case 0x29: addText({P("WIDTH #"), P(intArg(BX), Ty::Int), P(", "), P(intArg(DX), Ty::Int)}); return;
        case 0x2A: addText({P("WIDTH "), P(strBX()), P(", "), P(intArg(DX), Ty::Int)}); return;
        case 0x2B: addText("BEEP"); return;
        case 0x2F: pend.circleStart = ptrArg(BX, Ty::Sng); return;
        case 0x30: pend.circleEnd = ptrArg(BX, Ty::Sng); return;
        case 0x31: pend.circleAspect = ptrArg(BX, Ty::Sng); return;
        case 0x32: {
            ExprPtr a = intArg(BX);
            if (isMinus1(a)) addText("CLS");
            else stmt1("CLS", a, Ty::Int);
            return;
        }
        case 0x33: case 0x42: case 0x59:
            if (!pend.argsActive) { flushPendingStatements(); pend.args.clear(); pend.argsActive = true; }
            pend.args.push_back(intArg(BX));
            return;
        case 0x34: case 0x43: case 0x5A:
            if (!pend.argsActive) { flushPendingStatements(); pend.args.clear(); pend.argsActive = true; }
            pend.args.push_back(nullptr);
            return;
        case 0x35: case 0x44: case 0x5B: {
            const char* kw = rt.op == 0x35 ? "COLOR" : rt.op == 0x44 ? "LOCATE" : "SCREEN";
            std::vector<ExprPtr> args = pend.argsActive ? pend.args : std::vector<ExprPtr>{};
            args.push_back(intArg(BX));
            pend.args.clear();
            pend.argsActive = false;
            std::vector<Part> p{P(std::string(kw) + " ")};
            for (size_t i = 0; i < args.size(); ++i) {
                if (i) p.push_back(P(args[i] ? ", " : ","));
                if (args[i]) p.push_back(P(args[i], Ty::Int));
            }
            addText(std::move(p));
            return;
        }
        case 0x36: stmt1("DRAW", strBX()); return;
        case 0x37: case 0x82: case 0x83: return;
        case 0x3A: case 0x58: {
            // GET / PUT graphics: BX = array element (or descriptor when DX is 0 / -1)
            ExprPtr arr;
            int32_t size = 0;
            ExprPtr dx = intArg(DX);
            if (rt.op == 0x3A && isIntLit(dx, &size) && size == 0) {
                int32_t a;
                arr = isIntLit(reg(BX), &a) ? arrayRef(uint16_t(a), dynType(reg(BX)), true) : mkUnknown("array");
            } else {
                ExprPtr bx = reg(BX);
                int32_t a;
                if (isIntLit(bx, &a)) {
                    arr = arrayRef(uint16_t(a), Ty::Unknown, false);
                    if (rt.op == 0x3A && pass == 1) info.arrayBytes[uint16_t(a)] = uint16_t(size);
                } else {
                    arr = deref(bx, Ty::Unknown);
                }
            }
            if (rt.op == 0x3A) {
                std::vector<Part> p{P("GET ")};
                for (auto& x : pointParts(pend.p1)) p.push_back(x);
                p.push_back(P("-"));
                for (auto& x : pointParts(pend.p2)) p.push_back(x);
                p.push_back(P(", "));
                p.push_back(P(arr));
                addText(std::move(p));
            } else {
                static const char* actions[] = {"OR", "AND", "PRESET", "PSET", "XOR"};
                std::vector<Part> p{P("PUT ")};
                for (auto& x : pointParts(pend.p1)) p.push_back(x);
                p.push_back(P(", "));
                p.push_back(P(arr));
                int32_t act;
                if (isIntLit(dx, &act) && act >= 0 && act <= 3) p.push_back(P(std::string(", ") + actions[act]));
                else if (!isIntLit(dx, &act)) { p.push_back(P(", ")); p.push_back(P(dx)); }
                addText(std::move(p));
            }
            pend.p1 = pend.p2 = Pending::Pt();
            return;
        }
        case 0x3B: pend.step = true; return;
        case 0x3C: {
            int32_t v = -1;
            isIntLit(intArg(BX), &v);
            addText(v == 0 ? "KEY OFF" : v == 1 ? "KEY ON" : "KEY LIST");
            return;
        }
        case 0x3D: addText({P("KEY "), P(intArg(BX), Ty::Int), P(", "), P(ptrArg(DX, Ty::Str))}); return;
        case 0x3E: stmt1("LCOPY", intArg(BX), Ty::Int); return;
        case 0x45: addText("MOTOR"); return;
        case 0x46: stmt1("MOTOR", intArg(BX), Ty::Int); return;
        case 0x48: {
            std::vector<Part> p{P("PAINT ")};
            for (auto& x : pointParts(pend.p1)) p.push_back(x);
            appendArgs(p, {opt(BX), opt(DX)}, true);
            addText(std::move(p));
            pend.p1 = Pending::Pt();
            return;
        }
        case 0x49: {
            std::vector<Part> p{P("PAINT ")};
            for (auto& x : pointParts(pend.p1)) p.push_back(x);
            ExprPtr bg = isMinus1(intArg(CX)) ? nullptr : ptrArg(CX, Ty::Str);
            appendArgs(p, {strBX(), opt(DX), bg}, true);
            addText(std::move(p));
            pend.p1 = Pending::Pt();
            return;
        }
        case 0x4A:
            if (isMinus1(intArg(BX)) && isMinus1(intArg(DX))) addText("PALETTE");
            else addText({P("PALETTE "), P(intArg(BX), Ty::Int), P(", "), P(intArg(DX), Ty::Int)});
            return;
        case 0x4B: {
            ExprPtr arr;
            int32_t a = 0, off = 0;
            if (isMinus1(intArg(CX))) arr = isIntLit(reg(BX), &a) ? mkArrElem(uint16_t(a), Ty::Int, {intArg(DX)}, AF_ByteOffset | AF_Dynamic) : mkUnknown("array");
            else if (isIntLit(reg(BX), &a)) {
                arr = mkArrElem(uint16_t(a), Ty::Int, {intArg(DX)}, AF_ByteOffset);
                if (pass == 1 && isIntLit(intArg(CX), &off)) info.arrayBytes[uint16_t(a)] = uint16_t(off);
            } else arr = mkUnknown("array");
            addText({P("PALETTE USING "), P(arr)});
            return;
        }
        case 0x4C: addText("PEN ON"); return;
        case 0x4D: addText("PEN OFF"); return;
        case 0x4E: addText("PEN STOP"); return;
        case 0x51: stmt1("PLAY", strBX()); return;
        case 0x52: addText("PLAY ON"); return;
        case 0x53: addText("PLAY OFF"); return;
        case 0x54: addText("PLAY STOP"); return;
        case 0x55: case 0x56: {
            Pending::Pt p;
            gfxPoint(p, intArg(BX), intArg(DX), false, false);
            std::vector<Part> parts{P(rt.op == 0x56 ? "PSET " : "PRESET ")};
            for (auto& x : pointParts(p)) parts.push_back(x);
            appendArgs(parts, {opt(CX)}, true);
            addText(std::move(parts));
            return;
        }
        case 0x5C: addText("STRIG ON"); return;
        case 0x5D: addText("STRIG OFF"); return;
        case 0x5E: addText({P("SOUND "), P(intArg(BX), Ty::Int), P(", "), P(ptrArg(DX, Ty::Sng))}); return;
        case 0x5F: return;
        case 0x62: addText({P("PCOPY "), P(intArg(BX), Ty::Int), P(", "), P(intArg(DX), Ty::Int)}); return;
        case 0x64: case 0x65: case 0x66: case 0xA4: {
            static const char* w[] = {" ON", " OFF", " STOP"};
            int k = rt.op == 0xA4 ? 2 : rt.op - 0x64;
            addText({P("COM("), P(intArg(BX), Ty::Int), P(std::string(")") + w[k])});
            return;
        }
        case 0x67: case 0x68: case 0x69: {
            static const char* w[] = {" ON", " OFF", " STOP"};
            addText({P("KEY("), P(intArg(BX), Ty::Int), P(std::string(")") + w[rt.op - 0x67])});
            return;
        }
        case 0x6A: case 0x6B: case 0x6C: {
            static const char* w[] = {" ON", " OFF", " STOP"};
            addText({P("STRIG("), P(intArg(BX), Ty::Int), P(std::string(")") + w[rt.op - 0x6A])});
            return;
        }
        case 0x6D: case 0x6E: {
            std::vector<Part> p{P(rt.op == 0x6D ? "LOCK #" : "UNLOCK #"), P(intArg(BX), Ty::Int)};
            int32_t flags = 0;
            isIntLit(intArg(AX), &flags);
            if (((flags >> 8) & 0xff) != 0xff) {
                ExprPtr s = (flags & 0x10) && !pend.lockRecs.empty() ? pend.lockRecs.front() : intArg(DX);
                ExprPtr e = (flags & 0x01) && !pend.lockRecs.empty() ? pend.lockRecs.back() : intArg(CX);
                p.push_back(P(", "));
                p.push_back(P(s));
                p.push_back(P(" TO "));
                p.push_back(P(e));
            }
            pend.lockRecs.clear();
            addText(std::move(p));
            return;
        }
        case 0x6F:
            pend.viewX1 = ptrArg(BX, Ty::Sng);
            pend.viewY1 = ptrArg(DX, Ty::Sng);
            return;
        case 0x70: {
            std::vector<Part> p{P(isMinus1(intArg(CX)) ? "WINDOW SCREEN (" : "WINDOW (")};
            p.push_back(P(pend.viewX1));
            p.push_back(P(", "));
            p.push_back(P(pend.viewY1));
            p.push_back(P(")-("));
            p.push_back(P(ptrArg(BX, Ty::Sng)));
            p.push_back(P(", "));
            p.push_back(P(ptrArg(DX, Ty::Sng)));
            p.push_back(P(")"));
            addText(std::move(p));
            return;
        }
        case 0x71: addText("WINDOW"); return;
        case 0x72: pend.viewX1 = intArg(BX); pend.viewY1 = intArg(DX); return;
        case 0x73: pend.viewX2 = intArg(BX); pend.viewY2 = intArg(DX); return;
        case 0x74: {
            std::vector<Part> p{P(isMinus1(intArg(CX)) ? "VIEW SCREEN (" : "VIEW (")};
            p.push_back(P(pend.viewX1, Ty::Int));
            p.push_back(P(", "));
            p.push_back(P(pend.viewY1, Ty::Int));
            p.push_back(P(")-("));
            p.push_back(P(pend.viewX2, Ty::Int));
            p.push_back(P(", "));
            p.push_back(P(pend.viewY2, Ty::Int));
            p.push_back(P(")"));
            appendArgs(p, {opt(BX), opt(DX)}, true);
            addText(std::move(p));
            return;
        }
        case 0x75: addText("VIEW"); return;
        case 0x76: addText("TIMER ON"); return;
        case 0x77: addText("TIMER OFF"); return;
        case 0x78: addText("TIMER STOP"); return;
        case 0x79: printEnd(); return;
        case 0x7A: pend.openAccess = intArg(BX); return;
        case 0x7B: pend.lockRecs.push_back(deref(reg(BX), Ty::Sng)); return;
        case 0x7C: {
            ExprPtr s = strBX();
            if (s->kind == EK::StrLit && s->sval.empty()) addText("SHELL");
            else stmt1("SHELL", s);
            return;
        }
        case 0x7D: addText({P("IOCTL #"), P(intArg(BX), Ty::Int), P(", "), P(ptrArg(DX, Ty::Str))}); return;
        case 0x7E: stmt1("ENVIRON", strBX()); return;
        case 0x7F: stmt1("CHDIR", strBX()); return;
        case 0x80: stmt1("MKDIR", strBX()); return;
        case 0x81: stmt1("RMDIR", strBX()); return;
        case 0x84: case 0x87:
            flushPendingStatements();
            pend.p2 = Pending::Pt();
            gfxPoint(pend.p1, reg(BX), reg(DX), regIsFloatType(reg(CX)), regIsFloatType(reg(AX)));
            return;
        case 0x85: case 0x88:
            gfxPoint(pend.p2, reg(BX), reg(DX), regIsFloatType(reg(CX)), regIsFloatType(reg(AX)));
            return;
        case 0x89:
            gfxPoint(pend.p1, reg(BX), reg(DX), regIsFloatType(reg(CX)), regIsFloatType(reg(AX)));
            return;
        case 0x8D:
            flushPendingStatements();
            gfxPoint(pend.p1, reg(BX), reg(DX), regIsFloatType(reg(CX)), regIsFloatType(reg(AX)));
            return;
        case 0x86: {
            std::vector<Part> p{P("LINE ")};
            if (pend.p1.valid) for (auto& x : pointParts(pend.p1)) p.push_back(x);
            p.push_back(P("-"));
            for (auto& x : pointParts(pend.p2)) p.push_back(x);
            int32_t box = -1;
            isIntLit(intArg(DX), &box);
            ExprPtr boxE = box == 0 ? mkRaw("B") : box == 1 ? mkRaw("BF") : nullptr;
            appendArgs(p, {opt(BX), boxE, opt(CX)}, true);
            addText(std::move(p));
            pend.p1 = pend.p2 = Pending::Pt();
            return;
        }
        case 0x8A: case 0x8B: {
            std::vector<Part> p{P(rt.op == 0x8B ? "PSET " : "PRESET ")};
            for (auto& x : pointParts(pend.p1)) p.push_back(x);
            appendArgs(p, {opt(BX)}, true);
            addText(std::move(p));
            pend.p1 = Pending::Pt();
            return;
        }
        case 0x8C: {
            std::vector<Part> p{P("CIRCLE ")};
            for (auto& x : pointParts(pend.p1)) p.push_back(x);
            p.push_back(P(", "));
            p.push_back(P(ptrArg(BX, Ty::Sng)));
            appendArgs(p, {opt(DX), pend.circleStart, pend.circleEnd, pend.circleAspect}, true);
            pend.circleStart = pend.circleEnd = pend.circleAspect = nullptr;
            pend.p1 = Pending::Pt();
            addText(std::move(p));
            return;
        }
        case 0xA1:
            if (isMinus1(intArg(BX))) addText("VIEW PRINT");
            else addText({P("VIEW PRINT "), P(intArg(BX), Ty::Int), P(" TO "), P(intArg(DX), Ty::Int)});
            return;
        case 0xA5: addText({P("POKE "), P(ptrArg(BX, Ty::Sng)), P(", "), P(intArg(DX), Ty::Int)}); return;
        default: break;
        }
    } else if (rt.intNo == 0x3f) {
        auto facOp = [&](Op op, ExprPtr a, ExprPtr b, Ty t) {
            setFac(mkBinary(op, std::move(a), std::move(b), t), t);
        };
        auto slot = [&](int byte, Ty t) -> ExprPtr {
            int n = byte & 0x7f;
            auto it = st.slots.find(n);
            if (it == st.slots.end() || !it->second.e) return mkUnknown("slot", t);
            if (st.slotTy[n] != t) return mkConv(it->second.e, t);
            return it->second.e;
        };
        auto storePtr = [&](const ExprPtr& ptr, Ty t, ExprPtr value, Val* src) {
            int32_t a;
            if (isIntLit(ptr, &a)) {
                if ((a == 0x1A && t == Ty::Sng) || (a == 0x16 && t == Ty::Dbl)) {
                    setFac(value, t);
                    return;
                }
                if (isSlotAddr(uint16_t(a)) && everSlot.count((uint16_t(a) - slotBase) / 8)) {
                    int n = (uint16_t(a) - slotBase) / 8;
                    st.slots[n].e = value;
                    st.slots[n].fb = nullptr;
                    st.slotTy[n] = t;
                    return;
                }
            }
            storeTo(lvalue(ptr, t), value, src);
        };
        Ty sd = Ty::Sng;
        switch (rt.op) {
        case 0x00: addText({P("POKE "), P(ptrArg(BX, Ty::Sng)), P(", "), P(intArg(DX), Ty::Int)}); return;
        case 0x01: {
            // /D static array offset: subscripts on the stack, layout inline
            int dims = rt.byteArg(1) / 2;
            std::vector<ExprPtr> idx(static_cast<size_t>(dims));
            for (int i = dims - 1; i >= 0; --i) {
                if (st.stack.empty()) { idx[size_t(i)] = mkUnknown("subscript"); continue; }
                idx[size_t(i)] = st.stack.back().e;
                st.stack.pop_back();
            }
            std::string name = "@index:";
            for (int i = 0; i < dims; ++i) {
                if (i) name += ",";
                name += std::to_string(rt.byteArg(size_t(2 + 2 * i)) | (rt.byteArg(size_t(3 + 2 * i)) << 8));
            }
            auto e = std::make_shared<Expr>(*mkFunc(name, idx, Ty::Int));
            e->ival = rt.byteArg(0);
            setReg(AX, e);
            return;
        }
        case 0x02: {
            ExprPtr t = intArg(DX);
            int32_t v;
            if (isIntLit(t, &v) && v == 0) addText("ON ERROR GOTO 0");
            else if (isIntLit(t, &v)) {
                Stmt s = mkTextStmt(curAddr, {P("ON ERROR GOTO ")});
                s.target = uint16_t(v);
                s.hasTarget = true;
                flushPendingStatements();
                addStmt(s);
            } else addText("ON ERROR GOTO ???");
            return;
        }
        case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: {
            std::vector<Part> p;
            switch (rt.op) {
            case 0x03: p = {P("ON COM("), P(intArg(BX), Ty::Int), P(") GOSUB ")}; break;
            case 0x04: p = {P("ON KEY("), P(intArg(BX), Ty::Int), P(") GOSUB ")}; break;
            case 0x05: p = {P("ON PEN GOSUB ")}; break;
            case 0x06: p = {P("ON STRIG("), P(intArg(BX), Ty::Int), P(") GOSUB ")}; break;
            case 0x07: p = {P("ON TIMER("), P(ptrArg(BX, Ty::Sng)), P(") GOSUB ")}; break;
            default: p = {P("ON PLAY("), P(intArg(BX), Ty::Int), P(") GOSUB ")}; break;
            }
            int32_t v;
            Stmt s = mkTextStmt(curAddr, p);
            if (isIntLit(intArg(DX), &v) && v != 0) {
                s.target = uint16_t(v);
                s.hasTarget = true;
            } else {
                s.parts.push_back(P("0"));
            }
            flushPendingStatements();
            addStmt(s);
            return;
        }
        case 0x09: {
            int32_t v;
            Stmt s = mkTextStmt(curAddr, {P("RESUME ")});
            if (isIntLit(intArg(BX), &v)) {
                s.target = uint16_t(v);
                s.hasTarget = true;
            } else s.parts.push_back(P("???"));
            flushPendingStatements();
            addStmt(s);
            return;
        }
        case 0x0A: addText({P("RSET "), P(ptrArg(DX, Ty::Str)), P(" = "), P(strBX())}); return;
        case 0x0C: return;
        case 0x0D: case 0x0E: case 0x0F: case 0x10: {
            static const Ty ts[] = {Ty::Sng, Ty::Dbl, Ty::Int, Ty::Str};
            ExprPtr v = ptrArg(BX, ts[rt.op - 0x0D]);
            addText({P("READ "), P(v)});
            invalidateLoc(v);
            return;
        }
        case 0x11: case 0x12: case 0x13: case 0x14: {
            static const Ty ts[] = {Ty::Sng, Ty::Dbl, Ty::Int, Ty::Str};
            Ty t = ts[rt.op - 0x11];
            ExprPtr a = ptrArg(DI, t), b = ptrArg(SI, t);
            addText({P("SWAP "), P(a), P(", "), P(b)});
            invalidateLoc(a);
            invalidateLoc(b);
            return;
        }
        case 0x15: case 0x16: case 0x17: case 0x18: {
            static const Ty ts[] = {Ty::Sng, Ty::Dbl, Ty::Int, Ty::Str};
            setBxStr(fn1("VARPTR$", ptrArg(BX, ts[rt.op - 0x15]), Ty::Str));
            return;
        }
        case 0x19: setReg(BX, mkConv(ptrArg(SI, Ty::Sng), Ty::Int)); return;
        case 0x1A: setReg(BX, mkConv(ptrArg(SI, Ty::Dbl), Ty::Int)); return;
        case 0x1B: setReg(BX, mkConv(facValue(Ty::Sng), Ty::Int)); return;
        case 0x1C: setReg(BX, mkConv(facValue(Ty::Dbl), Ty::Int)); return;
        case 0x1D: case 0x1E: case 0x1F: case 0x20: {
            ExprPtr v = rt.op == 0x1D ? ptrArg(SI, Ty::Sng) : rt.op == 0x1E ? ptrArg(SI, Ty::Dbl)
                      : facValue(rt.op == 0x1F ? Ty::Sng : Ty::Dbl);
            Ty t = (rt.op == 0x1D || rt.op == 0x1F) ? Ty::Sng : Ty::Dbl;
            auto e = std::make_shared<Expr>(*mkBinary(Op::Ne, v, t == Ty::Sng ? mkSng(0) : mkDbl(0), Ty::Int));
            e->flags = 1; // implicit truth test ("IF x THEN")
            setReg(BX, e);
            return;
        }
        case 0x21: case 0x22: {
            ExprPtr v = rt.op == 0x21 ? ptrArg(SI, Ty::Sng) : facValue(Ty::Sng);
            auto e = std::make_shared<Expr>(*mkConv(v, Ty::Int));
            e->flags = 1; // unsigned conversion (addresses)
            setReg(BX, e);
            return;
        }
        case 0x24: case 0x26: case 0x28: case 0x2A: case 0x2C: case 0x2E: case 0x30: case 0x32:
            sd = Ty::Dbl;
            [[fallthrough]];
        case 0x23: case 0x25: case 0x27: case 0x29: case 0x2B: case 0x2D: case 0x2F: case 0x31: {
            int base = rt.op - (sd == Ty::Dbl ? 1 : 0);
            switch (base) {
            case 0x23: facOp(Op::Pow, ptrArg(SI, sd), ptrArg(DI, sd), sd); return;
            case 0x25: facOp(Op::Pow, facValue(sd), ptrArg(DI, sd), sd); return;
            case 0x27: facOp(Op::Pow, ptrArg(SI, sd), facValue(sd), sd); return;
            case 0x29: facOp(Op::Pow, slot(rt.byteArg(), sd), facValue(sd), sd); return;
            case 0x2B: setFac(fn1("ABS", ptrArg(SI, sd), sd), sd); return;
            case 0x2D: setFac(fn1("ABS", facValue(sd), sd), sd); return;
            case 0x2F: setFac(fn1("SGN", ptrArg(SI, sd), sd), sd); return;
            case 0x31: setFac(fn1("SGN", facValue(sd), sd), sd); return;
            }
            return;
        }
        case 0x33: addText("RESTORE"); return;
        case 0x34: {
            int32_t key;
            Stmt s = mkTextStmt(curAddr, {P("RESTORE ")});
            if (isIntLit(intArg(BX), &key)) {
                // the label is on the first DATA statement at or after the key
                uint16_t best = 0xffff;
                for (const DataLine& d : prog.dataLines)
                    if (d.key >= uint16_t(key) && d.key < best) best = d.key;
                s.target = best == 0xffff ? uint16_t(key) : best;
                s.hasTarget = true;
                if (pass == 1) info.restoreTargets.insert(s.target);
            }
            flushPendingStatements();
            addStmt(s);
            return;
        }
        case 0x35: case 0x36: printItem(fn1("SPC", intArg(BX), Ty::Unknown), '?'); return;
        case 0x37: case 0x38: printItem(fn1("TAB", intArg(BX), Ty::Unknown), '?'); return;
        case 0x39: case 0x3A: return;
        case 0x3B: setReg(BX, mkPtrTo(strBX())); return;
        case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x41: case 0x42: {
            // /D dynamic array access with subscripts on the stack
            ExprPtr desc = reg(BX);
            int32_t a = 0;
            isIntLit(desc, &a);
            int dims = 1;
            auto dit = info.dynDims.find(uint16_t(a));
            if (dit != info.dynDims.end()) dims = dit->second;
            std::vector<ExprPtr> idx(static_cast<size_t>(dims));
            for (int i = dims - 1; i >= 0; --i) {
                if (st.stack.empty()) { idx[size_t(i)] = mkUnknown("subscript"); continue; }
                idx[size_t(i)] = st.stack.back().e;
                st.stack.pop_back();
            }
            Ty t = dynType(desc);
            ExprPtr el = desc->kind == EK::Param ? mkArrElem(0, t, idx, AF_Dynamic | AF_Param, desc->ival)
                                                 : mkArrElem(uint16_t(a), t, idx, AF_Dynamic);
            switch (rt.op) {
            case 0x3C:
                if (t == Ty::Int) setReg(AX, el);
                else setFac(el, t == Ty::Unknown ? Ty::Sng : t);
                return;
            case 0x3D: storeTo(el, facOrAxResult(t), nullptr); return;
            case 0x3E: pend.dynTarget = el; return;
            case 0x3F: addText({P("SWAP "), P(pend.dynTarget), P(", "), P(el)}); return;
            case 0x41: setFac(fn1("VARPTR", el, Ty::Sng), Ty::Sng); return;
            default: {
                auto e = std::make_shared<Expr>(*mkFunc("@index", idx, Ty::Int));
                setReg(AX, e);
                return;
            }
            }
        }
        case 0x40: case 0xC8: {
            ExprPtr v = ptrArg(SI, pend.dynTarget ? pend.dynTarget->type : Ty::Unknown);
            addText({P("SWAP "), P(pend.dynTarget), P(", "), P(v)});
            return;
        }
        case 0x43: case 0x44: case 0x45: case 0x46: case 0x4F: case 0x50: case 0x51: case 0x52: {
            static const Ty ts[] = {Ty::Sng, Ty::Dbl, Ty::Int, Ty::Str};
            bool redim = rt.op >= 0x4F;
            Ty t = ts[(rt.op - (redim ? 0x4F : 0x43))];
            int dims = rt.byteArg();
            std::vector<ExprPtr> bounds(static_cast<size_t>(dims));
            for (int i = dims - 1; i >= 0; --i) {
                if (st.stack.empty()) { bounds[size_t(i)] = mkUnknown("dim"); continue; }
                bounds[size_t(i)] = simplifyInt(mkBinary(Op::Sub, st.stack.back().e, mkInt(1), Ty::Int));
                st.stack.pop_back();
            }
            int32_t a = 0;
            isIntLit(reg(BX), &a);
            if (pass == 1) {
                info.dynArrays[uint16_t(a)] = t;
                info.dynDims[uint16_t(a)] = dims;
            }
            std::vector<Part> p{P(redim ? "REDIM " : "DIM "), P(arrayRef(uint16_t(a), t, true)), P("(")};
            for (int i = 0; i < dims; ++i) {
                if (i) p.push_back(P(", "));
                p.push_back(P(bounds[size_t(i)], Ty::Int));
            }
            p.push_back(P(")"));
            addText(std::move(p));
            return;
        }
        case 0x47: case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: {
            static const Ty ts[] = {Ty::Sng, Ty::Dbl, Ty::Int, Ty::Str};
            bool dyn = rt.op <= 0x4A;
            Ty t = ts[(rt.op - (dyn ? 0x47 : 0x4B))];
            int32_t a = 0, bytes = 0;
            isIntLit(reg(DI), &a);
            if (!dyn && pass == 1 && isIntLit(intArg(CX), &bytes)) info.arrayBytes[uint16_t(a)] = uint16_t(bytes);
            if (!dyn && pass == 1) info.staticArrays[uint16_t(a)] = t;
            addText({P("ERASE "), P(arrayRef(uint16_t(a), t, dyn))});
            invalidateAllMemory();
            return;
        }
        case 0x53: case 0x54: return;
        case 0x55: {
            ExprPtr a = deref(reg(AX), Ty::Str), b = strBX();
            setBxStr(mkBinary(Op::Add, a, b, Ty::Str));
            return;
        }
        case 0x56: case 0x57: {
            Ty t = rt.op == 0x56 ? Ty::Dbl : Ty::Sng;
            ExprPtr v = intArg(BX);
            int32_t a;
            // an address converted to a number is VARPTR(variable)
            if (isIntLit(reg(BX), &a) && uint16_t(a) >= prog.dsVarStart && uint16_t(a) < prog.dsEnd &&
                !prog.dsInit[uint16_t(a)] && st.r[BX].e->kind == EK::IntLit) {
                setFac(fn1("VARPTR", mkVar(uint16_t(a), Ty::Unknown), t), t);
                return;
            }
            setFac(mkConv(v, t), t);
            return;
        }
        case 0x58: {
            Stmt s;
            s.kind = SK::Gosub;
            s.target = rt.word;
            s.hasTarget = true;
            flushPendingStatements();
            addStmt(s);
            State keep = st;
            st = unknownState();
            st.stack = keep.stack;
            return;
        }
        case 0x59: return;
        case 0x5A: {
            ExprPtr v = strBX();
            if (pend.input) {
                pend.input = false;
                std::string head = pend.inputLinePrompt.empty() ? pend.inputHead : pend.inputLinePrompt;
                pend.inputLinePrompt.clear();
                // "INPUT" head was built for the keyboard form
                if (head.rfind("INPUT", 0) == 0) head = "LINE " + head;
                uint16_t a = curAddr;
                curAddr = pend.inputAddr;
                if (pend.inputFile) addText({P(head), P(pend.inputFile, Ty::Int), P(", "), P(v)});
                else addText({P(head), P(v)});
                pend.inputFile = nullptr;
                curAddr = a;
            } else {
                addText({P("LINE INPUT "), P(v)});
            }
            invalidateLoc(v);
            return;
        }
        case 0x5B: addText({P("LSET "), P(ptrArg(DX, Ty::Str)), P(" = "), P(strBX())}); return;
        case 0x5C: {
            std::vector<Part> p{P("MID$("), P(ptrArg(DX, Ty::Str)), P(", "), P(intArg(CX), Ty::Int)};
            if (!isOmitted7fff(intArg(AX))) {
                p.push_back(P(", "));
                p.push_back(P(intArg(AX), Ty::Int));
            }
            p.push_back(P(") = "));
            p.push_back(P(strBX()));
            addText(std::move(p));
            invalidateLoc(ptrArg(DX, Ty::Str));
            return;
        }
        case 0x5D: {
            Stmt s = mkTextStmt(curAddr, {P("ON "), P(intArg(BX), Ty::Int), P(" GOSUB ")});
            s.targets = rt.targets;
            flushPendingStatements();
            addStmt(s);
            State keep = st;
            st = unknownState();
            st.stack = keep.stack;
            return;
        }
        case 0x5E:
            if (emit) results[procIndex][block->id].selector = intArg(BX);
            return;
        case 0x5F: popRet = true; return;
        case 0x60: return;
        case 0x61: {
            ExprPtr dst = lvalue(reg(DX), Ty::Str);
            ExprPtr src = strBX();
            storeTo(dst, src, nullptr);
            return;
        }
        case 0x62:
            st.flags.kind = Flags::RtCmp;
            st.flags.a = deref(reg(AX), Ty::Str);
            st.flags.b = strBX();
            return;
        case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: case 0x68: case 0x69: case 0x6A:
        case 0x6B: case 0x6C: case 0x6D: case 0x6E: {
            Ty t = printType(rt.op);
            ExprPtr v = t == Ty::Int ? intArg(BX) : ptrArg(BX, t);
            char sep = rt.op <= 0x66 ? ',' : rt.op <= 0x6A ? ';' : 0;
            printItem(v, sep);
            return;
        }
        case 0x6F: setFac(ptrArg(SI, Ty::Sng), Ty::Sng); return;
        case 0x70: setFac(ptrArg(SI, Ty::Dbl), Ty::Dbl); return;
        case 0x71: case 0x72: {
            int n = rt.byteArg() & 0x7f;
            everSlot.insert(n);
            Ty t = rt.op == 0x71 ? Ty::Sng : Ty::Dbl;
            if (st.facTy == t) st.slots[n] = st.fac;
            else { st.slots[n].e = facValue(t); st.slots[n].fb = nullptr; }
            st.slotTy[n] = t;
            return;
        }
        case 0x73: setFac(mkConv(ptrArg(SI, Ty::Sng), Ty::Dbl), Ty::Dbl); return;
        case 0x74: setFac(mkConv(facValue(Ty::Sng), Ty::Dbl), Ty::Dbl); return;
        case 0x75: setReg(BX, mkConv(ptrArg(SI, Ty::Sng), Ty::Int)); return;
        case 0x76: setReg(BX, mkConv(ptrArg(SI, Ty::Dbl), Ty::Int)); return;
        case 0x77: setReg(BX, mkConv(facValue(Ty::Sng), Ty::Int)); return;
        case 0x78: setReg(BX, mkConv(facValue(Ty::Dbl), Ty::Int)); return;
        case 0x79: setFac(mkConv(ptrArg(SI, Ty::Dbl), Ty::Sng), Ty::Sng); return;
        case 0x7A: setFac(mkConv(facValue(Ty::Dbl), Ty::Sng), Ty::Sng); return;
        case 0x7B: case 0x7C: {
            Ty t = rt.op == 0x7B ? Ty::Sng : Ty::Dbl;
            storePtr(reg(DI), t, ptrArg(SI, t), nullptr);
            return;
        }
        case 0x7D: case 0x7E: {
            Ty t = rt.op == 0x7D ? Ty::Sng : Ty::Dbl;
            ExprPtr v = facValue(t);
            st.fac.e = v;
            st.facTy = t;
            storePtr(reg(DI), t, v, &st.fac);
            return;
        }
        default: break;
        }
        // arithmetic and compare groups
        if (rt.op >= 0x7F && rt.op <= 0xAE) {
            int group = (rt.op - 0x7F) / 8;  // add, div, mul, sub, cmp, cmp0/mul2n
            int k = (rt.op - 0x7F) % 8;
            Ty t = (k % 2) ? Ty::Dbl : Ty::Sng;
            int form = k / 2;               // 0: SI op DI, 1: FAC op DI, 2: SI op FAC / FAC op SI, 3: slot op FAC
            if (group <= 3) {
                static const Op ops[] = {Op::Add, Op::Div, Op::Mul, Op::Sub};
                Op op = ops[group];
                ExprPtr a, b;
                switch (form) {
                case 0: a = ptrArg(SI, t); b = ptrArg(DI, t); break;
                case 1: a = facValue(t); b = ptrArg(DI, t); break;
                case 2:
                    if (op == Op::Add || op == Op::Mul) { a = facValue(t); b = ptrArg(SI, t); }
                    else { a = ptrArg(SI, t); b = facValue(t); }
                    break;
                default: a = slot(rt.byteArg(), t); b = facValue(t); break;
                }
                facOp(op, a, b, t);
                return;
            }
            if (group == 4) { // 0x9F - 0xA6 compares
                ExprPtr a, b;
                switch (form) {
                case 0: a = ptrArg(SI, t); b = ptrArg(DI, t); break;
                case 1: a = facValue(t); b = ptrArg(DI, t); break;
                case 2: a = ptrArg(SI, t); b = facValue(t); break;
                default: a = slot(rt.byteArg(), t); b = facValue(t); break;
                }
                st.flags.kind = Flags::RtCmp;
                st.flags.a = a;
                st.flags.b = b;
                return;
            }
            // group 5: 0xA7 - 0xAE
            switch (rt.op) {
            case 0xA7: case 0xA8: {
                Ty tt = rt.op == 0xA7 ? Ty::Sng : Ty::Dbl;
                st.flags.kind = Flags::RtCmp;
                st.flags.a = ptrArg(SI, tt);
                st.flags.b = tt == Ty::Sng ? mkSng(0) : mkDbl(0);
                return;
            }
            case 0xA9: case 0xAA: {
                Ty tt = rt.op == 0xA9 ? Ty::Sng : Ty::Dbl;
                st.flags.kind = Flags::RtCmp;
                st.flags.a = facValue(tt);
                st.flags.b = tt == Ty::Sng ? mkSng(0) : mkDbl(0);
                return;
            }
            case 0xAB: case 0xAC: case 0xAD: case 0xAE: {
                Ty tt = (rt.op == 0xAB || rt.op == 0xAD) ? Ty::Sng : Ty::Dbl;
                ExprPtr a = rt.op <= 0xAC ? ptrArg(SI, tt) : facValue(tt);
                double m = double(1 << rt.byteArg());
                facOp(Op::Mul, a, tt == Ty::Sng ? mkSng(m) : mkDbl(m), tt);
                return;
            }
            }
        }
        switch (rt.op) {
        case 0xAF: setFac(mkUnary(Op::Neg, ptrArg(SI, Ty::Sng), Ty::Sng), Ty::Sng); return;
        case 0xB0: setFac(mkUnary(Op::Neg, ptrArg(SI, Ty::Dbl), Ty::Dbl), Ty::Dbl); return;
        case 0xB1: setFac(mkUnary(Op::Neg, facValue(Ty::Sng), Ty::Sng), Ty::Sng); return;
        case 0xB2: setFac(mkUnary(Op::Neg, facValue(Ty::Dbl), Ty::Dbl), Ty::Dbl); return;
        case 0xB3:
            flushPendingStatements();
            pend.field = true;
            pend.fieldFile = intArg(BX);
            pend.fieldVars.clear();
            pend.fieldAddr = curAddr;
            return;
        case 0xB4:
            if (!pend.field) warn("FIELD variable without FIELD");
            pend.fieldVars.emplace_back(strBX(), intArg(DX));
            return;
        case 0xB5: {
            flushPendingStatements();
            int flags = rt.byteArg();
            ExprPtr prompt = strBX();
            std::string head = (flags & 1) ? "INPUT; " : "INPUT ";
            pend.input = true;
            pend.inputAddr = curAddr;
            pend.inputVars.clear();
            pend.inputFile = nullptr;
            pend.inputLinePrompt.clear();
            pend.inputCount = 1;
            if (prompt->kind == EK::StrLit && prompt->sval.empty() && !(flags & 2)) {
                pend.inputHead = head;
            } else {
                std::string q = "\"" + litText(prompt) + "\"";
                pend.inputHead = head + q + ((flags & 2) ? ", " : "; ");
                pend.inputLinePrompt = head + q + "; ";
            }
            // LINE INPUT has flag 2 set and no "?" prompt
            if (prompt->kind == EK::StrLit && prompt->sval.empty() && (flags & 2)) pend.inputHead = pend.inputLinePrompt = head;
            return;
        }
        case 0xB6:
            flushPendingStatements();
            pend.input = true;
            pend.inputAddr = curAddr;
            pend.inputVars.clear();
            pend.inputHead = "INPUT #";
            pend.inputFile = intArg(BX);
            pend.inputCount = 1000; // set by INPUT_ARGS
            return;
        case 0xB7: {
            int n = rt.byteArg(0);
            pend.inputTypes.clear();
            for (int i = 0; i < n; ++i) {
                static const Ty ts[] = {Ty::Unknown, Ty::Int, Ty::Sng, Ty::Dbl, Ty::Str};
                int tt = rt.byteArg(size_t(1 + i));
                pend.inputTypes.push_back(tt >= 1 && tt <= 4 ? ts[tt] : Ty::Unknown);
            }
            pend.inputCount = n;
            return;
        }
        case 0xB8: {
            size_t i = pend.inputVars.size();
            Ty t = i < pend.inputTypes.size() ? pend.inputTypes[i] : Ty::Unknown;
            ExprPtr v = ptrArg(BX, t);
            invalidateLoc(v);
            inputStore(v);
            return;
        }
        case 0xB9:
            invalidateLoc(pend.dynTarget);
            inputStore(pend.dynTarget ? pend.dynTarget : mkUnknown("dyn"));
            return;
        case 0xBA: setReg(BX, fn1("LEN", strBX(), Ty::Int)); return;
        case 0xBB: setReg(BX, fn1("ASC", strBX(), Ty::Int)); return;
        case 0xBC: case 0xBD: case 0xBE: case 0xBF: case 0xC0: case 0xC1:
            flushPendingStatements();
            if (pend.print) {
                warn("nested PRINT");
                printEnd();
            }
            pend.print = true;
            pend.printWrite = false;
            pend.printUsing = false;
            pend.printItems.clear();
            pend.printAddr = curAddr;
            switch (rt.op) {
            case 0xBC: pend.printHead = "PRINT"; break;
            case 0xC0: pend.printHead = "LPRINT"; break;
            case 0xBD: case 0xC1:
                pend.printHead = rt.op == 0xBD ? "PRINT" : "LPRINT";
                pend.printUsing = true;
                pend.printItems.emplace_back(strBX(), 'U');
                break;
            case 0xBE:
                pend.printHead = "PRINT";
                pend.printItems.emplace_back(intArg(BX), 'F');
                break;
            case 0xBF:
                pend.printHead = "PRINT";
                pend.printUsing = true;
                pend.printItems.emplace_back(intArg(DX), 'F');
                pend.printItems.emplace_back(strBX(), 'U');
                break;
            }
            return;
        case 0xC4: {
            Ty t = dynType(reg(BX));
            ExprPtr el = dynElem(t);
            if (t == Ty::Int) setReg(AX, el);
            else if (t == Ty::Sng || t == Ty::Dbl) setFac(el, t);
            else { setReg(AX, el); setFac(el, Ty::Unknown); }
            return;
        }
        case 0xC5: {
            Ty t = dynType(reg(BX));
            ExprPtr el = dynElem(t);
            storeTo(el, facOrAxResult(t), nullptr);
            return;
        }
        case 0xC6: pend.dynTarget = dynElem(dynType(reg(BX))); return;
        case 0xC7: {
            ExprPtr el = dynElem(dynType(reg(BX)));
            addText({P("SWAP "), P(el), P(", "), P(pend.dynTarget)});
            invalidateLoc(el);
            return;
        }
        case 0xC9: setFac(fn1("VARPTR", dynElem(dynType(reg(BX))), Ty::Sng), Ty::Sng); return;
        case 0xCA: case 0xCB: {
            Ty t = rt.op == 0xCA ? Ty::Sng : Ty::Dbl;
            int n = rt.byteArg() & 0x7f;
            auto it = st.slots.find(n);
            if (it != st.slots.end() && st.slotTy[n] == t) {
                Val v = it->second;
                setFac(v.e, t);
                st.fac.fb = v.fb;
            } else {
                setFac(slot(rt.byteArg(), t), t);
            }
            return;
        }
        default: break;
        }
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "unhandled runtime call INT %02Xh/%02X", rt.intNo, rt.op);
    warn(buf);
    addText({P(std::string("REM ") + buf)});
}

} // namespace qb
